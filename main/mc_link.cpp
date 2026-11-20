// SPDX-License-Identifier: MIT
#include "mc_link.h"

#include "rptr_link.h"   // S514/EST-453: the RPTR receive hooks and the tx door

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>   // fsync(fileno()): finish a node file before the rename makes it current
// No <time.h> here: chrome.h -> pages.h defines `namespace tm`, and <time.h> defines `struct tm` — the two cannot
// live in one translation unit (time_source.h says the same). The radio task asks time_source::epochNow() instead.

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"   // esp_timer_get_time(): the advert schedule
#include "esp_vfs_fat.h" // S515/EST-464: /int (the launcher's locfd partition), mounted exactly as it mounts it
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"   // the store lock
#include "freertos/task.h"   // uxTaskGetStackHighWaterMark: the stack evidence after each request   // esp_fill_random() for the one-time identity seed
#include "nvs.h"
#include "wear_levelling.h"   // wl_handle_t for the /int FAT mount

#include "advert_rule.h"   // S513/EST-435: one advert per 5 s, host-tested
#include "contact_blob.h"  // S513/EST-437: the contact table as one versioned NVS blob
#include "node_table.h"   // S515/EST-463: the node table's rules (advert -> archive, add/remove radio)
#include "node_store.h"   // S515/EST-464: /int radio.bin, SD nodes.bin, the boot merge and the migration
#include "contact_admin.h"  // S514/EST-447: remove/clear a contact, and the new-node notice's default
#include "chrome.h"        // S513/EST-435/437: the app's one notice slot, safe from any task
#include "mc_channels.h"
#include "tx_queue.h"   // S513: the send queue's rules, host-tested
#include "mc_identity.h"
#include "mc_crypto.h"
#include "mc_wire.h"
#include "channel_scope.h"   // S513/EST-440: per-channel flood scope, ported from the L2's S509
#include "channel_admin.h"    // S514/EST-446: add/rename/remove a channel, and the safe NVS rewrite
#include "mc_dm.h"      // S513/EST-433/434: TXT_MSG open + the two reply frames
#include "dm_send.h"    // S513/EST-436/439: compose, the PATH parser, the pending/ACK table and the retry rule
#include "radio_link.h"
#include "audio_out.h"     // S514/EST-450: the notification sounds (queue-only, never blocks this task)
#include "settings.h"     // EST-445: the name rule and the advert-interval mapping (pure, host-tested)
#include "sd_history.h"   // step 5: the chat history on the card
#include "chat_layout.h"  // step 5 / S503: historyReadCount, the read-prefix rule for the seen point
#include "time_source.h"
#include "clock.h"

namespace om {
namespace mc {
namespace {

const char* TAG = "tm/mc";

// ONE home for the node's name (Filip, S501: renamed from the placeholder "EDCT" to "TNMT (SI)"; EST-445:
// made editable and stored in NVS mcid/name). MeshCore forbids [ ] \ : , ? * inside an advert name; a space
// and parentheses are fine (AdvertDataHelpers.cpp:31-34). `g_node_name` is what an advert announces AND
// what a channel text / DM signs, so the two can never announce different identities. The default is
// tm::kDefaultNodeName ("TNMT (SI)") when nothing is stored.
char g_node_name[32] = "TNMT (SI)";

// EST-445: the automatic advert interval, stored in NVS mcid/advint as SECONDS (0 = Off). The default is
// today's behaviour, 3 hours. The schedule in serviceRequests() reads this; the manual advert paths do not.
uint32_t g_advint_s = 0;   // S514: Off until loadAdvertInterval() reads a stored choice

// The L2's words for an advert type (firmware/src/ui/node_menu.h:27-35, advTypeName) — the new-node notice and the
// Nodes sheet in the L2 name a type the same way, so the two cannot disagree. S513/EST-437. S514/EST-447: the ONE
// definition is now core/contact_admin.h::contactTypeLabel, because the NODES list sub-page names a type too.
const char* advertTypeName(uint8_t t) { return contactTypeLabel(t); }

// S514/EST-447: the new-node notice, cached from NVS "mcset"/"newnode" by begin() and rewritten by the Settings
// row. The radio task (which raises "New node: ...") must not open NVS; it reads this bool.
bool g_new_node_notice = true;


// EST-445: the schedule interval is NOT a constant any more. It is the value stored in NVS mcid/advint
// (g_advint_s, seconds; 0 = Off) and is read on every loop, so the Settings row changes it live. The manual
// test hook ('a' / "Advert now") is unaffected and still obeys the 5 s gate.

// MeshCore drops an advert whose timestamp is not NEWER than the last one it stored for that node (replay
// protection, BaseChatMesh.cpp:133-137). The P4's RTC restarts at 0 after a power loss — measured S501, "ts 6"
// then every advert we send is rejected by nodes that already know us, and nothing looks wrong from here.
// So the timestamp is floored by the highest value we have ever sent, kept in NVS. The DECISION is in
// core/clock.h (host-tested); this only reads and writes the NVS value.
uint32_t nextAdvertTimestamp() {
    uint32_t floor_ts = 0;
    nvs_handle_t h;
    if (nvs_open("mcid", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "advts", &floor_ts);   // absent -> 0
        nvs_close(h);
    }
    const uint32_t now = (uint32_t)om::time_source::epochNow();
    const uint32_t ts  = om::clock::advertFloor(now, floor_ts);
    if (nvs_open("mcid", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_u32(h, "advts", ts) == ESP_OK) nvs_commit(h);
        nvs_close(h);
    }
    return ts;
}

meshcore::ChannelSet g_set;
bool                 g_loaded = false;

// S514/EST-446: a Settings add/remove wrote NVS on the MAIN task; the radio task (the one owner of the
// table) reloads it on its next loop. `rev` lets a page notice that the table it drew has changed.
volatile bool g_channels_reload = false;
uint32_t     g_channels_rev    = 0;

// S513/EST-440: the channel scopes as last read from NVS "mcch" ("s<hh>" by the LIVE hash). The cache
// only saves a flash read per redraw; the key is still the loaded table's hash, never a legacy copy.
char g_scope[meshcore::kMaxChannels][meshcore::kScopeNameMax + 1];
bool g_scope_loaded[meshcore::kMaxChannels] = {};

// The store the pages read (S502, step 4). The radio task writes it, the UI reads it; the mutex is created in
// begin() and the guard below is the ONLY way in.
void* nodeTableAlloc(size_t bytes) {
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);   // last resort, logged by initNodeTable()
    return p;
}
void nodeTableFree(void* p) {
    if (p != nullptr) heap_caps_free(p);
}

// Runs before the first ContactStore is constructed (the installer at the bottom of this block, above g_contacts).
struct NodeTableInstaller {
    NodeTableInstaller() { setContactAlloc(&nodeTableAlloc, &nodeTableFree); }
} g_nodeTableInstaller;

ContactStore    g_contacts;
MessageRing     g_messages;
ReadState       g_read;
DmRing          g_dms;   // S513/EST-434: direct-message threads (RAM, item 3); SD history is item 4
SemaphoreHandle_t g_store_mutex = nullptr;

// S513/EST-437: the contact table survives a restart as ONE versioned blob in NVS ("mcct"/"tbl"). The radio task
// never writes NVS — it only raises a dirty flag under the store lock. The main task encodes the table under the
// same lock and writes it, so a flash write can never hold up receiving and two tasks can never write one blob.
//
// Two kinds of dirty, because they are worth different amounts: a contact that was ADDED, RENAMED or whose route
// changed is written on the next main-loop pass; last-heard and SNR move on every advert, so those wait for the
// 10-minute floor. The flags are cleared BEFORE the lock is released and the NVS write happens after that, so a
// mutation that lands during the write sets a flag again and is not lost.
constexpr const char* kContactsNvsNamespace = "mcct";
constexpr const char* kContactsNvsKey       = "tbl";
constexpr uint32_t    kContactsSoftWriteMs  = 10u * 60u * 1000u;   // last-heard/SNR floor
// S515/EST-461: a NEW-contact ack (the Nodes dot gone) is worth a write of its own, but never one per key press:
// at most one every 5 s, and one forced write when the user leaves the Nodes page.
constexpr uint32_t    kContactsAckWriteMs   = 5u * 1000u;
constexpr uint32_t    kContactsRetryMs      = 10u * 1000u;         // after a failed NVS write

bool     g_contacts_dirty_struct = false;
bool     g_contacts_dirty_soft   = false;
bool     g_contacts_dirty_ack    = false;
bool     g_contacts_ack_force    = false;   // "one write on leaving the page", past the 5 s floor
bool     g_contacts_write_failed = false;
uint32_t g_contacts_last_write_ms  = 0;
uint32_t g_contacts_last_attempt_ms = 0;
// S515/EST-463: the table is 2,000 rows (~312 KB) and the blob that carries it is ~316 KB, so NEITHER can be a
// .bss array any more: the app has ~570 KB of internal RAM in total and the screen, the rings and the mesh already
// live there. Both are PSRAM, taken once, and a failed allocation is reported instead of quietly shrinking the
// table (the store then behaves as a full table and logs it).
uint8_t* g_contact_blob = nullptr;   // restore reads it, the main task encodes it

// S515/EST-464: the two node files and the boot merge. The radio list is /int/om-lite/radio.bin (internal flash,
// the launcher's locfd partition) and the archive is /sdcard/om-lite/nodes.bin. The encoding buffer is PSRAM like
// the table: an archive of 2,000 rows is ~318 KB, far too much for internal RAM.
constexpr const char* kIntMountPoint   = "/int";
constexpr const char* kIntRadioDir     = "/int/om-lite";
constexpr const char* kRadioFilePath   = "/int/om-lite/radio.bin";
constexpr const char* kRadioTempPath   = "/int/om-lite/radio.tmp";
constexpr const char* kArchiveDir      = "/sdcard/om-lite";
constexpr const char* kArchiveFilePath = "/sdcard/om-lite/nodes.bin";
constexpr const char* kArchiveTempPath = "/sdcard/om-lite/nodes.tmp";
constexpr uint32_t    kArchiveWriteMs  = 30u * 1000u;   // the SD archive: at most one write per 30 s (+ forced)

bool          g_int_mounted   = false;
wl_handle_t   g_int_wl        = WL_INVALID_HANDLE;
bool          g_sd_ready      = false;   // the card the archive can use (main tells us at boot)
bool          g_archive_dirty = false;
bool          g_archive_force = false;
uint32_t      g_archive_last_write_ms = 0;
bool          g_archive_write_failed  = false;
uint32_t      g_radio_signature = 0;      // the bytes last written, so an archive-only change never rewrites flash
bool          g_radio_have_signature = false;
uint8_t       g_nvs_blob_version = 0;     // what restoreContacts() saw (0 = no blob); the migration picks the rule
HeardToRadio  g_heard_to_radio = kHeardToRadioDefault;
uint8_t*      g_node_file_buf = nullptr;  // the /int and SD files' encode/decode buffer (PSRAM)

void initNodeTable() {
    // S516 (EST-472 item 10): this is called from begin(), nodeStorageBegin() and restoreContacts(), and the
    // device log showed "node table: 2000 rows" three times. The ALLOCATIONS were already idempotent; the LOG
    // was not. One init, one line — the banner belongs to the first call that actually got the table.
    static bool s_reported = false;
    if (g_contacts.c == nullptr) {
        ESP_LOGE(TAG, "node table: allocation of %u B FAILED; every store starts empty and full",
                 (unsigned)contactsTableBytes());
        return;
    }
    if (g_contact_blob == nullptr) {
        g_contact_blob = static_cast<uint8_t*>(nodeTableAlloc(kContactBlobMaxBytes));
        if (g_contact_blob == nullptr) ESP_LOGE(TAG, "contacts: NVS blob scratch allocation FAILED");
    }
    if (g_node_file_buf == nullptr) {
        g_node_file_buf = static_cast<uint8_t*>(nodeTableAlloc(kNodeFileMaxBytes));
        if (g_node_file_buf == nullptr) ESP_LOGE(TAG, "node storage: file buffer allocation FAILED");
    }
    if (s_reported) return;
    s_reported = true;
    ESP_LOGI(TAG, "node table: %u rows, %u B in PSRAM (%u B/row)", (unsigned)kMaxNodes,
             (unsigned)contactsTableBytes(), (unsigned)sizeof(Contact));
}

// Caller holds the store lock (or is the single main-task reader before the radio task exists).
void markContactsDirty(bool structural) {
    if (structural) g_contacts_dirty_struct = true;
    else g_contacts_dirty_soft = true;
    g_archive_dirty = true;   // anything that changes the table changes the archive too
}

// S515/EST-461: an ack clears `fresh` on one row. It rides the same writer, with its own floor: it must not
// wait the 10-minute soft floor (the dot would come back after a restart) and it must not write per key press.
void markContactsAck(bool leaving_page) {
    g_contacts_dirty_ack = true;
    g_archive_dirty      = true;
    if (leaving_page) {
        g_contacts_ack_force = true;
        g_archive_force      = true;   // "on leaving Nodes": the archive is written now, past the 30 s floor
    }
}

// Step 5 / S503 — the id each channel's LOG FILE has reached, and the last seen point we appended. Both are per
// channel because a log file is per channel: the format numbers records inside one file, not globally.
// `g_next_log_id` is restored from the file at boot (highest id + 1), so a record written before a restart keeps
// the id that the Meta record refers to. Only touched under the store lock.
uint32_t g_next_log_id[kMaxChannels]   = {0};
uint32_t g_seen_written[kMaxChannels] = {0};   // 0 = no Meta record appended by us during this boot

// The node's identity: its own keypair, not a copy of the L2's (Filip, S500: "its own node on the mesh").
// MeshCore stores the 32-byte SEED and derives both keys from it, so that is what lives in NVS.
meshcore::Identity g_id;
bool               g_has_id = false;
char               g_pub_hex[17] = {0};   // first 8 bytes of the public key, for the log

// The radio has ONE owner (the radio task). Key handlers queue a request; the task performs it. See mc_link.h
// for why: the S501 stack-protection panic came from crypto on the wrong task and two writers on the link.
//
// S513: the queue used to be ONE global slot, read by the radio task while the main task could overwrite it — a
// second Enter during a ~0.5 s airtime could transmit a torn text, and a second request REPLACED the first, so a
// message could vanish after the composer had already been cleared. It is now a ring of FOUR complete requests
// (core/tx_queue.*, host-tested) and every push/pop holds g_tx_lock for a few microseconds — never across a
// transmission. The ring is the pure rule rather than an xQueueCreate() so the host tests exercise the code the
// device runs; the copy-by-value and "refuse when full" semantics are the FreeRTOS queue's.
TxQueue            g_txq;

// S514/EST-456: the two node actions' jobs. Written by the radio task (an answer, a deadline) and read by the
// MAIN task (the sheet, the notice), so every access goes through the store lock like the contact table.
ops::PingJob  g_ping;
ops::TraceView g_trace;
bool          g_trace_pending = false;
uint8_t       g_trace_key[meshcore::kPubKeyBytes] = {0};
uint32_t      g_trace_tag = 0;
ops::TraceJob g_trace_job;
uint32_t      g_trace_serial = 0;
uint32_t      g_ping_seen_state = 0;   // main-task poll: the last state it reported
uint32_t      g_tag_counter = 0;
SemaphoreHandle_t  g_tx_lock = nullptr;   // created once in begin()

// S513/EST-439: the DMs we sent and the ACK each one expects. The RADIO task matches an arriving ACK (it must not
// transmit, but it may read this table), the MAIN task begins/rearms/drops entries and polls the retry rule. One
// mutex, held for the few microseconds one of those operations takes — never across a transmit, never across the
// store lock.
meshcore::DmPendingTable g_dm_pending;
SemaphoreHandle_t        g_dm_lock = nullptr;   // created once in begin()

// S513/EST-441: defined with the seen-ring below, needed here first (queueFrame registers every frame it queues).
bool markOwnFrame(const uint8_t* frame, size_t len);

// A reply that was BUILT on the radio task (a DM ACK / path return) still leaves through the queue: the receive
// path must not transmit, because a send inside receive handling is how the S501 two-writers race started.
// S513/EST-441: every frame that passes here is registered as ours BEFORE it can reach the radio — this is the
// path a DM, a retry, an ACK and a path return all take, and the echo of any of them must be recognised.
bool queueFrame(const uint8_t* frame, size_t len) {
    if (g_tx_lock == nullptr || frame == nullptr || len == 0) return false;
    markOwnFrame(frame, len);
    xSemaphoreTake(g_tx_lock, portMAX_DELAY);
    const bool   ok    = g_txq.pushFrame(TxKind::Frame, frame, len);
    const size_t depth = g_txq.size();
    xSemaphoreGive(g_tx_lock);
    if (!ok) ESP_LOGW(TAG, "tx: queue full (%d waiting); a DM reply was NOT queued", (int)depth);
    return ok;
}

// S513/EST-435: ONE advert per 5 s, counted from the last advert that actually went on the air. The RULE is
// core/advert_rule.* (host-tested, no clock inside); this is only the state it reads and the millisecond timeline
// the device runs on. Three things touch it, all under g_tx_lock: a key press (main task), the schedule (radio task)
// and the transmission's own result (radio task).
om::mc::AdvertGate g_advert_gate;

uint32_t monotonicMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// S514/EST-456: MeshCore's getCurrentTimeUnique() role — a value that must not repeat within a session, because
// the answer is matched by key and tag. Seconds plus a per-send counter is unique here.
uint32_t uniqueTag() { return (uint32_t)((esp_timer_get_time() / 1000000) << 2) | (g_tag_counter++ & 0x03); }

// S513/EST-441: the seen-ring, so a frame WE transmitted that comes back over the mesh is not processed as
// incoming. The rule and the ring are core/mc_wire.* (host-tested); this is only the instance the device uses.
// It was 8 slots of 4-byte fingerprints with no idea whose frame an entry was. 8 was sized for receiving alone;
// every TRANSMIT now costs a slot too, and one DM attempt puts a frame plus a path return or ACK on the air, a
// retry repeats them, and adverts share the ring — so the echo of a retry could arrive after its own entry had
// been pushed out, which is the v41 defect. 32 slots x 5 bytes = 160 B and covers a whole 3-attempt cycle.
meshcore::SeenRing g_seen;

// The one place a frame of OURS is remembered, called BEFORE it goes out (the radio's send, or the queue that
// leads to it). Upstream's own words for this are in Mesh.cpp:651/680/713/723/736 — "mark this packet as already
// sent in case it is rebroadcast back to us" — and its hash is the payload, never the path (Packet.cpp:41-50),
// which is exactly why a repeater's copy of our frame matches: it only adds path bytes.
//
// The ring is touched by BOTH tasks now (the main task composes DMs and retries into queueFrame; the radio task
// receives and sends), so the few microseconds of an entry are taken under the existing transmit lock rather than
// under a second one. A lost entry here would mean an echo processed as incoming — the very defect — so it is
// worth the mutex.
bool markOwnFrame(const uint8_t* frame, size_t len) {
    if (frame == nullptr || len == 0) return false;
    meshcore::View v;
    if (!meshcore::decode(frame, len, &v)) return false;
    const uint32_t fp = meshcore::frameFingerprint(v);
    const bool     locked = g_tx_lock != nullptr;
    if (locked) xSemaphoreTake(g_tx_lock, portMAX_DELAY);
    g_seen.noteOurs(fp);
    if (locked) xSemaphoreGive(g_tx_lock);
    return true;
}

// ---- S513/EST-439: the DM send's shared state ---------------------------------------------------------------
// The thread's messages live in g_dms (seq is the id the pending table names); this finds one by that id.
DmMessage* findDmBySeq(uint32_t seq) {
    for (size_t i = 0; i < g_dms.count; i++) {
        DmMessage* m = &g_dms.m[(g_dms.head + i) % kMaxDms];
        if (m->seq == seq) return m;
    }
    return nullptr;
}

// The ACK arrived for `seq`: the delivered mark. Kept across a restart the L2's way — the message's log gets an
// Update record naming its log id (firmware/src/history/history_log.h Type::Update, the L2's own delivered mark),
// and loadDm applies it in file order on the next boot.
void markDmDelivered(uint32_t seq) {
    StoreLock l;
    DmMessage* m = findDmBySeq(seq);
    if (m == nullptr) {
        ESP_LOGI(TAG, "DM: ACK for message %u, which has already left the ring", (unsigned)seq);
        return;
    }
    m->delivery = om::ui::DmState::Delivered;
    if (m->log_id != 0) {
        if (!sdhistory::appendDmState(m->peer_pub, m->log_id, m->delivery, (uint32_t)om::time_source::epochNow())) {
            ESP_LOGW(TAG, "DM: delivered, but the Update record could not be written");
        }
    }
    ESP_LOGI(TAG, "DM: message %u DELIVERED (log id %u)", (unsigned)seq, (unsigned)m->log_id);
}

// Radio task: a 4-byte ACK crc from the air. The table decides WHICH message it proves (four bytes only, as
// MyMesh::processAck compares); a repeat simply matches nothing.
bool dmNoteAck(const uint8_t ack[4]) {
    if (g_dm_lock == nullptr || ack == nullptr) return false;
    uint32_t seq      = 0;
    bool     matched  = false;
    xSemaphoreTake(g_dm_lock, portMAX_DELAY);
    matched = meshcore::dmPendingAck(g_dm_pending, ack, &seq);
    xSemaphoreGive(g_dm_lock);
    if (matched) markDmDelivered(seq);
    return matched;
}

// Radio task: the paths a PATH packet may open are the contacts as they are NOW (their keys decide the MAC), and
// the table must not be walked while the main task writes it.
size_t snapshotContactPubs(uint8_t* pubs, size_t cap) {
    size_t n = 0;
    StoreLock l;
    n = g_contacts.count < cap ? g_contacts.count : cap;
    for (size_t i = 0; i < n; i++) memcpy(pubs + i * meshcore::kPubKeyBytes, g_contacts.c[i].pub,
                                          meshcore::kPubKeyBytes);
    return n;
}

void loadOrCreateIdentity() {
    uint8_t seed[meshcore::kSeedBytes];
    size_t  seed_len = sizeof(seed);

    nvs_handle_t h;
    if (nvs_open("mcid", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "identity: cannot open NVS namespace mcid");
        return;
    }
    esp_err_t r = nvs_get_blob(h, "seed", seed, &seed_len);
    if (r == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(seed, sizeof(seed));   // hardware RNG
        if (nvs_set_blob(h, "seed", seed, sizeof(seed)) != ESP_OK || nvs_commit(h) != ESP_OK) {
            ESP_LOGE(TAG, "identity: generated a seed but could not store it (not persisted!)");
        } else {
            ESP_LOGI(TAG, "identity: created a new node key (stored as a seed in mcid/seed)");
        }
    } else if (r != ESP_OK || seed_len != sizeof(seed)) {
        ESP_LOGE(TAG, "identity: stored seed unreadable (%s, %u B)", esp_err_to_name(r),
                 (unsigned)seed_len);
        nvs_close(h);
        return;
    }
    nvs_close(h);

    if (!meshcore::identityFromSeed(g_id, seed)) {
        ESP_LOGE(TAG, "identity: seed -> keypair failed");
        return;
    }
    g_has_id = true;
    char full[2 * meshcore::kPubKeyBytes + 1];
    for (size_t i = 0; i < 8; i++) snprintf(&g_pub_hex[2 * i], 3, "%02x", g_id.pub[i]);
    for (size_t i = 0; i < meshcore::kPubKeyBytes; i++) snprintf(&full[2 * i], 3, "%02x", g_id.pub[i]);
    ESP_LOGI(TAG, "identity: %s  public %s", g_pub_hex, full);
}

void loadChannels() {
    g_set.count = 0;
    for (size_t i = 0; i < meshcore::kMaxChannels; ++i) {
        g_scope_loaded[i] = false;   // S513/EST-440: a reload re-reads by the new live hashes
        g_scope[i][0]     = 0;
    }
    nvs_handle_t h;
    if (nvs_open("mcch", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "channels: no \"mcch\" namespace yet (nothing provisioned)");
        return;
    }
    uint8_t cnt = 0;
    if (nvs_get_u8(h, "cnt", &cnt) != ESP_OK) {
        ESP_LOGW(TAG, "channels: no count stored, table empty");
        nvs_close(h);
        return;
    }
    if (cnt > meshcore::kMaxChannels) {
        ESP_LOGW(TAG, "channels: stored count %u exceeds %u, using %u", cnt,
                 (unsigned)meshcore::kMaxChannels, (unsigned)meshcore::kMaxChannels);
        cnt = (uint8_t)meshcore::kMaxChannels;
    }
    for (uint8_t i = 0; i < cnt; i++) {
        char     key_name[8];
        uint8_t  key[meshcore::kKeyBytes];
        size_t   key_len = sizeof(key);
        char     name[32] = {0};
        size_t   name_len = sizeof(name) - 1;
        snprintf(key_name, sizeof(key_name), "k%u", (unsigned)i);
        if (nvs_get_blob(h, key_name, key, &key_len) != ESP_OK || key_len != sizeof(key)) {
            ESP_LOGW(TAG, "channels: slot %u has no usable key, skipped", (unsigned)i);
            continue;
        }
        snprintf(key_name, sizeof(key_name), "n%u", (unsigned)i);
        if (nvs_get_str(h, key_name, name, &name_len) != ESP_OK) name[0] = '\0';
        if (!meshcore::channelsAdd(g_set, key, name[0] ? name : "channel")) {
            ESP_LOGW(TAG, "channels: slot %u could not be added (duplicate key or table full)", (unsigned)i);
        }
    }
    nvs_close(h);
}

// S514/EST-446: perform a channel rewrite plan against NVS "mcch". EVERY step is committed on its own, so
// a power cut between any two steps is exactly the cut the host test simulates (core/channel_admin.cpp's
// order is what makes every such cut loadable). The radio task's table is NOT touched here: the caller
// raises g_channels_reload and the radio task (the one owner of g_set) reloads on its next loop.
bool channelApplyPlan(const meshcore::ChannelSet& after, const meshcore::ChannelNvsStep* plan, int steps) {
    if (plan == nullptr || steps <= 0) return false;
    nvs_handle_t h;
    if (nvs_open("mcch", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t r = ESP_OK;
    for (int i = 0; i < steps && r == ESP_OK; ++i) {
        const meshcore::ChannelNvsStep& st = plan[i];
        char key[8];
        switch (st.op) {
            case meshcore::ChannelNvsOp::SetKey:
                snprintf(key, sizeof(key), "k%u", (unsigned)st.index);
                r = nvs_set_blob(h, key, after.ch[st.index].secret, meshcore::kKeyBytes);
                break;
            case meshcore::ChannelNvsOp::SetName:
                snprintf(key, sizeof(key), "n%u", (unsigned)st.index);
                r = nvs_set_str(h, key, after.ch[st.index].name);
                break;
            case meshcore::ChannelNvsOp::SetCount:
                r = nvs_set_u8(h, "cnt", after.count);
                break;
            case meshcore::ChannelNvsOp::EraseKey:
                snprintf(key, sizeof(key), "k%u", (unsigned)st.index);
                r = nvs_erase_key(h, key);
                if (r == ESP_ERR_NVS_NOT_FOUND) r = ESP_OK;
                break;
            case meshcore::ChannelNvsOp::EraseName:
                snprintf(key, sizeof(key), "n%u", (unsigned)st.index);
                r = nvs_erase_key(h, key);
                if (r == ESP_ERR_NVS_NOT_FOUND) r = ESP_OK;
                break;
            case meshcore::ChannelNvsOp::EraseScope:
                snprintf(key, sizeof(key), "s%02x", (unsigned)st.hash);
                r = nvs_erase_key(h, key);
                if (r == ESP_ERR_NVS_NOT_FOUND) r = ESP_OK;   // nothing stored IS Default
                break;
            case meshcore::ChannelNvsOp::None:
                break;
        }
        if (r == ESP_OK) r = nvs_commit(h);
    }
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "channels: NVS rewrite failed (%s); the table will be re-read", esp_err_to_name(r));
        g_channels_reload = true;   // whatever did reach flash is now the truth; reload rather than guess
        return false;
    }
    g_channels_reload = true;
    return true;
}

// The table as it is now, minus slot `drop`, in the same order (the compaction the L2 does).
meshcore::ChannelSet setWithout(const meshcore::ChannelSet& in, int drop) {
    meshcore::ChannelSet out{};
    for (size_t i = 0; i < in.count; ++i) {
        if ((int)i == drop) continue;
        meshcore::channelsAdd(out, in.ch[i].secret, in.ch[i].name);
    }
    return out;
}

}  // namespace

// S515/EST-461: the NVS budget in ENTRIES (the nvs partition is only 16 KB, and blob v3 adds 10 bytes to every
// contact row). Logged at boot before and after the contact blob is read, and after every blob write, so a
// namespace that is filling up shows in the log instead of as a mysterious first-boot failure.
void logNvsStats(const char* when) {
    nvs_stats_t st{};
    const esp_err_t r = nvs_get_stats(nullptr, &st);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "nvs %s: stats unavailable (%s)", when, esp_err_to_name(r));
        return;
    }
    ESP_LOGI(TAG, "nvs %s: used %u, free %u, total %u entries", when, (unsigned)st.used_entries,
             (unsigned)st.free_entries, (unsigned)st.total_entries);
}

// S513/EST-437: the contact table's one NVS blob. Read at boot before the radio task exists; a wrong blob is
// ignored whole (decode writes into a scratch table), so a damaged namespace cannot half-fill the Nodes list.
size_t restoreContacts() {
    initNodeTable();   // the blob and the table must exist even if begin() has not run yet
    g_nvs_blob_version = 0;
    logNvsStats("before the contact blob (boot)");
    nvs_handle_t h;
    if (nvs_open(kContactsNvsNamespace, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "contacts: none stored yet (no \"%s\" namespace)", kContactsNvsNamespace);
        return 0;
    }
    size_t    len = 0;
    esp_err_t r   = nvs_get_blob(h, kContactsNvsKey, nullptr, &len);
    if (r == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "contacts: none stored yet");
        nvs_close(h);
        return 0;
    }
    if (r != ESP_OK || len == 0 || len > kContactBlobMaxBytes) {
        ESP_LOGW(TAG, "contacts: stored blob ignored (unreadable: %s, %u B)", esp_err_to_name(r), (unsigned)len);
        nvs_close(h);
        return 0;
    }
    size_t cap = len;
    r          = nvs_get_blob(h, kContactsNvsKey, g_contact_blob, &cap);
    nvs_close(h);
    if (r != ESP_OK || cap != len) {
        ESP_LOGW(TAG, "contacts: stored blob ignored (read %s, %u of %u B)", esp_err_to_name(r), (unsigned)cap,
                 (unsigned)len);
        return 0;
    }
    g_nvs_blob_version = g_contact_blob[0];   // the migration reads this: v1-v3 rows were all radio nodes

    // Decode straight into the live table: every check in contactsDecode is BEFORE its first write, so a wrong
    // blob leaves the (still empty) table alone, and the boot stack does not carry a second 2.7 KB ContactStore.
    const ContactDecode d = contactsDecode(g_contact_blob, len, g_contacts);
    if (d != ContactDecode::Ok) {
        const unsigned version = len > 0 ? g_contact_blob[0] : 0;
        ESP_LOGW(TAG, "contacts: stored blob ignored (%s, version %u, %u B)", contactDecodeText(d), version,
                 (unsigned)len);
        return 0;
    }
    // S513/EST-441: a stored row whose key is only four bytes and zeros is not a contact — v41 wrote exactly that
    // from a DM log's file name, and it is on Filip's device now, so it is DROPPED here rather than merely not
    // written again. The table is marked dirty so the next main-task pass rewrites NVS without it.
    const size_t dropped = contactsDropIncomplete(g_contacts);
    // Nothing has changed since this was read: the first real mutation raises a flag again.
    g_contacts_dirty_struct = dropped > 0;   // a dropped row must reach NVS, or it returns on the next boot
    g_contacts_dirty_soft   = false;
    g_contacts_dirty_ack    = false;
    g_contacts_ack_force    = false;
    g_contacts_write_failed = false;
    if (dropped > 0) {
        ESP_LOGW(TAG, "contacts: dropped %u record(s) without a full key", (unsigned)dropped);
    }
    ESP_LOGI(TAG, "contacts: %u restored from NVS", (unsigned)g_contacts.count);
    logNvsStats("after the contact blob (boot)");
    return g_contacts.count;
}

// S515/EST-464: finish a file before the rename makes it current. FATFS has no fsync(), but a 30 s floor and a
// temp file mean a power cut loses at most this one write, never the old file.
// S515 (Claude, review): FatFs f_rename REFUSES an existing target (ff.c f_rename: FR_EXIST when the new name is
// in use; vfs_fat_rename passes that straight through), unlike POSIX. So every write after the first would have
// failed and the node files would never have changed again. The old file is removed between the finished temp
// file and the rename; a power cut in that gap leaves only the temp file, and readNodeFileOrTemp() loads it.
bool writeFileAtomic(const char* path, const char* tmp, const uint8_t* data, size_t len) {
    FILE* f = fopen(tmp, "wb");
    if (f == nullptr) {
        ESP_LOGW(TAG, "node storage: cannot open %s", tmp);
        return false;
    }
    const size_t wrote = fwrite(data, 1, len, f);
    const int    flushed = fflush(f);
    const int    synced  = fsync(fileno(f));
    const int    closed  = fclose(f);
    // S516 (EST-472 item 2): a write is finished only when EVERY step said so. Before, only `wrote` was checked, so
    // a failed flush/fsync/fclose still went on to remove the old file: the old bytes were gone and the new ones
    // might never have reached the card/flash. Now the temp is removed and the old file stays.
    if (wrote != len) {
        ESP_LOGW(TAG, "node storage: short write to %s (%u of %u B)", tmp, (unsigned)wrote, (unsigned)len);
        remove(tmp);
        return false;
    }
    if (flushed != 0 || synced != 0 || closed != 0) {
        ESP_LOGW(TAG, "node storage: %s not durable (fflush %d, fsync %d, fclose %d, errno %d); old file kept",
                 tmp, flushed, synced, closed, errno);
        remove(tmp);
        return false;
    }
    struct stat st;
    if (stat(path, &st) == 0 && remove(path) != 0) {
        ESP_LOGW(TAG, "node storage: cannot remove the old %s (errno %d)", path, errno);
        remove(tmp);
        return false;
    }
    if (rename(tmp, path) != 0) {
        ESP_LOGW(TAG, "node storage: rename %s -> %s failed (errno %d)", tmp, path, errno);
        remove(tmp);
        return false;
    }
    return true;
}

// Read a whole file into the PSRAM buffer. False when it is absent (not an error) or larger than the format allows.
bool readNodeFile(const char* path, size_t* out_len) {
    *out_len = 0;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (st.st_size <= 0 || (size_t)st.st_size > kNodeFileMaxBytes) {
        ESP_LOGW(TAG, "node storage: %s has a bad size (%ld B)", path, (long)st.st_size);
        return false;
    }
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    const size_t got = fread(g_node_file_buf, 1, (size_t)st.st_size, f);
    fclose(f);
    if (got != (size_t)st.st_size) {
        ESP_LOGW(TAG, "node storage: short read from %s (%u of %ld B)", path, (unsigned)got, (long)st.st_size);
        return false;
    }
    *out_len = got;
    return true;
}

// S515 (Claude, review): the file, or, when it is absent, the temp file a power cut between writeFileAtomic()'s
// remove and its rename left behind. S516 (EST-472 item 2): the temp file is CRC-CHECKED BEFORE it is trusted —
// it is renamed into place only when every byte the header claims is there and the CRC agrees. A torn temp is
// removed instead (its only other copy, the old file, was already removed by writeFileAtomic), the caller falls
// back to the NVS store, and a corrupt radio.bin is not left behind for every later boot to reject.
bool readNodeFileOrTemp(const char* path, const char* tmp, size_t* out_len) {
    if (readNodeFile(path, out_len)) return true;
    struct stat st;
    if (stat(path, &st) == 0) return false;     // the file is there but unreadable: not a recovery case
    if (!readNodeFile(tmp, out_len)) return false;
    const NodeFileResult checked = nodeFileCheck(g_node_file_buf, *out_len);
    if (checked != NodeFileResult::Ok) {
        ESP_LOGW(TAG, "node storage: %s is not usable (%s); removed, the store falls back", tmp,
                 nodeFileResultText(checked));
        remove(tmp);
        *out_len = 0;
        return false;
    }
    ESP_LOGW(TAG, "node storage: %s missing, loading %s (an interrupted write)", path, tmp);
    if (rename(tmp, path) != 0) ESP_LOGW(TAG, "node storage: rename %s -> %s failed (errno %d)", tmp, path, errno);
    return true;
}

// S515/EST-464: /int is the launcher's locfd partition, mounted with the launcher's own config (main/main.c:
// 481-490): wear levelling, format_if_mount_failed FALSE (NEVER format it: the launcher's app list lives here),
// 10 open files, one WL sector per allocation unit, one FAT. Our folder is /int/om-lite, never /int/apps.
bool mountInt() {
    esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed   = false,
        .max_files                = 10,
        .allocation_unit_size     = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat              = false,
    };
    const esp_err_t r = esp_vfs_fat_spiflash_mount_rw_wl(kIntMountPoint, "locfd", &cfg, &g_int_wl);
    if (r != ESP_OK) {
        g_int_wl      = WL_INVALID_HANDLE;
        g_int_mounted = false;
        ESP_LOGW(TAG, "/int not mounted (%s): radio list stays on the NVS fallback (cap %u)",
                 esp_err_to_name(r), (unsigned)kNodeNvsRadioCap);
        return false;
    }
    g_int_mounted = true;
    struct stat st;
    if (mkdir(kIntRadioDir, 0777) != 0 && stat(kIntRadioDir, &st) != 0) {
        ESP_LOGW(TAG, "/int mounted but %s cannot be created", kIntRadioDir);
    }
    uint64_t total = 0, freeb = 0;
    if (esp_vfs_fat_info(kIntMountPoint, &total, &freeb) == ESP_OK) {
        ESP_LOGI(TAG, "/int mounted (free %u KB)", (unsigned)(freeb / 1024));
    } else {
        ESP_LOGI(TAG, "/int mounted (free unknown)");
    }
    return true;
}

void unmountInt() {
    if (!g_int_mounted) return;
    esp_vfs_fat_spiflash_unmount_rw_wl(kIntMountPoint, g_int_wl);
    g_int_wl      = WL_INVALID_HANDLE;
    g_int_mounted = false;
    ESP_LOGI(TAG, "/int unmounted for the launcher");
}

// Boot: load radio.bin (or the NVS fallback + one-time migration), then merge the SD archive. Returns the
// number of rows after the merge. Called before the radio task and before restoreDmHistory().
size_t nodeStorageBegin(bool sd_mounted) {
    initNodeTable();
    g_sd_ready = sd_mounted;
    mountInt();

    bool radio_loaded = false;
    if (g_int_mounted) {
        size_t len = 0;
        if (readNodeFileOrTemp(kRadioFilePath, kRadioTempPath, &len)) {
            ContactStore radio;   // its rows live in PSRAM; only this small object is on the stack
            const NodeFileResult r = nodeFileDecode(g_node_file_buf, len, radio);
            if (r == NodeFileResult::Ok) {
                g_contacts = radio;
                const size_t marked = markAllInRadio(g_contacts);
                // The bytes we just read are the bytes the writer would produce, so the first archive-only advert
                // does not cause a needless rewrite of internal flash.
                g_radio_signature      = nodeFileSignature(g_node_file_buf, len);
                g_radio_have_signature = true;
                ESP_LOGI(TAG, "radio: %u nodes from %s", (unsigned)marked, kRadioFilePath);
                radio_loaded = true;
            } else {
                ESP_LOGW(TAG, "radio: %s ignored (%s); trying the NVS fallback", kRadioFilePath,
                         nodeFileResultText(r));
            }
        }
    }
    if (!radio_loaded) {
        restoreContacts();   // the fallback (or the migration source); logs its own lines and the NVS stats
        if (g_int_mounted) {
            // One-time migration: no radio.bin yet, so the NVS rows become the radio list. The NVS blob itself is
            // left UNTOUCHED (a fallback; never erased), exactly as EST-463 asks.
            const size_t n = migrateNvsToRadio(g_contacts, g_nvs_blob_version);
            // Write the file even when it is empty: a fresh device then has a valid radio.bin, and a corrupt one
            // is REPLACED instead of being retried and rejected on every boot. The NVS blob is not touched.
            const size_t bytes = nodeFileEncode(g_contacts, true, kMaxRadio, g_node_file_buf, kNodeFileMaxBytes);
            if (bytes > 0 && writeFileAtomic(kRadioFilePath, kRadioTempPath, g_node_file_buf, bytes)) {
                g_radio_signature      = nodeFileSignature(g_node_file_buf, bytes);
                g_radio_have_signature = true;
                if (n > 0) ESP_LOGI(TAG, "migrated %u nodes from NVS", (unsigned)n);
                else       ESP_LOGI(TAG, "radio: 0 nodes from %s (empty file written)", kRadioFilePath);
            } else {
                ESP_LOGW(TAG, "migration: could not write %s", kRadioFilePath);
            }
        } else {
            // No /int: the NVS blob is the store, so the rows it holds keep their radio mark. A v1-v3 blob had no
            // mark and every row in it WAS a radio node.
            migrateNvsToRadio(g_contacts, g_nvs_blob_version);
        }
    }

    if (g_sd_ready) {
        size_t len = 0;
        if (readNodeFileOrTemp(kArchiveFilePath, kArchiveTempPath, &len)) {
            ContactStore archive;
            const NodeFileResult r = nodeFileDecode(g_node_file_buf, len, archive);
            if (r == NodeFileResult::Ok) {
                const MergeResult m = mergeArchive(g_contacts, archive);
                ESP_LOGI(TAG, "archive: %u nodes from %s", (unsigned)m.archive_rows, kArchiveFilePath);
                if (m.added + m.updated != m.archive_rows || m.dropped > 0) {
                    ESP_LOGW(TAG, "archive merge: %u added, %u updated, %u dropped",
                             (unsigned)m.added, (unsigned)m.updated, (unsigned)m.dropped);
                }
            } else {
                ESP_LOGW(TAG, "archive: %s ignored (%s)", kArchiveFilePath, nodeFileResultText(r));
            }
        } else {
            ESP_LOGI(TAG, "archive: 0 nodes from %s (no file yet)", kArchiveFilePath);
        }
    } else {
        ESP_LOGW(TAG, "archive: no SD card; nodes heard this boot stay in RAM only");
    }
    ESP_LOGI(TAG, "node table: %u rows in PSRAM (%u B), %u in the radio",
             (unsigned)g_contacts.count, (unsigned)contactsTableBytes(), (unsigned)radioCount(g_contacts));
    // S516/EST-472 item 8: internal RAM after the two message rings (g_messages 18,700 B + g_dms 20,748 B, `nm`
    // on the built ELF) and the PSRAM allocations above. One line, at boot, with the number the Settings ›
    // General heap row shows.
    ESP_LOGI(TAG, "heap: %u B internal free, largest internal block %u B, PSRAM free %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (radio_loaded) logNvsStats("after the node files (boot)");   // restoreContacts() logged its own pair
    return g_contacts.count;
}

// S516/EST-472 item 8: evidence, not a guess. The MAIN task now writes radio.bin/nodes.bin and the radio task
// writes dm-*.log; this is logged ONCE PER TASK, after that task's first FatFs write, so the low-water mark
// includes the FatFs call rather than only the boot path. Two writers exist, so two lines are the whole budget.
void logFatfsWriterStackOnce() {
    static TaskHandle_t seen[2] = {nullptr, nullptr};
    const TaskHandle_t  me      = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < 2; ++i)
        if (seen[i] == me) return;
    int slot = (seen[0] == nullptr) ? 0 : (seen[1] == nullptr ? 1 : -1);
    if (slot < 0) return;
    seen[slot] = me;
    ESP_LOGI(TAG, "stack: %u B free at the low-water mark (this task, after a FatFs write)",
             (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
}

// S513/EST-437 + S515/EST-464: the MAIN task's writer. The radio task only raises a flag. The radio list goes to
// /int/om-lite/radio.bin (or, with no /int, to the NVS blob capped at 32); the archive goes to the SD card at most
// once per 30 s (+ on leaving Nodes and on Esc). Both encode under the store lock, then write outside it.
void contactsPersistTick() {
    if (g_store_mutex == nullptr) return;   // begin() has not run yet
    const uint32_t now = monotonicMs();

    bool   structural = false, soft = false, ack = false, force = false;
    size_t radio_bytes = 0, radio_rows = 0;
    uint32_t radio_sig = 0;
    {
        StoreLock l;
        structural = g_contacts_dirty_struct;
        soft       = g_contacts_dirty_soft;
        ack        = g_contacts_dirty_ack;
        force      = g_contacts_ack_force;
        if (structural || soft || ack) {
            // A soft-only change waits for the 10-minute floor; a structural one goes on this pass; an ack waits
            // 5 s (or at once when the page was left). The failure retry is its own 10 s clock.
            bool due = structural || force;
            if (!due) {
                uint32_t floor_ms = 0;
                if (soft) floor_ms = kContactsSoftWriteMs;
                if (ack && (floor_ms == 0 || kContactsAckWriteMs < floor_ms)) floor_ms = kContactsAckWriteMs;
                due = floor_ms == 0 || g_contacts_last_write_ms == 0 ||
                      (uint32_t)(now - g_contacts_last_write_ms) >= floor_ms;
            }
            if (due && !(g_contacts_write_failed && g_contacts_last_attempt_ms != 0 &&
                         (uint32_t)(now - g_contacts_last_attempt_ms) < kContactsRetryMs)) {
                radio_rows = radioCount(g_contacts);
                if (g_int_mounted) {
                    radio_bytes = nodeFileEncode(g_contacts, true, kMaxRadio, g_node_file_buf, kNodeFileMaxBytes);
                    if (radio_bytes > 0) radio_sig = nodeFileSignature(g_node_file_buf, radio_bytes);   // not the whole-file CRC (S515)
                } else {
                    radio_bytes = contactsEncodeRadio(g_contacts, g_contact_blob, kContactBlobMaxBytes,
                                                      kNodeNvsRadioCap);
                    if (radio_bytes > 0) radio_sig = nodeFileCrc32(g_contact_blob, radio_bytes);
                    if (radio_rows > kNodeNvsRadioCap) {
                        ESP_LOGW(TAG, "radio list over the NVS fallback cap (%u radio, %u stored)",
                                 (unsigned)radio_rows, (unsigned)kNodeNvsRadioCap);
                    }
                }
                // Cleared BEFORE the lock is released: a mutation that lands during the write sets a flag again.
                g_contacts_dirty_struct = false;
                g_contacts_dirty_soft   = false;
                g_contacts_dirty_ack    = false;
                g_contacts_ack_force    = false;
            }
        }
    }

    if (radio_bytes > 0 && (!g_radio_have_signature || radio_sig != g_radio_signature)) {
        bool ok = false;
        if (g_int_mounted) {
            ok = writeFileAtomic(kRadioFilePath, kRadioTempPath, g_node_file_buf, radio_bytes);
            if (ok) {
                ESP_LOGI(TAG, "radio: %u written to %s (%u B)", (unsigned)radio_rows, kRadioFilePath,
                         (unsigned)radio_bytes);
                logFatfsWriterStackOnce();
            }
        } else {
            nvs_handle_t h;
            esp_err_t    r = nvs_open(kContactsNvsNamespace, NVS_READWRITE, &h);
            if (r == ESP_OK) {
                r = nvs_set_blob(h, kContactsNvsKey, g_contact_blob, radio_bytes);
                if (r == ESP_OK) r = nvs_commit(h);
                nvs_close(h);
            }
            ok = r == ESP_OK;
            if (ok) ESP_LOGI(TAG, "radio: %u stored to the NVS fallback (%u B)", (unsigned)radio_rows,
                             (unsigned)radio_bytes);
        }
        g_contacts_last_attempt_ms = now;
        if (ok) {
            g_contacts_last_write_ms = now;
            g_contacts_write_failed  = false;
            g_radio_signature        = radio_sig;
            g_radio_have_signature   = true;
            if (!g_int_mounted) logNvsStats("after the contact blob (write)");
        } else {
            g_contacts_write_failed = true;
            ESP_LOGE(TAG, "radio: write failed; retrying in %u s", (unsigned)(kContactsRetryMs / 1000));
            StoreLock l;
            if (structural) g_contacts_dirty_struct = true;
            else g_contacts_dirty_soft = true;
            if (ack) g_contacts_dirty_ack = true;
        }
    }

    // ---- the SD archive: all nodes, at most once per 30 s, plus leaving Nodes / Esc ---------------------------
    if (!g_sd_ready) return;
    bool archive_dirty = false, archive_force = false;
    {
        StoreLock l;
        archive_dirty = g_archive_dirty;
        archive_force = g_archive_force;
        g_archive_force = false;
    }
    if (!archive_dirty && !archive_force) return;
    if (!archive_force && g_archive_last_write_ms != 0 &&
        (uint32_t)(now - g_archive_last_write_ms) < kArchiveWriteMs) {
        g_archive_force = archive_force;
        return;
    }
    size_t archive_bytes = 0, archive_rows = 0;
    {
        StoreLock l;
        archive_rows  = g_contacts.count;
        archive_bytes = nodeFileEncode(g_contacts, false, 0, g_node_file_buf, kNodeFileMaxBytes);
        if (archive_bytes > 0) g_archive_dirty = false;
    }
    if (archive_bytes == 0) {
        ESP_LOGE(TAG, "archive: encode failed (%u rows), not written", (unsigned)archive_rows);
        return;
    }
    // S515 (Claude, review): this runs on the MAIN task, so the time it takes is a UI pause; it is logged so the
    // device shows it. A failure (e.g. the card pulled) also starts the 30 s floor: before, a failed write was
    // retried on every loop pass (30-250 ms), each one re-encoding the whole table under the store lock.
    const int64_t t0 = esp_timer_get_time();
    const bool    ok = writeFileAtomic(kArchiveFilePath, kArchiveTempPath, g_node_file_buf, archive_bytes);
    const unsigned ms = (unsigned)((esp_timer_get_time() - t0) / 1000);
    g_archive_last_write_ms = now;
    if (ok) {
        g_archive_write_failed  = false;
        ESP_LOGI(TAG, "archive: %u written to %s (%u B, %u ms)", (unsigned)archive_rows, kArchiveFilePath,
                 (unsigned)archive_bytes, ms);
        logFatfsWriterStackOnce();
    } else {
        g_archive_write_failed = true;
        StoreLock l;
        g_archive_dirty = true;
        ESP_LOGE(TAG, "archive: write failed after %u ms; next try in %u s", ms, (unsigned)(kArchiveWriteMs / 1000));
    }
}

// S515/EST-464: Esc to the launcher. Flush both stores past their floors, then unmount /int so the launcher can
// mount it again. Called before the "Back to the menu" frame, while the app still owns the partition.
// S516 (EST-472 item 3): the RADIO file is forced too. Before, only the archive and the ack were forced, so a
// structural change that had not reached the main loop's writer (or whose last attempt failed) was simply lost on
// exit. `structural` makes this pass encode the radio list now; the signature check means an unchanged list is
// still NOT rewritten (no flash wear), and a failed attempt is retried once here instead of never.
void nodeStorageShutdown() {
    {
        StoreLock l;
        g_contacts_dirty_struct = true;
        g_contacts_ack_force    = true;
        g_archive_force         = true;
        g_archive_dirty         = true;
        // The retry floor must not swallow the LAST write of the session.
        g_contacts_last_attempt_ms = 0;
    }
    contactsPersistTick();
    unmountInt();
}

// S514/EST-447: the NODES section's remove/clear. The table change is under the store lock; the NVS rewrite is
// NOT done here — raising the structural flag makes the main loop's contactsPersistTick() write the SAME blob
// without a second writer, and a power cut between the RAM change and that write simply leaves the old table
// until the next advert, which is what the confirm text promises.
bool contactsRemove(size_t index) {
    if (g_store_mutex == nullptr) return false;
    StoreLock l;
    if (!contactRemoveAt(g_contacts, index)) return false;
    markContactsDirty(true);
    ESP_LOGI(TAG, "contacts: removed row %u (%u left); its DM history stays on the card", (unsigned)index,
             (unsigned)g_contacts.count);
    return true;
}

size_t contactsClear() {
    if (g_store_mutex == nullptr) return 0;
    StoreLock l;
    const size_t n = g_contacts.count;
    if (n == 0) return 0;
    contactsClearAll(g_contacts);
    markContactsDirty(true);
    ESP_LOGI(TAG, "contacts: cleared all %u; their DM history stays on the card", (unsigned)n);
    return n;
}

// S514/EST-455: the favourite flag is part of the contact record, so it is a STRUCTURAL change and follows the
// same one-writer rule as remove/clear: change RAM under the lock, raise the flag, let contactsPersistTick()
// rewrite the `mcct` blob on this loop's pass.
bool contactsSetFavourite(size_t index, bool on) {
    if (g_store_mutex == nullptr) return false;
    StoreLock l;
    if (!contactSetFavourite(g_contacts, index, on)) return false;
    markContactsDirty(true);
    ESP_LOGI(TAG, "contacts: row %u favourite -> %s", (unsigned)index, on ? "on" : "off");
    return true;
}

// S515/EST-461: clear the NEW flag on one row. The dot goes at once (the caller repaints); the WRITE is
// coalesced by the ack floor, so holding Down through twenty new nodes is still one NVS write.
bool contactsAckFresh(size_t index) {
    if (g_store_mutex == nullptr) return false;
    StoreLock l;
    if (index >= g_contacts.count || !g_contacts.c[index].fresh) return false;
    g_contacts.c[index].fresh = false;
    markContactsAck(false);
    ESP_LOGI(TAG, "contacts: row %u acked (new dot gone, %u still new)", (unsigned)index,
             (unsigned)contactsFreshCount(g_contacts));
    return true;
}

// Leaving the Nodes page: the one write that must not wait (the ticket's "plus one on leaving the page").
void contactsAckLeave() {
    if (g_store_mutex == nullptr) return;
    StoreLock l;
    // S515/EST-464: leaving Nodes also flushes the SD archive past its 30 s floor, but only when something
    // changed — an untouched list must not cost a card write every time the page is left.
    if (g_archive_dirty) g_archive_force = true;
    if (!g_contacts_dirty_ack) return;   // nothing acked: no radio write, no flash wear
    markContactsAck(true);
}

// ------------------------------------------- S515/EST-465: the store UI's actions (Nodes menu, Settings)
// Each one runs core/node_table.h's rule under the lock, then raises the structural dirty flag the one writer
// already owns. radioCount() is O(n) and n is 2,000: fine for a key press or a redraw.
om::mc::HeardToRadio heardToRadioSetting() { return g_heard_to_radio; }

bool setHeardToRadioSetting(om::mc::HeardToRadio v) {
    nvs_handle_t h;
    if (nvs_open("mcset", NVS_READWRITE, &h) != ESP_OK) return false;
    const esp_err_t r = nvs_set_u8(h, "heardto", (uint8_t)v);
    if (r == ESP_OK) nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "add heard nodes to the radio: NVS write failed (%s); the old value stays",
                 esp_err_to_name(r));
        return false;
    }
    g_heard_to_radio = v;
    ESP_LOGI(TAG, "add heard nodes to the radio: %s (NVS mcset/heardto)", heardToRadioText(g_heard_to_radio));
    return true;
}

// S515/EST-468: `sd` is EVERY node saved on the SD card — the radio's rows are in the same file, one row per
// node, so this is the whole table (the head shows "SD -" when there is no card; the UI owns that word).
void nodeStoreCounts(size_t* radio, size_t* sd) {
    if (radio != nullptr) *radio = 0;
    if (sd != nullptr) *sd = 0;
    if (g_store_mutex == nullptr) return;
    StoreLock l;
    if (radio != nullptr) *radio = radioCount(g_contacts);
    if (sd != nullptr) *sd = g_contacts.count;
}

// S516/EST-472 item 6: the cap of the store in use. /int mounted -> radio.bin -> kMaxRadio; no /int -> the old
// NVS blob -> kMaxRadioNvs. Read by the rule (addToRadio / advertInArchive), the "Radio full" sentence and both
// headers. `g_int_mounted` is written once at boot (mountInt) and once at exit (unmountInt), never per packet.
size_t radioCapacity() { return radioCapFor(g_int_mounted); }

// S515 (Claude, review): the four user actions below FORCE the archive write. A structural change already writes
// radio.bin on the next pass, but the SD archive waited its 30 s floor, so a power-off inside that window brought a
// deleted node back from the card on the next boot (the merge adds archive rows). They are rare and deliberate.
om::mc::RadioResult nodeAddToRadio(const uint8_t pub[32]) {
    if (g_store_mutex == nullptr) return om::mc::RadioResult::NotFound;
    StoreLock l;
    const size_t           cap = radioCapacity();
    const om::mc::RadioResult r = addToRadio(g_contacts, pub, cap);
    if (r == om::mc::RadioResult::Ok) {
        markContactsDirty(true);
        g_archive_force = true;   // a user action: the SD copy follows at once (Claude, S515 review)
        ESP_LOGI(TAG, "nodes: added to the radio (%u / %u)", (unsigned)radioCount(g_contacts), (unsigned)cap);
    } else if (r == om::mc::RadioResult::RadioFull) {
        ESP_LOGW(TAG, "nodes: radio full (%u / %u); nothing changed", (unsigned)radioCount(g_contacts),
                 (unsigned)cap);
    }
    return r;
}

om::mc::RadioResult nodeRemoveFromRadio(const uint8_t pub[32]) {
    if (g_store_mutex == nullptr) return om::mc::RadioResult::NotFound;
    StoreLock l;
    const om::mc::RadioResult r = removeFromRadio(g_contacts, pub);
    if (r == om::mc::RadioResult::Ok) {
        markContactsDirty(true);
        g_archive_force = true;   // a user action: the SD copy follows at once (Claude, S515 review)
        ESP_LOGI(TAG, "nodes: moved to the archive (%u left in the radio)", (unsigned)radioCount(g_contacts));
    }
    return r;
}

bool nodeDeleteEverywhere(const uint8_t pub[32]) {
    if (g_store_mutex == nullptr) return false;
    StoreLock l;
    if (!deleteEverywhere(g_contacts, pub)) return false;
    markContactsDirty(true);
    g_archive_force = true;   // a user action: the SD copy follows at once (Claude, S515 review)
    ESP_LOGI(TAG, "nodes: deleted from both stores (%u left); its DM history stays on the card",
             (unsigned)g_contacts.count);
    return true;
}

size_t nodeClearRadio() {
    if (g_store_mutex == nullptr) return 0;
    StoreLock l;
    size_t n = 0;
    for (size_t i = 0; i < g_contacts.count; i++) {
        if (!g_contacts.c[i].in_radio) continue;
        g_contacts.c[i].in_radio = false;
        n++;
    }
    if (n == 0) return 0;
    markContactsDirty(true);
    g_archive_force = true;   // a user action: the SD copy follows at once (Claude, S515 review)
    ESP_LOGI(TAG, "nodes: radio cleared (%u moved to the archive; the archive keeps them)", (unsigned)n);
    return n;
}

bool newNodeNoticeEnabled() { return g_new_node_notice; }

bool setNewNodeNoticeEnabled(bool on) {
    nvs_handle_t h;
    if (nvs_open("mcset", NVS_READWRITE, &h) != ESP_OK) return false;
    const esp_err_t r = nvs_set_u8(h, "newnode", on ? 1 : 0);
    if (r == ESP_OK) nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "new-node notice: NVS write failed (%s); the old value stays", esp_err_to_name(r));
        return false;
    }
    g_new_node_notice = on;
    ESP_LOGI(TAG, "new-node notice: -> %s (NVS mcset/newnode)", on ? "on" : "off");
    return true;
}

// EST-445: the node's name from NVS mcid/name. A missing or invalid value keeps the app's default; a
// rejected value is never silently truncated into a different name.
void loadNodeName() {
    snprintf(g_node_name, sizeof(g_node_name), "%s", tm::kDefaultNodeName);
    nvs_handle_t h;
    if (nvs_open("mcid", NVS_READONLY, &h) != ESP_OK) return;
    char   tmp[32] = {0};
    size_t len     = sizeof(tmp);
    const esp_err_t r = nvs_get_str(h, "name", tmp, &len);
    nvs_close(h);
    if (r != ESP_OK) return;
    if (!tm::nodeNameValid(tmp)) {
        ESP_LOGW(TAG, "node name: stored value is not a valid MeshCore name; using \"%s\"", g_node_name);
        return;
    }
    snprintf(g_node_name, sizeof(g_node_name), "%s", tmp);
    ESP_LOGI(TAG, "node name: \"%s\" (NVS mcid/name)", g_node_name);
}

// EST-445: the automatic advert interval from NVS mcid/advint (seconds, 0 = Off). Any value that is not
// one of the five choices falls back to 3 h; a corrupt NVS must not silence the node.
void loadAdvertInterval() {
    g_advint_s = (uint32_t)tm::advertIntervalSeconds(tm::advertIntervalDefault());
    nvs_handle_t h;
    if (nvs_open("mcid", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "adverts: interval default %u s", (unsigned)g_advint_s);
        return;
    }
    uint32_t v = 0;
    const esp_err_t r = nvs_get_u32(h, "advint", &v);
    nvs_close(h);
    if (r == ESP_OK && tm::advertIntervalStoredValid(v)) g_advint_s = v;
    ESP_LOGI(TAG, "adverts: automatic %u s (%s)", (unsigned)g_advint_s,
             g_advint_s == 0 ? "off" : "scheduled");
}

// S514/EST-447: the new-node notice from NVS "mcset"/"newnode" (1 = On, 0 = Off). Absent or corrupt reads as
// On: core/contact_admin.h owns that rule, so a damaged byte cannot silently hide new nodes.
void loadNewNodeNotice() {
    g_new_node_notice = true;
    nvs_handle_t h;
    if (nvs_open("mcset", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 1;
    const esp_err_t r = nvs_get_u8(h, "newnode", &v);
    nvs_close(h);
    g_new_node_notice = newNodeNoticeFromStored(r == ESP_OK, v);
    ESP_LOGI(TAG, "new-node notice: %s (NVS mcset/newnode)", g_new_node_notice ? "on" : "off");
}

// S515/EST-464 (EST-463 item 2): "Add heard nodes to the radio" — Off / Clients / All, default Off, stored beside
// the other node settings in NVS mcset/heardto. The Settings ROW is the next ticket; this is only the value.
void loadHeardToRadio() {
    g_heard_to_radio = kHeardToRadioDefault;
    nvs_handle_t h;
    if (nvs_open("mcset", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 0;
    const esp_err_t r = nvs_get_u8(h, "heardto", &v);
    nvs_close(h);
    if (r == ESP_OK) g_heard_to_radio = heardToRadioFromByte(v);
    ESP_LOGI(TAG, "add heard nodes to the radio: %s (NVS mcset/heardto)", heardToRadioText(g_heard_to_radio));
}

void begin() {
    initNodeTable();   // S515/EST-463: PSRAM first, before anything can read or restore the table
    if (g_store_mutex == nullptr) g_store_mutex = xSemaphoreCreateRecursiveMutex();
    if (g_tx_lock == nullptr) g_tx_lock = xSemaphoreCreateMutex();   // S513: the send queue, created once
    if (g_dm_lock == nullptr) g_dm_lock = xSemaphoreCreateMutex();   // S513/EST-439: the pending DM table
    // S513/EST-437: the last-heard/SNR floor starts at boot, so the first soft change after a restart waits its
    // 10 minutes like every other one. Add/rename/route changes are not affected: those write on the next pass.
    g_contacts_last_write_ms = monotonicMs();
    loadOrCreateIdentity();
    loadNodeName();          // EST-445: mcid/name, default "TNMT (SI)"
    loadAdvertInterval();    // EST-445: mcid/advint; default Off since S514 (Filip)
    loadNewNodeNotice();     // S514/EST-447: mcset/newnode, default On
    loadHeardToRadio();      // S515/EST-464: mcset/heardto, default Off (Filip's rule)
    loadChannels();
    g_loaded = true;
    ESP_LOGI(TAG, "channels: %u loaded", (unsigned)g_set.count);
    for (size_t i = 0; i < g_set.count; i++) {
        ESP_LOGI(TAG, "  ch%u \"%s\" hash 0x%02X", (unsigned)i, g_set.ch[i].name, g_set.ch[i].hash);
    }
    // Step 5: what was said before, from the card, in the L2's format. No lock is needed here: this runs on the
    // main task before the radio task exists.
    //
    // S503 — unread is now restored instead of assumed. The log's Meta record carries the seen point (the log id
    // of the newest message the user had read), so a message that arrived while the channel was closed comes back
    // as unread after a restart, and one that was read stays read. A log written before this slice has no Meta
    // record; for those the old rule still applies (everything loaded counts as read) so an upgrade cannot
    // suddenly show 40 unread from the last two days.
    static Message  loaded[kMaxMessages];
    static uint32_t log_ids[kMaxMessages];
    static uint32_t ring_ids[kMaxMessages];
    for (size_t i = 0; i < g_set.count; i++) {
        uint32_t max_log_id = 0;
        const size_t n = sdhistory::load((uint8_t)i, g_set.ch[i].hash, g_set.ch[i].name, loaded, kMaxMessages,
                                         &max_log_id);
        for (size_t k = 0; k < n; k++) {
            log_ids[k]  = loaded[k].log_id;
            ring_ids[k] = messagesPush(g_messages, loaded[k]);
        }
        g_next_log_id[i] = max_log_id + 1;   // the first record of a fresh file gets id 1
        if (n == 0) continue;

        const SeenPoint sp = sdhistory::loadSeen((uint8_t)i, g_set.ch[i].hash, g_set.ch[i].name);
        if (sp.found) {
            const int read = historyReadCount(log_ids, (int)n, sp.seen_log_id);
            g_read.seen[i]     = read > 0 ? ring_ids[read - 1] : 0;
            g_seen_written[i]  = sp.seen_log_id;
            ESP_LOGI(TAG, "ch%u: %d of %u read (seen point %u), %u unread", (unsigned)i, read, (unsigned)n,
                     (unsigned)sp.seen_log_id, (unsigned)(n - (size_t)read));
        } else {
            markChannelRead(g_messages, g_read, (uint8_t)i);
            ESP_LOGI(TAG, "ch%u: %u message(s), no seen point yet (a log from before S503): all counted as read",
                     (unsigned)i, (unsigned)n);
        }
    }
    if (g_set.count == 0) {
        ESP_LOGW(TAG, "channels: none stored — provision over BadgeLink (nvs write mcch cnt u8 N, "
                      "n<i> str NAME, k<i> blob <16-byte hex key>)");
    }
}

size_t channelCount() { return g_set.count; }

const char* channelName(size_t i) { return (i < g_set.count) ? g_set.ch[i].name : ""; }

// S513/EST-440: one read from NVS "mcch", key "s<hh>" of the LIVE hash. Missing is the Default state
// and true; an unreadable/over-long value is also Default (never truncated into a shorter region, which
// could silently be a DIFFERENT region), and only a real read error returns false.
static bool scopeReadNvs(uint8_t hash, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return false;
    out[0] = 0;
    nvs_handle_t h;
    if (nvs_open("mcch", NVS_READONLY, &h) != ESP_OK) return false;
    char key[8];
    snprintf(key, sizeof(key), "s%02x", hash);
    size_t    len = 0;
    esp_err_t r   = nvs_get_str(h, key, nullptr, &len);
    if (r == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return true;   // Default: a missing key is the state an unset channel is in
    }
    if (r == ESP_OK && len <= meshcore::kScopeNameMax + 1) {
        char   raw[meshcore::kScopeNameMax + 1];
        size_t got = sizeof(raw);
        r          = nvs_get_str(h, key, raw, &got);
        if (r == ESP_OK) snprintf(out, cap, "%s", raw);
    } else if (r == ESP_OK) {
        // Corrupt/over-long: Default, never truncated into a shorter (possibly different) region.
        ESP_LOGW(TAG, "scope key %s is %u B, ignored as Default", key, (unsigned)len);
    }
    nvs_close(h);
    return r == ESP_OK;
}

bool channelScopeStored(size_t i, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return false;
    out[0] = 0;
    if (i >= g_set.count || i >= meshcore::kMaxChannels) return false;
    if (!g_scope_loaded[i]) {
        if (!scopeReadNvs(g_set.ch[i].hash, g_scope[i], sizeof(g_scope[i]))) {
            g_scope[i][0] = 0;   // never cache a guess; Default until a read succeeds
            return false;
        }
        g_scope_loaded[i] = true;
        if (g_scope[i][0] != 0)
            ESP_LOGI(TAG, "ch%u scope stored: %s (key s%02x)", (unsigned)i, g_scope[i], g_set.ch[i].hash);
    }
    snprintf(out, cap, "%s", g_scope[i]);
    return true;
}

bool channelScopeSet(size_t i, const char* setting) {
    if (i >= g_set.count || i >= meshcore::kMaxChannels) return false;
    const char* text = setting != nullptr ? setting : "";
    if (strlen(text) > meshcore::kScopeNameMax) return false;   // one rule: the L2's 30-character cap

    char key[8];
    snprintf(key, sizeof(key), "s%02x", g_set.ch[i].hash);
    nvs_handle_t h;
    if (nvs_open("mcch", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t r;
    if (text[0] == 0) {
        // S509's second defect: NVS answers NOT_FOUND for a key that never existed, and "nothing
        // stored" IS Default. That is success, not a failed erase.
        size_t len = 0;
        r          = nvs_get_str(h, key, nullptr, &len);
        if (r == ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(h);
            g_scope[i][0]     = 0;
            g_scope_loaded[i] = true;
            return true;
        }
        if (r == ESP_OK) {
            r = nvs_erase_key(h, key);
            if (r == ESP_OK) r = nvs_commit(h);
            else if (r == ESP_ERR_NVS_NOT_FOUND) r = ESP_OK;
        }
    } else {
        r = nvs_set_str(h, key, text);
        if (r == ESP_OK) r = nvs_commit(h);
    }
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "ch%u scope NOT %s (%s)", (unsigned)i, text[0] == 0 ? "cleared" : "saved",
                 esp_err_to_name(r));
        return false;
    }
    snprintf(g_scope[i], sizeof(g_scope[i]), "%s", text);
    g_scope_loaded[i] = true;
    ESP_LOGI(TAG, "ch%u scope %s (key s%02x)", (unsigned)i, text[0] == 0 ? "cleared" : "saved",
             g_set.ch[i].hash);
    return true;
}

void channelScopeHead(size_t i, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = 0;
    char stored[meshcore::kScopeNameMax + 1] = "";
    if (!channelScopeStored(i, stored, sizeof(stored))) return;
    const meshcore::ScopePlan plan = meshcore::scopePlanFor(stored, nullptr);   // no node default today
    meshcore::scopeHeadText(plan, out, (int)cap);
}


// One place that both writes a message to the store and appends it to its channel's log, so an incoming and an
// outgoing message can never be numbered by two different rules. The id goes up only when the record really
// reached the card: a message that could not be written keeps log_id 0, which is exactly what tells the seen
// point (and the next boot's read count) not to point at it. Caller holds the store lock.
static uint32_t pushAndLog(size_t idx, Message& m) {
    const uint8_t  ch   = (uint8_t)idx;
    const uint32_t next = g_next_log_id[ch];
    m.log_id            = 0;
    if (next != 0 && sdhistory::append(ch, g_set.ch[idx].hash, g_set.ch[idx].name, m, next)) {
        m.log_id          = next;
        g_next_log_id[ch] = next + 1;
    }
    return messagesPush(g_messages, m);
}

// Item 4: a thread restored from the card knows only the first four bytes of its contact's key (the file name).
// S513/EST-441: a restored thread is NO LONGER a contact row, so this rebinds the THREAD only — the row, when
// there is one, arrived from a verified advert and already carries the full key.
void rebindRestoredDmPeer(const uint8_t pub[32], const char* name) {
    if (pub == nullptr) return;
    for (size_t j = 0; j < g_dms.count; j++) {
        DmMessage* m = &g_dms.m[(g_dms.head + j) % kMaxDms];
        if (!contactKeyIncomplete(m->peer_pub) || memcmp(m->peer_pub, pub, 4) != 0) continue;
        memcpy(m->peer_pub, pub, sizeof(m->peer_pub));
        if (name != nullptr && name[0] != '\0') snprintf(m->peer_name, sizeof(m->peer_name), "%s", name);
    }
}

// S514/EST-456: a hop hash -> a known repeater's name, the L2's meshPathHopName rule (2-byte hash mode compares
// that many leading key bytes). An unknown hop stays unnamed and is drawn as hex.
bool traceHopName(const uint8_t* hash, uint8_t len, void* ctx, char* out, size_t cap) {
    ContactStore* cs = static_cast<ContactStore*>(ctx);
    if (cs == nullptr || hash == nullptr || len < 1 || len > 3) return false;
    for (size_t i = 0; i < cs->count; ++i) {
        if (contactKeyIncomplete(cs->c[i].pub)) continue;
        if (memcmp(cs->c[i].pub, hash, len) != 0) continue;
        snprintf(out, cap, "%s", cs->c[i].name);
        return out[0] != '\0';
    }
    return false;
}

// ---- S515/EST-462: the REAL path of a message ----------------------------------------------------------------
// One place turns a decoded frame into the facts the time row shows. The path is COPIED: the RX buffer is reused
// by the next packet. A live frame's hop count is always known (0 = heard directly); only a restored record can
// be unknown, and that is mc_history's rule, not this one.
RouteType routeOfView(const meshcore::View& v) {
    switch (v.route()) {
        case meshcore::kRouteFlood: return RouteType::Flood;
        case meshcore::kRouteTransportFlood: return RouteType::ScopedFlood;
        case meshcore::kRouteDirect: return RouteType::Direct;
        default: return RouteType::Unknown;
    }
}

template <typename M>
void takeWirePath(const meshcore::View& v, size_t frame_len, M& m) {
    m.direct     = (v.path_bytes == 0);
    m.hops_known = true;
    m.hops       = v.hash_count;
    m.hash_size  = (v.hash_count > 0) ? v.hash_size : 0;
    m.path_bytes = 0;
    const size_t bytes = (size_t)v.hash_count * (size_t)v.hash_size;
    if (v.hash_count > 0 && v.path != nullptr && v.hash_size >= 1 && v.hash_size <= 3 &&
        bytes <= meshcore::kMaxPath) {
        memcpy(m.path, v.path, bytes);
        m.path_bytes = (uint8_t)bytes;
    }
    m.frame_len = frame_len > 0 && frame_len <= 0xFFFF ? (uint16_t)frame_len : 0;
    m.route     = routeOfView(v);
    m.frame_fp  = meshcore::frameFingerprint(v);   // the seen ring's key, so a dropped copy can find its line
}

// The frame we are about to send: its fingerprint, taken the same way the RX path takes it.
uint32_t outgoingFingerprint(const uint8_t* frame, size_t len) {
    meshcore::View ov;
    if (frame == nullptr || len == 0 || !meshcore::decode(frame, len, &ov)) return 0;
    return meshcore::frameFingerprint(ov);
}

// The seen ring has just dropped a frame (the 0x42 rule). If it was OURS it is an echo of a frame we queued, so
// it counts on the outgoing line it came from; otherwise it is a duplicate copy of a peer's line. A count that
// finds no line (evicted, or an ACK/path return rather than a message) changes nothing.
void noteDroppedCopy(uint32_t fp, bool ours) {
    if (fp == 0) return;
    StoreLock l;
    for (size_t i = 0; i < g_messages.count; ++i) {
        Message& m = g_messages.m[(g_messages.head + i) % kMaxMessages];
        if (m.frame_fp != fp || m.outgoing != ours) continue;
        ++m.copies;
        return;
    }
    for (size_t i = 0; i < g_dms.count; ++i) {
        DmMessage& m = g_dms.m[(g_dms.head + i) % kMaxDms];
        if (m.frame_fp != fp || m.outgoing != ours) continue;
        ++m.copies;
        return;
    }
}

void onPacket(const uint8_t* buf, size_t len, int rssi_dbm, int snr_db) {
    meshcore::View v;
    if (!meshcore::decode(buf, len, &v)) {
        ESP_LOGW(TAG, "rx %u B: not a MeshCore frame", (unsigned)len);
        return;
    }
    // S513/EST-441: ONE check for every type, before any of them is acted on. A frame whose payload we have
    // already seen is either our OWN transmission heard back (registered by markOwnFrame before it went out) or a
    // second copy of a flood. The path is not part of the fingerprint, so a repeater's copy — same payload, its
    // hash added to the path — matches. This is what stopped v41 from opening its own DM as an incoming one.
    {
        bool ours = false;
        bool seen = false;
        // Same lock as markOwnFrame(): the ring is written by both tasks (see the comment there).
        const bool     locked = g_tx_lock != nullptr;
        const uint32_t fp     = meshcore::frameFingerprint(v);
        if (locked) xSemaphoreTake(g_tx_lock, portMAX_DELAY);
        seen = g_seen.seen(fp, &ours);
        if (locked) xSemaphoreGive(g_tx_lock);
        if (seen) {
            // S515/EST-462: the drop is still a fact worth counting — a duplicate copy of a peer's line, or our
            // own frame heard back. It lands on the message in the store, never on the log.
            noteDroppedCopy(fp, ours);
            ESP_LOGI(TAG, "rx %u B type %u: %s, dropped", (unsigned)len, (unsigned)v.type(),
                     ours ? "our own echo" : "duplicate, already shown");
            return;
        }
    }
    // ---------------------------------------------- S514/EST-453: RPTR part 1's two answer types
    // A CONTROL frame is the discovery answer; a RESPONSE is a login or a status reply. Both are handled on the
    // radio task, and the RPTR module keeps its own lock — it never transmits from here (a request always goes
    // back out through queueRptrFrame/queueFrame).
    if (v.type() == meshcore::kTypeControl) {
        om::rptrlink::onControl(v, snr_db);
        return;
    }
    if (v.type() == meshcore::kTypeResponse) {
        if (om::rptrlink::onResponse(v)) return;
        // Not one of ours: fall through to the old "unhandled" log at the bottom.
    }

    // S514/EST-456: the PING's answer. rptrlink::onResponse above only consumes its OWN answers, so this sees
    // every other RESPONSE. Only the contact we pinged opens it, and only while its ping is waiting.
    if (v.type() == meshcore::kTypeResponse && g_has_id) {
        StoreLock l;
        if (g_ping.state == ops::PingState::Sent) {
            uint8_t secret[meshcore::kSecretBytes];
            if (identitySharedSecret(g_id, g_ping.key, secret) &&
                ops::openPingResponse(g_id, g_ping.key, secret, v)) {
                ops::pingReplied(g_ping, monotonicMs());
                ESP_LOGI(TAG, "ping: reply in %lu ms", (unsigned long)g_ping.rtt_ms);
                return;
            }
        }
    }

    // S514/EST-456: the TRACE's answer — a DIRECT TRACE packet whose path holds one SNR per hop and whose payload
    // carries the hop hashes (Mesh.cpp:42-57). Names come from the contact table; an unmatched hop is its hash.
    if (v.type() == 0x09 && v.route() == meshcore::kRouteDirect) {
        StoreLock l;
        if (g_trace_pending && v.payload_len >= 4 && memcmp(v.payload, &g_trace_tag, 4) == 0) {
            ops::TraceView tv;
            if (ops::parseTraceResponse(v, (int8_t)(snr_db * 4), &traceHopName, &g_contacts, &tv)) {
                snprintf(tv.node, sizeof(tv.node), "%s", g_trace_job.node);
                tv.rttMs  = monotonicMs() - g_trace_job.sent_ms;
                g_trace         = tv;
                g_trace_pending = false;
                ++g_trace_serial;
                ESP_LOGI(TAG, "trace: %d hop(s) back", tv.count);
                return;
            }
        }
    }

    // ------------------------------------------------- S513/EST-439: the PATH answer and the delivered ACK
    // A PATH packet is the recipient's answer to a flooded DM: it carries the route back TO that contact and, when
    // its extra type is ACK, the delivered mark for the message we sent (Mesh.cpp:160-180). Opening it is the
    // only way a route is ever learned, and storing it is a STRUCTURAL change to the contact table.
    if (v.type() == meshcore::kTypePath) {
        // S514 (Claude): a PATH can also be a repeater's answer to our login/status (it is not in the contact
        // table when it was found by a scan); the RPTR link tries the login target's key first.
        if (om::rptrlink::onPathReply(v)) return;
        if (!g_has_id) {
            ESP_LOGW(TAG, "rx %u B PATH but this node has no identity yet, dropped", (unsigned)len);
            return;
        }
        // S515/EST-463: 64 KB of peer keys, so PSRAM once (the DM and PATH snapshots share the same buffer).
        static uint8_t* peer_pubs = nullptr;
        if (peer_pubs == nullptr) {
            peer_pubs = static_cast<uint8_t*>(nodeTableAlloc((size_t)kMaxNodes * meshcore::kPubKeyBytes));
            if (peer_pubs == nullptr) {
                ESP_LOGE(TAG, "rx PATH: peer snapshot allocation failed, dropped");
                return;
            }
        }
        const size_t peer_count = snapshotContactPubs(peer_pubs, kMaxNodes);
        if (peer_count == 0) {
            ESP_LOGI(TAG, "rx %u B PATH but no contacts are known yet, dropped", (unsigned)len);
            return;
        }
        meshcore::DmPath p;
        if (!meshcore::dmOpenPath(g_id, v, peer_pubs, peer_count, &p)) {
            ESP_LOGI(TAG, "rx %u B PATH, no contact of ours opens it", (unsigned)len);
            return;
        }
        {
            StoreLock l;
            Contact&      c     = g_contacts.c[p.peer];
            const uint8_t hops  = (uint8_t)(p.out_path_len & 63);
            const size_t  bytes = (size_t)hops * (size_t)((p.out_path_len >> 6) + 1);
            const bool    moved = c.out_path_len != p.out_path_len ||
                                  (bytes > 0 && memcmp(c.out_path, p.out_path, bytes) != 0);
            if (moved) {
                c.out_path_len = p.out_path_len;
                memset(c.out_path, 0, sizeof(c.out_path));
                if (bytes > 0) memcpy(c.out_path, p.out_path, bytes);
                markContactsDirty(true);   // a new route is written to NVS on the main task's next pass
            }
            ESP_LOGI(TAG, "DM: route to \"%s\" %s (path_len 0x%02X, %u hop(s))", c.name,
                     c.out_path_len == kOutPathUnknown ? "unknown" : "stored", (unsigned)c.out_path_len,
                     (unsigned)hops);
        }
        if (p.extra_type == meshcore::kTypeAck && p.extra_len >= 4) {
            if (!dmNoteAck(p.extra)) ESP_LOGI(TAG, "DM: PATH carried an ACK no pending message expected");
        }
        return;
    }

    // A plain ACK (a DIRECT arrival was answered with createAck): payload = the 4-byte crc.
    if (v.type() == meshcore::kTypeAck) {
        const uint8_t* ack = meshcore::dmAckPayload(v);
        if (ack == nullptr) {
            ESP_LOGI(TAG, "rx %u B ACK with a payload shorter than 4 bytes", (unsigned)len);
            return;
        }
        if (!dmNoteAck(ack)) ESP_LOGI(TAG, "rx %u B ACK: no pending DM expects it", (unsigned)len);
        return;
    }

    // ------------------------------------------------------------------- direct messages (S513/EST-434, item 2)
    // A TXT_MSG is not channel-scoped either: it is addressed to OUR pub_key[0] and sealed with the shared secret
    // of one contact. It is handled here, on the radio task, and the reply is only QUEUED — the receive path never
    // transmits. The DM's timestamp is NOT a clock source (unlike an advert): anyone holding a contact's key can
    // write any ts, so it is recorded and noteText()/noteAdvert() are deliberately not called.
    if (v.type() == meshcore::kTypeTxtMsg) {
        // S515/EST-470: a repeater's CLI ANSWER arrives as a TXT_MSG that is sealed with the LOGIN TARGET's key,
        // and the answering repeater is usually NOT in the contact table - the same shape as v55's dropped login
        // reply (PATH -> onPathReply). The RPTR link tries that key FIRST and takes only a CLI-typed text, so:
        //   · a CLI answer never lands in a DM thread and is never dropped as "no contact opens it";
        //   · it never reaches dmPush, so it raises no new-message pop-up (core/msg_popup.h) and no Chat dot;
        //   · a PLAIN text from that same repeater still falls through to the DM code below, untouched.
        if (om::rptrlink::onCliAnswer(v)) return;
        if (!g_has_id) {
            ESP_LOGW(TAG, "rx %u B TXT_MSG but this node has no identity yet, dropped", (unsigned)len);
            return;
        }
        // A snapshot of the contact table, because dmOpen walks it and two contacts may share pub_key[0]. The
        // arrays are static: the radio task is the only caller, and 32 contacts must not land on its stack.
        // S515/EST-463: 2,000 peers is ~112 KB, so this snapshot is PSRAM, not .bss (see initNodeTable()).
        static uint8_t* peer_pubs  = nullptr;
        static char*    peer_names = nullptr;
        if (peer_pubs == nullptr) {
            peer_pubs  = static_cast<uint8_t*>(nodeTableAlloc((size_t)kMaxNodes * meshcore::kPubKeyBytes));
            peer_names = static_cast<char*>(nodeTableAlloc((size_t)kMaxNodes * kNameBytes));
        }
        if (peer_pubs == nullptr || peer_names == nullptr) {
            ESP_LOGE(TAG, "rx TXT_MSG: peer snapshot allocation failed, dropped");
            return;
        }
        size_t         peer_count = 0;
        {
            StoreLock l;
            peer_count = g_contacts.count;
            for (size_t i = 0; i < peer_count; i++) {
                memcpy(&peer_pubs[i * meshcore::kPubKeyBytes], g_contacts.c[i].pub, meshcore::kPubKeyBytes);
                snprintf(&peer_names[i * kNameBytes], kNameBytes, "%s", g_contacts.c[i].name);
            }
        }
        if (peer_count == 0) {
            ESP_LOGI(TAG, "rx %u B TXT_MSG but no contacts are known yet, dropped", (unsigned)len);
            return;
        }

        meshcore::DmMessage dm;
        if (!meshcore::dmOpen(g_id, v, peer_pubs, peer_count, (uint8_t)esp_random(), &dm)) {
            ESP_LOGI(TAG, "rx %u B TXT_MSG, no contact of ours opens it (src 0x%02X, %d dBm)",
                     (unsigned)len, v.payload_len > 1 ? v.payload[1] : 0, rssi_dbm);
            return;
        }
        // S513/EST-441: the seen-ring check at the top of onPacket already dropped our own echo and a duplicate
        // flood copy, for every type — before the MAC was even tried, which is what stops our own DM opening as
        // "from" a peer whose pub_key[0] happens to equal ours (ECDH is symmetric, so it DOES open).

        // S516/EST-472 item 5: rptr_link handles the LOGGED-IN repeater's CLI answers above. This is the second
        // door: a CLI-typed TXT_MSG that still reached the DM path (a late answer, a repeater that is also a
        // contact, a login that has ended) is repeater traffic, not a person's message. Without this it became a
        // DM thread with a pop-up and an unread dot.
        if (meshcore::isRepeaterCliText(dm.txt_type)) {
            ESP_LOGI(TAG, "rx %u B TXT_MSG of CLI type %u (repeater traffic, not a DM), dropped", (unsigned)len,
                     (unsigned)dm.txt_type);
            return;
        }

        const uint8_t* peer_pub = &peer_pubs[(size_t)dm.peer * meshcore::kPubKeyBytes];
        char           peer_name[kNameBytes];
        snprintf(peer_name, sizeof(peer_name), "%s", &peer_names[(size_t)dm.peer * kNameBytes]);
        if (peer_name[0] == '\0') {
            // A contact row with no name yet: the 8-hex prefix of its public key is a stable, honest label.
            snprintf(peer_name, sizeof(peer_name), "%02x%02x%02x%02x", peer_pub[0], peer_pub[1], peer_pub[2],
                     peer_pub[3]);
        }
        ESP_LOGI(TAG, "DM from \"%s\": %s  (ts %u, %d dBm, snr %d dB, %u B, %s)", peer_name, dm.text,
                 (unsigned)dm.timestamp, rssi_dbm, snr_db, (unsigned)len,
                 v.path_bytes == 0 ? "direct" : "flood");

        {
            StoreLock l;
            DmMessage m;
            memcpy(m.peer_pub, peer_pub, sizeof(m.peer_pub));
            snprintf(m.peer_name, sizeof(m.peer_name), "%s", peer_name);
            m.outgoing = false;
            m.ts       = dm.timestamp;
            m.local_ts = (uint32_t)om::time_source::epochNow();   // OUR clock, for ordering and the list's HH:MM
            m.snr_x10  = (int16_t)(snr_db * 10);
            // S515/EST-462: the DM's own real path and frame length, exactly as the RX log line prints it.
            takeWirePath(v, len, m);
            snprintf(m.text, sizeof(m.text), "%.*s", (int)(sizeof(m.text) - 1), dm.text);
            // Item 4: the thread's log is dm-<pubkey[0..3] 8 hex>. Its id is per file, kept on the contact row so
            // evicting the ring cannot reset the numbering; a message that could not be written keeps log_id 0
            // and can never be what the seen point names (the same rule as channels, S503).
            const int      ci   = contactsFind(g_contacts, peer_pub);
            uint32_t       next = (ci >= 0) ? g_contacts.c[ci].dm_log_next
                                            : dmNewestLogId(g_dms, peer_pub) + 1;
            if (next == 0) next = 1;
            if (sdhistory::appendDm(peer_pub, m, next)) {
                m.log_id = next;
                if (ci >= 0) g_contacts.c[ci].dm_log_next = next + 1;
                logFatfsWriterStackOnce();   // S516 item 8: the radio task's own FatFs writer, measured once
            }
            (void)dmPush(g_dms, m);
        }
        // S514/EST-450: a DM to us, after the seen-ring check above dropped our own echo and a
        // duplicate flood copy. Played before the ACK reply is queued so the sound never delays it.
        om::audioout::playEvent(tm::soundcfg::Event::Dm);
        // S515/EST-467: the same message as a chrome pop-up. The READER (main task) applies "not while this
        // same chat is open" and "not while the screen is off"; our own echo never got here (seen ring).
        {
            char head[64];
            snprintf(head, sizeof(head), "DM \xc2\xb7 %s", peer_name);
            om::ui::msgPopupOffer(head, dm.text, om::ui::PopupChat::Dm, 0, peer_pub);
        }

        if (!dm.ack_valid) {
            // CLI_DATA / CLI_COMMAND are shown but never acknowledged (BaseChatMesh.cpp:244): there is no ACK
            // hash for a type MeshCore does not acknowledge.
            ESP_LOGI(TAG, "DM: txt_type %u is not acknowledged", (unsigned)dm.txt_type);
            return;
        }

        uint8_t      frame[256];
        size_t       n = 0;
        if (v.route() == meshcore::kRouteFlood) {
            // A flood arrival gets a PATH packet carrying the route back plus the ACK (createPathReturn).
            uint8_t secret[meshcore::kSecretBytes];
            if (meshcore::identitySharedSecret(g_id, peer_pub, secret)) {
                n = meshcore::dmBuildPathReturn(v, peer_pub[0], g_id.pub, secret, dm.ack, frame, sizeof(frame));
            }
        } else {
            // A direct arrival is answered with createAck (BaseChatMesh.cpp:43-58). If we have learned a route to
            // that contact it is copied back; OUT_PATH_UNKNOWN is the only value that means "no route", so a
            // zero-hop route is a direct ACK to a neighbour (EST-439).
            static uint8_t ack_path[meshcore::kMaxPath];
            uint8_t        ack_path_len = kOutPathUnknown;
            {
                StoreLock l;
                const int ci = contactsFind(g_contacts, peer_pub);
                if (ci >= 0 && contactHasOutPath(g_contacts.c[ci])) {
                    ack_path_len = g_contacts.c[ci].out_path_len;
                    const size_t bytes = (size_t)(ack_path_len & 63) * (size_t)((ack_path_len >> 6) + 1);
                    if (bytes > 0) memcpy(ack_path, g_contacts.c[ci].out_path, bytes);
                }
            }
            n = meshcore::dmBuildAck(ack_path_len == kOutPathUnknown ? nullptr : ack_path, ack_path_len, dm.ack,
                                     frame, sizeof(frame));
        }
        if (n == 0) {
            ESP_LOGW(TAG, "DM: could not build the %s reply", v.route() == meshcore::kRouteFlood ? "path return"
                                                                                              : "ACK");
            return;
        }
        if (!queueFrame(frame, n)) {
            ESP_LOGW(TAG, "DM: the reply (%u B) could not be queued", (unsigned)n);
        } else {
            ESP_LOGI(TAG, "DM: queued a %s (%u B)", v.route() == meshcore::kRouteFlood ? "path return" : "flood ACK",
                     (unsigned)n);
        }
        return;
    }

    // An advert is not channel-scoped (it is a signed node announcement flooded across the mesh), so it is
    // handled BEFORE the channel table is consulted — and it is the only AUTHENTICATED time this device ever
    // hears on its own (Ed25519 signature over pub||ts||app_data). S501 dropped adverts here when no channel
    // was provisioned; that was wrong twice.
    if (v.type() == meshcore::kTypeAdvert) {
        uint8_t  pub[meshcore::kPubKeyBytes];
        uint32_t ts   = 0;
        uint8_t  type = 0;
        char     name[24] = {0};
        meshcore::AdvertPosition pos;   // S515/EST-461: the advert's position, stored for EST-463's map
        // type != nullptr: keep EVERY verified node type here (repeaters included), not only chat nodes — the
        // Nodes page is a list of what the radio can hear.
        if (!meshcore::advertParse(v, pub, &ts, name, sizeof(name), &type, &pos)) {
            ESP_LOGW(TAG, "rx %u B: advert with an INVALID signature, dropped", (unsigned)len);
            return;
        }
        if (g_has_id && memcmp(pub, g_id.pub, sizeof(pub)) == 0) {
            ESP_LOGI(TAG, "ADVERT: our own advert came back");
            return;
        }
        char prefix[17] = {0};
        for (size_t i = 0; i < 8; i++) snprintf(&prefix[2 * i], 3, "%02x", pub[i]);
        ESP_LOGI(TAG, "ADVERT \"%s\" from %s (ts %u, %d dBm, snr %d dB, %u B)", name, prefix,
                 (unsigned)ts, rssi_dbm, snr_db, (unsigned)len);
        // A verified advert is a verified CLOCK CANDIDATE, not a clock: the signature proves who signed it,
        // never that their clock is right. The radio task only records the sample (with the key that signed
        // it); the main task adopts it only when a second, DIFFERENT node agrees (core/clock.h, S513/EST-429).
        om::time_source::noteAdvert(ts, pub, sizeof(pub));
        {
            StoreLock l;
            rebindRestoredDmPeer(pub, name);
            // S513/EST-437: remember the row BEFORE the upsert so "new" means "was not in the table", not
            // "the name changed". A restored contact (NVS or a DM thread from the card) is known, so its advert
            // is silent; our own echo returned above.
            const int before = contactsFind(g_contacts, pub);
            char      old_name[kNameBytes] = {0};
            uint8_t   old_path = 0;
            uint8_t   old_type = 0;
            if (before >= 0) {
                snprintf(old_name, sizeof(old_name), "%s", g_contacts.c[before].name);
                old_path = g_contacts.c[before].path_len;
                old_type = g_contacts.c[before].type;
            }
            const uint32_t heard = om::time_source::epochNow();
            // S515/EST-464 (the EST-463 blocker): a verified advert goes through the table's RULE, never straight to
            // contactsUpsert. With the setting Off (the default) that means archive-only; nothing enters the radio
            // automatically. The rule also keeps a hand-chosen radio node in the radio on a refresh.
            const int      idx =
                advertInArchive(g_contacts, g_heard_to_radio, pub, name, type, v.path_bytes, ts, heard, rssi_dbm,
                                snr_db * 10, pos.has_position, pos.lat_e6, pos.lon_e6, radioCapacity());
            if (idx >= 0) {
                // Add / rename / route change is worth an immediate write; a repeat advert that only moves
                // last-heard and SNR waits for the 10-minute floor. The radio file's signature check means an
                // archive-only change writes no internal flash at all.
                const bool structural = before < 0 || strcmp(old_name, g_contacts.c[idx].name) != 0 ||
                                        old_path != g_contacts.c[idx].path_len ||
                                        old_type != g_contacts.c[idx].type;
                markContactsDirty(structural);
                if (before < 0 && g_new_node_notice) {
                    char text[64];
                    snprintf(text, sizeof(text), "New node: %s (%s)", name[0] != '\0' ? name : "?",
                             advertTypeName(type));
                    om::ui::noticeSet(text, 3000);
                    // S514/EST-450: the sound uses the SAME condition as the notice. A repeat advert
                    // (before >= 0), our own echo and a flood duplicate all returned above without
                    // reaching this branch, so a restored contact stays silent too.
                    om::audioout::playEvent(tm::soundcfg::Event::NewNode, true);
                }
            }
        }
        return;
    }
    if (!g_loaded || g_set.count == 0) {
        ESP_LOGW(TAG, "rx %u B type %u, but no channels are provisioned", (unsigned)len,
                 (unsigned)v.type());
        return;
    }
    if (v.type() != meshcore::kTypeGrpTxt) {
        ESP_LOGI(TAG, "rx %u B type %u (not a channel text), rssi %d dBm snr %d dB", (unsigned)len,
                 (unsigned)v.type(), rssi_dbm, snr_db);
        return;
    }

    char     sender[32] = {0};
    char     text[200]  = {0};
    uint32_t ts         = 0;
    const int idx = meshcore::channelsOpen(g_set, v, sender, sizeof(sender), text, sizeof(text), &ts);
    if (idx < 0) {
        ESP_LOGI(TAG, "rx %u B group text, no channel of ours opens it (hash 0x%02X, rssi %d dBm)",
                 (unsigned)len, v.payload_len > 0 ? v.payload[0] : 0, rssi_dbm);
        return;
    }
    // S513/EST-441: our own text and its duplicate flood copies were dropped by the seen-ring check at the top of
    // onPacket, which now covers every type instead of this branch alone.
    ESP_LOGI(TAG, "MSG ch%u \"%s\" from \"%s\": %s  (ts %u, %d dBm, snr %d dB, %u B)",
             (unsigned)idx, g_set.ch[idx].name, sender, text, (unsigned)ts, rssi_dbm, snr_db,
             (unsigned)len);
    // The ts is only a time if the MAC that opened it verified, which it just did: this frame is a bonafide
    // holder of the channel key. But that proves the KEY, not the sender and not the time (a channel key can
    // be public, and the sender name is self-declared), so a text is recorded and never counts as a witness
    // for the clock. Adverts only: two different signed nodes must agree (core/clock.h, S513/EST-429).
    om::time_source::noteText(ts);
    {
        StoreLock l;
        Message m;
        m.channel   = (uint8_t)idx;
        m.outgoing  = false;
        m.ts        = ts;
        m.local_ts  = (uint32_t)om::time_source::epochNow();
        m.snr_x10   = (int16_t)(snr_db * 10);
        // S515/EST-462: the REAL path, the route type and the frame length (the same number the RX log prints).
        takeWirePath(v, len, m);
        // A message longer than the field is CUT, not dropped: the field widths are the UI's, not MeshCore's.
        snprintf(m.sender, sizeof(m.sender), "%.*s", (int)(sizeof(m.sender) - 1), sender);
        snprintf(m.text, sizeof(m.text), "%.*s", (int)(sizeof(m.text) - 1), text);
        const uint32_t seq = pushAndLog((size_t)idx, m);
        (void)seq;
    }
    // S514/EST-450: a live channel text from someone else. Our own echo and a duplicate flood copy
    // were dropped by the seen ring before this point, so they can never reach the speaker.
    om::audioout::playEvent(tm::soundcfg::Event::Message);
    // S515/EST-467: the pop-up's first line is the channel and the sender, then the text. The main task
    // decides whether this same chat is already on screen; the radio task knows nothing of pages.
    {
        char head[64];
        snprintf(head, sizeof(head), "#%.20s \xc2\xb7 %.20s", g_set.ch[idx].name, sender);
        om::ui::msgPopupOffer(head, text, om::ui::PopupChat::Channel, (uint8_t)idx, nullptr);
    }
}

// Performed by the radio task only.
static int doText(size_t i, const char* sender, const char* text) {
    if (i >= g_set.count) {
        ESP_LOGW(TAG, "tx: no channel %u (have %u)", (unsigned)i, (unsigned)g_set.count);
        return -1;
    }
    if (sender == nullptr || text == nullptr) return -1;

    // MeshCore puts a unix timestamp in the plaintext; it is what the receiving app shows as the message time.
    // Since S502 it is the app's OWN clock (main/time_source.cpp): the coprocessor's RTC at boot, the stored
    // floor if that was empty, and — only since S513/EST-429 — a time two DIFFERENT adverts agree on. One
    // wrong node can no longer move it. Logged, so a wrong one is visible rather than silent.
    const uint32_t ts = (uint32_t)om::time_source::epochNow();

    // S513/EST-440: the channel's own flood scope, resolved once per send (channel -> node default ->
    // unscoped). A region becomes MeshCore's TRANSPORT_FLOOD frame with the two transport codes; No
    // scope and Default-with-no-default stay the plain flood, byte for byte what was sent before.
    char scopeStored[meshcore::kScopeNameMax + 1] = "";
    if (!channelScopeStored(i, scopeStored, sizeof(scopeStored)))
        ESP_LOGW(TAG, "tx: ch%u scope could not be read; sending unscoped", (unsigned)i);
    const meshcore::ScopePlan plan = meshcore::scopePlanFor(scopeStored, nullptr);

    uint8_t      frame[256];
    const size_t n = meshcore::buildScopedChannelText(g_set.ch[i], ts, sender, text, meshcore::kFloodPathHashSize, plan, nullptr, frame,
                                                      sizeof(frame));
    if (n == 0) {
        ESP_LOGE(TAG, "tx: build failed (text too long? %u chars)", (unsigned)strlen(text));
        return -1;
    }
    // S513/EST-441: registered BEFORE the frame goes out. It used to be registered after the send, which left a
    // window in which the echo could be processed as an incoming text; and it is `ours`, so the drop says why.
    markOwnFrame(frame, n);
    const int sent = om::radio::send(frame, n);
    // The L2's words, with the region name the ticket names (a #tag is public); rule 9 still keeps the
    // message text out of the log.
    char scope_log[48];
    if (plan.region[0] != 0)
        snprintf(scope_log, sizeof(scope_log), "region \"#%.30s\"", plan.region);
    else if (plan.mode == meshcore::ScopeMode::None)
        snprintf(scope_log, sizeof(scope_log), "none");
    else
        snprintf(scope_log, sizeof(scope_log), "unscoped");
    ESP_LOGI(TAG, "TX ch%u \"%s\" %u B ts %u scope=%s -> %s", (unsigned)i, g_set.ch[i].name, (unsigned)n,
             (unsigned)ts, scope_log, sent > 0 ? "ok" : "FAILED");
    if (sent > 0) {
        // Keep the outgoing line in the store ourselves, so the chat has it the instant Enter is pressed. The echo
        // is already accounted for: the frame was registered above, and RX drops it as our own — and since
        // S515/EST-462 that drop also COUNTS on this line ("repeated 2×").
        StoreLock l;
        Message   m;
        m.channel  = (uint8_t)i;
        m.outgoing = true;
        m.ts       = ts;
        m.local_ts = ts;
        m.direct   = false;
        m.snr_x10  = 0;
        // The route we actually sent on, and the frame length we queued. A flood's hop count is not known: it
        // depends on who repeats it, so `hops_known` stays false rather than inventing one.
        m.hops_known = false;
        m.hops       = 0;
        m.hash_size  = 0;
        m.frame_len  = (uint16_t)n;
        m.route      = plan.region[0] != '\0' ? RouteType::ScopedFlood : RouteType::Flood;
        m.frame_fp   = outgoingFingerprint(frame, n);
        snprintf(m.sender, sizeof(m.sender), "%.*s", (int)(sizeof(m.sender) - 1), sender);
        snprintf(m.text, sizeof(m.text), "%.*s", (int)(sizeof(m.text) - 1), text);
        const uint32_t seq = pushAndLog(i, m);
        (void)seq;
    } else {
        // S514/EST-450: the radio refused the frame. "Sent" was the queue accepting it on the main task
        // (the L2's own rule); this is the real failure, so it is the error sound.
        om::audioout::playEvent(tm::soundcfg::Event::Error);
    }
    return sent > 0 ? (int)n : -1;
}

static int doAdvert(const char* name) {
    // The 5 s gap counts from the AIR, not from the key press, and this is the only place that knows whether
    // anything was transmitted. EVERY exit from this function resolves the gate, or a build failure would leave
    // it "in flight" and no advert could ever be sent again.
    auto resolve = [](bool ok) {
        // S513/EST-437: the notice is the REAL result of this transmission, on every exit — a refused radio call
        // and a build/identity failure both say "Advert NOT sent", never "sent" because the key was pressed. The
        // notice slot is safe from the radio task (chrome.cpp).
        om::ui::noticeSet(ok ? "Advert sent" : "Advert NOT sent", 3000);
        if (g_tx_lock == nullptr) return;
        xSemaphoreTake(g_tx_lock, portMAX_DELAY);
        advertResolved(g_advert_gate, monotonicMs(), ok);
        xSemaphoreGive(g_tx_lock);
    };
    if (!g_has_id) {
        ESP_LOGE(TAG, "advert: no identity (see the identity lines above)");
        resolve(false);
        return -1;
    }
    if (name == nullptr || name[0] == '\0') {
        resolve(false);
        return -1;
    }
    const uint32_t ts = nextAdvertTimestamp();
    uint8_t        frame[256];
    const size_t   n = meshcore::advertBuild(g_id, ts, name, meshcore::kFloodPathHashSize, frame, sizeof(frame));
    if (n == 0) {
        ESP_LOGE(TAG, "advert: build failed (name too long?)");
        resolve(false);
        return -1;
    }
    // S513/EST-441: an advert is a frame of ours too, so it is registered before it goes out. The pub_key
    // comparison in the advert branch still catches it (a second, independent guard), but the ring is what makes
    // every type behave the same.
    markOwnFrame(frame, n);
    const int sent = om::radio::send(frame, n);
    ESP_LOGI(TAG, "ADVERT TX \"%s\" %u B ts %u -> %s", name, (unsigned)n, (unsigned)ts,
             sent > 0 ? "ok" : "FAILED");
    resolve(sent > 0);   // this also sets "Advert sent" / "Advert NOT sent" from the real result
    return sent > 0 ? (int)n : -1;
}

bool queueText(size_t i, const char* text) {
    if (i >= g_set.count || text == nullptr || g_tx_lock == nullptr) return false;
    if (i > 0xFF) return false;   // the request carries a uint8 channel; a longer table is not a message
    xSemaphoreTake(g_tx_lock, portMAX_DELAY);
    const bool   ok    = g_txq.pushText((uint8_t)i, g_node_name, text);
    const size_t depth = g_txq.size();
    xSemaphoreGive(g_tx_lock);
    if (!ok) ESP_LOGW(TAG, "tx: the send queue is full (%d waiting); the text was NOT queued", (int)depth);
    return ok;
}

// S513/EST-441: may a DM to this peer be sent, and what is it called? False when the peer is not a contact with a
// FULL key — a restored thread carries four bytes from its file name and nothing else. The DM view uses this to
// keep the text and say "No key for <name> yet: waiting for its advert"; queueDm() re-checks it and refuses.
bool dmPeerReady(const uint8_t pub[32], char* name, size_t cap) {
    if (name != nullptr && cap > 0) name[0] = '\0';
    if (pub == nullptr) return false;
    StoreLock l;
    if (dmPeerKey(g_contacts, pub, name, cap)) return true;
    // Not a full-key contact: name it the way its thread is named — the only matching full-key contact's name if
    // the prefix is unique, else the name on the thread's own records, else the 8-hex prefix.
    if (name != nullptr && cap > 0) {
        const int ci = contactsFindUniquePrefix(g_contacts, pub);
        if (ci >= 0) snprintf(name, cap, "%s", g_contacts.c[ci].name);
        if (name[0] == '\0') {
            for (size_t j = g_dms.count; j > 0; j--) {
                const DmMessage* m = dmAt(g_dms, j - 1);
                if (m == nullptr || memcmp(m->peer_pub, pub, sizeof(m->peer_pub)) != 0) continue;
                if (m->peer_name[0] == '\0') continue;
                snprintf(name, cap, "%s", m->peer_name);
                break;
            }
        }
        if (name[0] == '\0') snprintf(name, cap, "%02x%02x%02x%02x", pub[0], pub[1], pub[2], pub[3]);
    }
    return false;
}

bool queueDm(const uint8_t pub[32], const char* text) {
    if (pub == nullptr || text == nullptr || text[0] == '\0') return false;
    if (!g_has_id || g_tx_lock == nullptr || g_dm_lock == nullptr) return false;
    if (strlen(text) > meshcore::kMaxText) return false;

    // S513/EST-441: before ANYTHING else, the peer must be a contact with a full key. A thread restored from the
    // card carries only four bytes from its file name; a shared secret over those bytes is a secret the peer
    // cannot derive (that is why ERA-3 could not open our DM in v41), so it is never computed. The caller keeps
    // its text and the DM view says "waiting for its advert".
    char known[kNameBytes] = {0};
    {
        StoreLock l;
        if (!dmPeerKey(g_contacts, pub, known, sizeof(known))) {
            const char* label = known[0] != '\0' ? known : "";
            if (label[0] == '\0') {
                const int ci = contactsFindUniquePrefix(g_contacts, pub);
                if (ci >= 0) label = g_contacts.c[ci].name;
            }
            ESP_LOGW(TAG, "DM: no full key for \"%s\" yet; waiting for its advert", label[0] != '\0' ? label : "?");
            return false;
        }
    }

    // The caller gets false only when nothing was stored and nothing was queued, so its text is still its own.
    xSemaphoreTake(g_tx_lock, portMAX_DELAY);
    const bool queue_full = g_txq.full();
    xSemaphoreGive(g_tx_lock);
    if (queue_full) {
        ESP_LOGW(TAG, "DM: the send queue is full; \"%s\" was NOT queued", text);
        return false;
    }

    uint8_t secret[meshcore::kSecretBytes];
    if (!meshcore::identitySharedSecret(g_id, pub, secret)) {
        ESP_LOGE(TAG, "DM: no shared secret for this contact");
        return false;
    }

    // The route exactly as the contact stores it: OUT_PATH_UNKNOWN (or a missing row) floods; every other valid
    // encoding is DIRECT, including a zero-hop path to a neighbour (EST-439).
    uint8_t path[meshcore::kMaxPath] = {0};
    uint8_t path_len                 = kOutPathUnknown;
    char    name[kNameBytes]        = {0};
    {
        StoreLock l;
        const int ci = contactsFind(g_contacts, pub);
        if (ci >= 0) {
            path_len = g_contacts.c[ci].out_path_len;
            const size_t bytes = path_len == kOutPathUnknown
                                     ? 0
                                     : (size_t)(path_len & 63) * (size_t)((path_len >> 6) + 1);
            if (bytes > 0) memcpy(path, g_contacts.c[ci].out_path, bytes);
            snprintf(name, sizeof(name), "%s", g_contacts.c[ci].name);
        }
    }
    if (name[0] == '\0') snprintf(name, sizeof(name), "%02x%02x%02x%02x", pub[0], pub[1], pub[2], pub[3]);

    const uint32_t          ts = (uint32_t)om::time_source::epochNow();
    meshcore::DmComposed    c;
    if (!meshcore::dmCompose(g_id, pub, secret, ts, 0, text, path, path_len, &c)) {
        ESP_LOGW(TAG, "DM: could not compose \"%s\" (%u chars)", text, (unsigned)strlen(text));
        return false;
    }

    // The message goes into the thread (and the card) BEFORE the pending entry, because the entry names it by
    // its seq. Its state is Sending: the ACK, not the radio's "sent", is what proves delivery (dm_delivery.h).
    uint32_t seq = 0;
    {
        StoreLock l;
        DmMessage m;
        memcpy(m.peer_pub, pub, sizeof(m.peer_pub));
        snprintf(m.peer_name, sizeof(m.peer_name), "%s", name);
        m.outgoing = true;
        m.delivery = om::ui::DmState::Sending;
        m.ts       = ts;
        m.local_ts = ts;
        m.direct   = !c.flood;
        // S515/EST-462: the route we sent on and the frame length we queued. A DIRECT DM is a known 0 hops; a
        // flood's hop count is not knowable here, so it stays unknown.
        m.hops_known = !c.flood;
        m.hops       = 0;
        m.hash_size  = 0;
        m.frame_len  = c.frame_len <= 0xFFFF ? (uint16_t)c.frame_len : 0;
        m.route      = c.flood ? RouteType::Flood : RouteType::Direct;
        m.frame_fp   = outgoingFingerprint(c.frame, c.frame_len);
        snprintf(m.text, sizeof(m.text), "%.*s", (int)(sizeof(m.text) - 1), text);
        const int ci   = contactsFind(g_contacts, pub);
        uint32_t  next = (ci >= 0) ? g_contacts.c[ci].dm_log_next : dmNewestLogId(g_dms, pub) + 1;
        if (next == 0) next = 1;
        if (sdhistory::appendDm(pub, m, next)) {
            m.log_id = next;
            if (ci >= 0) g_contacts.c[ci].dm_log_next = next + 1;
        }
        seq = dmPush(g_dms, m);
    }

    const uint32_t airtime = meshcore::dmAirtimeMillis(c.frame_len);
    xSemaphoreTake(g_dm_lock, portMAX_DELAY);
    meshcore::dmPendingBegin(g_dm_pending, pub, seq, c.expected_ack,
                             c.flood ? meshcore::dmFloodTimeoutMillis(airtime)
                                     : meshcore::dmDirectTimeoutMillis(airtime, path_len),
                             !c.flood, monotonicMs());
    xSemaphoreGive(g_dm_lock);

    // The pending entry exists BEFORE the frame can leave, so an ACK that arrives quickly still finds it.
    if (!queueFrame(c.frame, c.frame_len)) {
        xSemaphoreTake(g_dm_lock, portMAX_DELAY);
        meshcore::dmPendingDrop(g_dm_pending, seq);
        xSemaphoreGive(g_dm_lock);
        StoreLock l;
        if (DmMessage* m = findDmBySeq(seq)) {
            m->delivery = om::ui::DmState::Failed;
            if (m->log_id != 0) {
                (void)sdhistory::appendDmState(m->peer_pub, m->log_id, m->delivery,
                                               (uint32_t)om::time_source::epochNow());
            }
        }
        ESP_LOGW(TAG, "DM: the frame could not be queued; \"%s\" was not sent", text);
        return false;
    }
    ESP_LOGI(TAG, "DM: queued \"%s\" to \"%s\" (%u B, %s, attempt 0)", text, name, (unsigned)c.frame_len,
             c.flood ? "flood" : "direct");
    return true;
}

bool dmTick() {
    if (g_dm_lock == nullptr || g_store_mutex == nullptr) return false;

    meshcore::DmTimeout due;
    xSemaphoreTake(g_dm_lock, portMAX_DELAY);
    due = meshcore::dmPendingTimeout(g_dm_pending, monotonicMs());
    xSemaphoreGive(g_dm_lock);
    if (due.action == meshcore::DmTimeoutAction::None) return false;

    if (due.action == meshcore::DmTimeoutAction::GiveUp) {
        bool changed = false;
        bool unconfirmed = false;
        {
            StoreLock l;
            DmMessage* m = findDmBySeq(due.msg_id);
            if (m != nullptr) {
                m->delivery = om::ui::DmState::NotConfirmed;
                changed      = true;
                unconfirmed  = true;
                if (m->log_id != 0) {
                    (void)sdhistory::appendDmState(m->peer_pub, m->log_id, m->delivery,
                                                   (uint32_t)om::time_source::epochNow());
                }
                ESP_LOGI(TAG, "DM: message %u has NO CONFIRMATION after %u attempts", (unsigned)due.msg_id,
                         (unsigned)meshcore::kDmMaxAttempts);
            }
        }
        // S514/EST-450: a DM with no confirmation is the ticket's second error case. A stale timeout
        // whose message has already left the ring changes nothing, so it is silent too.
        if (unconfirmed) om::audioout::playEvent(tm::soundcfg::Event::Error);
        return changed;
    }

    // A retry is due: the text and the timestamp are the ORIGINAL message's (MeshCore keeps the timestamp and
    // changes only the attempt, BaseChatMesh.cpp:451-471); the route may have been learned since, or cleared if
    // the direct attempt timed out.
    uint8_t  pub[32]      = {0};
    uint8_t  path[meshcore::kMaxPath] = {0};
    uint8_t  path_len     = kOutPathUnknown;
    char     text[kTextBytes] = {0};
    uint32_t ts           = 0;
    {
        StoreLock l;
        DmMessage* m = findDmBySeq(due.msg_id);
        if (m == nullptr) {
            // The ring evicted it: nothing left to retry (the log still has the message).
            xSemaphoreTake(g_dm_lock, portMAX_DELAY);
            meshcore::dmPendingDrop(g_dm_pending, due.msg_id);
            xSemaphoreGive(g_dm_lock);
            ESP_LOGW(TAG, "DM: message %u left the ring before its retry", (unsigned)due.msg_id);
            return false;
        }
        memcpy(pub, m->peer_pub, sizeof(pub));
        ts = m->ts;
        snprintf(text, sizeof(text), "%s", m->text);

        const int ci = contactsFind(g_contacts, pub);
        if (ci >= 0) {
            if (due.clear_path && contactHasOutPath(g_contacts.c[ci])) {
                g_contacts.c[ci].out_path_len = kOutPathUnknown;
                memset(g_contacts.c[ci].out_path, 0, sizeof(g_contacts.c[ci].out_path));
                markContactsDirty(true);
                ESP_LOGI(TAG, "DM: the direct attempt failed; the route to \"%s\" is cleared",
                         g_contacts.c[ci].name);
            }
            path_len = g_contacts.c[ci].out_path_len;
            const size_t bytes = path_len == kOutPathUnknown
                                     ? 0
                                     : (size_t)(path_len & 63) * (size_t)((path_len >> 6) + 1);
            if (bytes > 0) memcpy(path, g_contacts.c[ci].out_path, bytes);
        }
    }

    uint8_t secret[meshcore::kSecretBytes];
    if (!meshcore::identitySharedSecret(g_id, pub, secret)) {
        xSemaphoreTake(g_dm_lock, portMAX_DELAY);
        meshcore::dmPendingDrop(g_dm_pending, due.msg_id);
        xSemaphoreGive(g_dm_lock);
        return false;
    }
    meshcore::DmComposed c;
    const uint8_t        attempt = due.attempt;
    if (!meshcore::dmCompose(g_id, pub, secret, ts, attempt, text, path, due.flood ? kOutPathUnknown : path_len,
                             &c)) {
        ESP_LOGW(TAG, "DM: retry %u could not be composed", (unsigned)attempt);
        return false;
    }

    const uint32_t airtime = meshcore::dmAirtimeMillis(c.frame_len);
    const uint32_t timeout = c.flood ? meshcore::dmFloodTimeoutMillis(airtime)
                                     : meshcore::dmDirectTimeoutMillis(airtime, path_len);
    // Rearm BEFORE queueing: the reply to this attempt can arrive before queueFrame() has even returned.
    xSemaphoreTake(g_dm_lock, portMAX_DELAY);
    meshcore::dmPendingRearm(g_dm_pending, due.msg_id, c.expected_ack, timeout, !c.flood, monotonicMs());
    xSemaphoreGive(g_dm_lock);

    if (!queueFrame(c.frame, c.frame_len)) {
        xSemaphoreTake(g_dm_lock, portMAX_DELAY);
        meshcore::dmPendingDelay(g_dm_pending, due.msg_id, 300, monotonicMs());
        xSemaphoreGive(g_dm_lock);
        ESP_LOGW(TAG, "DM: retry %u could not be queued; trying again shortly", (unsigned)attempt);
        return false;
    }
    // S515/EST-462: the retry is the frame the air now carries, so it is also the one whose echo counts. The
    // message's own facts move with it (length, route) rather than keeping the first attempt's.
    {
        StoreLock l;
        if (DmMessage* m = findDmBySeq(due.msg_id)) {
            m->direct     = !c.flood;
            m->hops_known = !c.flood;
            m->hops       = 0;
            m->hash_size  = 0;
            m->frame_len  = c.frame_len <= 0xFFFF ? (uint16_t)c.frame_len : 0;
            m->route      = c.flood ? RouteType::Flood : RouteType::Direct;
            m->frame_fp   = outgoingFingerprint(c.frame, c.frame_len);
        }
    }
    ESP_LOGI(TAG, "DM: retry %u queued for message %u (%u B, %s)", (unsigned)attempt, (unsigned)due.msg_id,
             (unsigned)c.frame_len, c.flood ? "flood" : "direct");
    return true;
}

AdvertDecision queueAdvert() {
    AdvertDecision d;
    if (g_tx_lock == nullptr) {
        d.verdict = AdvertVerdict::Busy;   // the link is not up yet: nothing can be queued
        return d;
    }
    bool refused_by_queue_full = false;
    xSemaphoreTake(g_tx_lock, portMAX_DELAY);
    d = advertRequest(g_advert_gate, monotonicMs());
    if (d.verdict == AdvertVerdict::Send) {
        if (g_txq.pushAdvert(g_node_name)) {
            advertQueued(g_advert_gate);
        } else {
            // The queue is full. The advert was NOT queued, so the gate must not say one is in flight.
            d.verdict             = AdvertVerdict::Busy;
            refused_by_queue_full = true;
            ESP_LOGW(TAG, "tx: the send queue is full; the advert was NOT queued");
        }
    }
    xSemaphoreGive(g_tx_lock);
    if (d.verdict == AdvertVerdict::Wait) {
        ESP_LOGI(TAG, "advert: refused, %u s left of the %u s gap", (unsigned)d.wait_s,
                 (unsigned)(kAdvertGapMs / 1000));
    } else if (d.verdict == AdvertVerdict::Busy && !refused_by_queue_full) {
        ESP_LOGI(TAG, "advert: refused, one is still queued");
    }
    return d;
}

void serviceRequests() {
    if (g_tx_lock == nullptr) return;

    // S514/EST-446: the main task committed a new channel table; this is the one owner that reloads the
    // live table (and therefore the radio's view of which hashes it can open and which keys it sends with).
    if (g_channels_reload) {
        g_channels_reload = false;
        loadChannels();
        ++g_channels_rev;
        ESP_LOGI(TAG, "channels: reloaded from NVS, %u channel(s)", (unsigned)g_set.count);
    }

    // The schedule first: an advert at start and every g_advint_s (NVS mcid/advint). This is what makes the node
    // discoverable on its own; the 'a' key only forces one early. It goes to the BACK of the queue and only when
    // nothing the user typed is waiting — a scheduled advert must never displace, or delay, a message.
    // S513/EST-435: it asks the SAME 5 s rule as the key, so a manual advert two seconds ago also defers the
    // schedule. A refusal here is silent on purpose: nothing was pressed, so there is nothing to tell.
    // S514 (Claude, Filip: "disable the auto-advert on start-up"): esp_timer counts from boot, so 0 here means
    // "measure from boot": the first scheduled advert goes out one interval after start, never at boot. The
    // decision is the host-tested core rule (core/settings.h advertScheduleDue), not a second copy of it.
    static int64_t s_last_advert_us = 0;
    static bool    s_ever_sched     = false;
    if (g_has_id) {
        const int64_t  since_us = esp_timer_get_time() - s_last_advert_us;
        const uint32_t since_s  = since_us > 0 ? (uint32_t)(since_us / 1000000LL) : 0u;
        if (tm::advertScheduleDue(g_advint_s, since_s, s_ever_sched)) {
            xSemaphoreTake(g_tx_lock, portMAX_DELAY);
            if (advertRequest(g_advert_gate, monotonicMs()).verdict == AdvertVerdict::Send && !g_txq.hasText() &&
                g_txq.pushAdvert(g_node_name)) {
                advertQueued(g_advert_gate);
                s_last_advert_us = esp_timer_get_time();
                s_ever_sched     = true;
            }
            xSemaphoreGive(g_tx_lock);
        }
    }

    // S514/EST-456: the two deadlines (the L2's 8 s ping / 12 s trace) expire on the radio task's own clock.
    {
        StoreLock l;
        const ops::PingState before = g_ping.state;
        ops::pingTick(g_ping, monotonicMs());
        if (before == ops::PingState::Sent && g_ping.state == ops::PingState::Timeout)
            ESP_LOGI(TAG, "ping: no reply");
        const bool was_pending = g_trace_job.pending;
        ops::traceTick(g_trace_job, monotonicMs());
        if (was_pending && !g_trace_job.pending) {
            g_trace_pending = false;
            ++g_trace_serial;
            ESP_LOGI(TAG, "trace: no data back");
        }
    }

    // One request per loop, so a burst cannot hold the radio task away from receiving.
    TxRequest req;
    xSemaphoreTake(g_tx_lock, portMAX_DELAY);
    const bool   have = g_txq.pop(&req);
    const size_t left = g_txq.size();
    xSemaphoreGive(g_tx_lock);
    if (!have) return;
    if (left > 0) ESP_LOGI(TAG, "tx: %u more request(s) queued", (unsigned)left);

    if (req.kind == TxKind::Text) {
        doText((size_t)req.channel, req.sender, req.text);   // a COPY of what the main task queued
    } else if (req.kind == TxKind::Advert) {
        doAdvert(req.sender);   // resolves the 5 s gate on every path, including "never got on the air"
    } else {
        // A reply built on the radio task (DM ACK / path return): this is the only place it is transmitted.
        const int sent = om::radio::send(req.frame, req.frame_len);
        ESP_LOGI(TAG, "TX reply %u B -> %s", (unsigned)req.frame_len, sent > 0 ? "ok" : "FAILED");
    }
    // Evidence, not a guess: how much stack this task really needed for the work it just did.
    ESP_LOGI(TAG, "radio task stack: %u B free at the low-water mark",
             (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
}

// --------------------------------------------------------------------------- S514/EST-446 channel admin
// The main task writes NVS; the radio task reloads. Every function logs the channel NAME and never a key.
bool channelsAddHashtag(const char* typed, char* out_name, size_t cap) {
    char tag[meshcore::kChannelTagMax + 1];
    const int n = meshcore::channelTagCanonical(typed, tag, sizeof(tag));
    if (n < 0) {
        ESP_LOGW(TAG, "channels: \"%s\" is not a hashtag", typed != nullptr ? typed : "");
        return false;
    }
    uint8_t key[meshcore::kKeyBytes];
    if (!meshcore::channelTagKey(tag, key)) return false;
    if (g_set.count >= meshcore::kMaxChannels) {
        ESP_LOGW(TAG, "channels: table full (%u), %s not added", (unsigned)meshcore::kMaxChannels, tag);
        return false;
    }
    if (meshcore::channelKeyKnown(g_set, key)) {
        ESP_LOGW(TAG, "channels: %s is already in the table", tag);
        return false;
    }
    meshcore::ChannelSet after = g_set;
    if (!meshcore::channelsAdd(after, key, tag)) return false;
    meshcore::ChannelNvsStep plan[meshcore::kChannelNvsMaxSteps];
    const int steps = meshcore::channelRewritePlan(g_set, after, plan, meshcore::kChannelNvsMaxSteps);
    if (!channelApplyPlan(after, plan, steps)) return false;
    if (out_name != nullptr && cap > 0) snprintf(out_name, cap, "%s", tag);
    ESP_LOGI(TAG, "channels: added %s (hash %02x), %u step(s); the radio task will reload", tag,
             (unsigned)after.ch[after.count - 1].hash, (unsigned)steps);
    return true;
}

bool channelsAddByKey(const char* name, const char* key_b64) {
    char label[meshcore::kChannelLabelMax + 1];
    if (name != nullptr) snprintf(label, sizeof(label), "%s", name);
    else label[0] = 0;
    if (!meshcore::channelLabelValid(label)) {
        ESP_LOGW(TAG, "channels: \"%s\" is not a usable channel name", label);
        return false;
    }
    uint8_t key[meshcore::kKeyBytes];
    if (!meshcore::channelKeyFromBase64(key_b64, key)) {
        ESP_LOGW(TAG, "channels: %s: the pasted key is not a 16-byte base64 channel key", label);
        return false;
    }
    if (g_set.count >= meshcore::kMaxChannels) {
        ESP_LOGW(TAG, "channels: table full (%u), %s not added", (unsigned)meshcore::kMaxChannels, label);
        return false;
    }
    if (meshcore::channelKeyKnown(g_set, key)) {
        ESP_LOGW(TAG, "channels: %s: that key is already in the table", label);
        return false;
    }
    meshcore::ChannelSet after = g_set;
    if (!meshcore::channelsAdd(after, key, label)) return false;
    meshcore::ChannelNvsStep plan[meshcore::kChannelNvsMaxSteps];
    const int steps = meshcore::channelRewritePlan(g_set, after, plan, meshcore::kChannelNvsMaxSteps);
    if (!channelApplyPlan(after, plan, steps)) return false;
    ESP_LOGI(TAG, "channels: added %s by key (hash %02x), %u step(s)", label,
             (unsigned)after.ch[after.count - 1].hash, (unsigned)steps);
    return true;
}

bool channelsRename(size_t i, const char* name) {
    if (i >= g_set.count) return false;
    if (!meshcore::channelLabelValid(name)) return false;
    meshcore::ChannelSet after = g_set;
    snprintf(after.ch[i].name, sizeof(after.ch[i].name), "%s", name);
    meshcore::ChannelNvsStep plan[meshcore::kChannelNvsMaxSteps];
    const int steps = meshcore::channelRewritePlan(g_set, after, plan, meshcore::kChannelNvsMaxSteps);
    if (!channelApplyPlan(after, plan, steps)) return false;
    ESP_LOGI(TAG, "channels: slot %u renamed to \"%s\"", (unsigned)i, name);
    return true;
}

// Index 0 is removable like any other (the ticket says so): the table compacts, and the removed channel's
// scope record is erased only when no surviving channel carries the same 1-byte hash.
bool channelsRemove(size_t i) {
    if (i >= g_set.count) return false;
    char name[32];
    snprintf(name, sizeof(name), "%s", g_set.ch[i].name);
    const meshcore::ChannelSet after = setWithout(g_set, (int)i);
    meshcore::ChannelNvsStep plan[meshcore::kChannelNvsMaxSteps];
    const int steps = meshcore::channelRewritePlan(g_set, after, plan, meshcore::kChannelNvsMaxSteps);
    if (!channelApplyPlan(after, plan, steps)) return false;
    ESP_LOGI(TAG, "channels: removed \"%s\" (slot %u), %u step(s); its history stays on the card", name,
             (unsigned)i, (unsigned)steps);
    return true;
}

// The 16 bytes a "share" shows on screen (base64). Never logged, never stored anywhere else.
bool channelsKeyCopy(size_t i, uint8_t out[meshcore::kKeyBytes]) {
    if (out == nullptr || i >= g_set.count) return false;
    memcpy(out, g_set.ch[i].secret, meshcore::kKeyBytes);
    return true;
}

void channelsRequestReload() { g_channels_reload = true; }

uint32_t channelsRevision() { return g_channels_rev; }

const char* publicKeyHex() { return g_pub_hex; }

const char* nodeName() { return g_node_name; }

// EST-445: store a name the UI has already accepted (core/settings.h::nodeNameApply). This re-checks it too:
// a caller bug must not put a forbidden name on the air, and the live name only moves when NVS accepted it.
bool setNodeName(const char* name) {
    char checked[tm::kNodeNameMaxBytes + 1];
    if (!tm::nodeNameApply(name, checked, sizeof(checked))) return false;
    nvs_handle_t h;
    if (nvs_open("mcid", NVS_READWRITE, &h) != ESP_OK) return false;
    const esp_err_t r = nvs_set_str(h, "name", checked);
    if (r == ESP_OK) nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "node name: NVS write failed (%s); the old name stays", esp_err_to_name(r));
        return false;
    }
    // The radio task copies the name into each queued request under g_tx_lock; take the same lock for the
    // live copy, so a rename can never be read half-written. (The panel reads it on this same task.)
    if (g_tx_lock != nullptr) xSemaphoreTake(g_tx_lock, portMAX_DELAY);
    snprintf(g_node_name, sizeof(g_node_name), "%s", checked);
    if (g_tx_lock != nullptr) xSemaphoreGive(g_tx_lock);
    ESP_LOGI(TAG, "node name: -> \"%s\" (mcid/name)", g_node_name);
    return true;
}

// EST-445: the automatic advert interval (seconds; 0 = Off), as the General tab shows/cycles it. The schedule
// reads g_advint_s every loop, so this takes effect without a restart.
uint32_t advertIntervalSeconds() { return g_advint_s; }

bool setAdvertIntervalSeconds(uint32_t seconds) {
    if (!tm::advertIntervalStoredValid(seconds)) return false;
    nvs_handle_t h;
    if (nvs_open("mcid", NVS_READWRITE, &h) != ESP_OK) return false;
    const esp_err_t r = nvs_set_u32(h, "advint", seconds);
    if (r == ESP_OK) nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "adverts: interval write failed (%s); the old interval stays", esp_err_to_name(r));
        return false;
    }
    g_advint_s = seconds;
    ESP_LOGI(TAG, "adverts: automatic interval -> %u s (%s)", (unsigned)seconds,
             seconds == 0 ? "off" : "scheduled");
    return true;
}

ContactStore& contacts() { return g_contacts; }
MessageRing&  messages() { return g_messages; }
ReadState&    readState() { return g_read; }
DmRing&       dmThreads() { return g_dms; }

StoreLock::StoreLock() {
    if (g_store_mutex != nullptr) xSemaphoreTakeRecursive(g_store_mutex, portMAX_DELAY);
}
StoreLock::~StoreLock() {
    if (g_store_mutex != nullptr) xSemaphoreGiveRecursive(g_store_mutex);
}

const char* statusText() {
    static char buf[24];
    snprintf(buf, sizeof(buf), "mc %uch", (unsigned)g_set.count);
    return buf;
}

// The whole of "the user has now seen this channel": mark it read in the store, and move the on-card seen point
// to the newest message that is actually on the card. Called on open, on the two-second refresh while open, and
// on the way up (Esc). The Meta record is written only when the point MOVED, so an open channel costs nothing
// extra while you read it.
//
// The card is a slow writer compared to a redraw, so this runs under the store lock like every other write, and
// only when there is something to record.
void noteChannelRead(uint8_t ch) {
    if (ch >= g_set.count) return;
    StoreLock l;
    markChannelRead(g_messages, g_read, ch);

    uint32_t newest = 0;
    for (size_t i = 0; i < g_messages.count; i++) {
        const Message* m = messagesAt(g_messages, i);
        if (m != nullptr && m->channel == ch && m->log_id > newest) newest = m->log_id;
    }
    if (newest == 0 || newest == g_seen_written[ch]) return;
    if (sdhistory::appendSeen(ch, g_set.ch[ch].hash, g_set.ch[ch].name, newest, 0)) {
        g_seen_written[ch] = newest;
        ESP_LOGI(TAG, "ch%u: seen point -> log id %u", (unsigned)ch, (unsigned)newest);
    }
}

// Item 4: restore the DM logs from the card. Runs on the main task after sdhistory::begin() and before the radio
// task exists, so no lock is needed for the store itself; StoreLock is still used because the pages may read.
size_t restoreDmHistory() {
    StoreLock l;
    const size_t n = sdhistory::restoreDmThreads(g_dms, g_contacts);
    if (n > 0) ESP_LOGI(TAG, "DM history: %u thread(s) restored from the card", (unsigned)n);
    return n;
}

// "The user has now seen this DM thread": mark it read and move the seen point in that contact's log (the format's
// Meta record). Like a channel, the record is appended only when the point MOVED.
void noteDmRead(const uint8_t pub[32]) {
    if (pub == nullptr) return;
    StoreLock l;
    const int idx = contactsFind(g_contacts, pub);
    if (idx < 0) return;
    dmMarkPeerRead(g_dms, g_contacts, pub);
    const uint32_t newest = dmNewestLogId(g_dms, pub);
    if (newest == 0 || newest == g_contacts.c[idx].dm_seen_log) return;
    if (sdhistory::appendDmSeen(pub, newest, 0)) {
        g_contacts.c[idx].dm_seen_log = newest;
        ESP_LOGI(TAG, "DM: seen point -> log id %u", (unsigned)newest);
    }
}

// S514/EST-453: the RPTR page's two doors (see mc_link.h). Both are thin and honest: a frame goes through the
// SAME four-slot queue and the SAME radio task as a DM, and the identity is only handed out once begin() has one.
bool selfIdentity(meshcore::Identity* out) {
    if (out == nullptr || !g_has_id) return false;
    *out = g_id;
    return true;
}

bool queueRptrFrame(const uint8_t* frame, size_t len) { return queueFrame(frame, len); }

// ------------------------------------------------------- S514/EST-456: Ping and Trace (see mc_link.h)
// A copy of the contact under the lock: the frame is built from the SAME row the sheet named.
namespace {
bool contactByPub(const uint8_t pub[meshcore::kPubKeyBytes], Contact* out) {
    if (pub == nullptr || out == nullptr) return false;
    StoreLock l;
    const int i = contactsFind(g_contacts, pub);
    if (i < 0) return false;
    *out = g_contacts.c[i];
    return true;
}
}  // namespace

bool nodePing(const uint8_t pub[meshcore::kPubKeyBytes]) {
    if (!g_has_id || pub == nullptr) return false;
    Contact c;
    if (!contactByPub(pub, &c) || contactKeyIncomplete(c.pub)) return false;
    uint8_t secret[meshcore::kSecretBytes];
    if (!identitySharedSecret(g_id, c.pub, secret)) return false;
    uint8_t frame[kTxFrameBytes];
    const bool    known = contactHasOutPath(c);
    const uint8_t hops   = (uint8_t)(c.out_path_len & 63);
    const size_t  bytes  = known ? (size_t)hops * (size_t)((c.out_path_len >> 6) + 1) : 0;
    const uint32_t tag   = uniqueTag();
    const size_t   n = ops::buildPingFrame(g_id, c.pub, secret, tag, esp_random(), known ? c.out_path : nullptr,
                                           known ? c.out_path_len : kOutPathUnknown, frame, sizeof(frame));
    if (n == 0) return false;
    {
        StoreLock l;
        ops::pingStarted(g_ping, c.pub, c.name, tag, monotonicMs());
    }
    if (!queueFrame(frame, n)) {
        StoreLock l;
        ops::pingFailed(g_ping);
        return false;
    }
    ESP_LOGI(TAG, "ping \"%s\": status request queued (%u B, %s, tag %08lx)", c.name, (unsigned)n,
             known ? "direct" : "flood", (unsigned long)tag);
    (void)bytes;
    return true;
}

bool nodeTrace(const uint8_t pub[meshcore::kPubKeyBytes]) {
    if (!g_has_id || pub == nullptr) return false;
    Contact c;
    if (!contactByPub(pub, &c) || contactKeyIncomplete(c.pub)) return false;
    if (!contactHasOutPath(c)) return false;   // the row is absent on screen; a stale caller sends nothing
    const uint8_t hops      = (uint8_t)(c.out_path_len & 63);
    const uint8_t hash_size = (uint8_t)((c.out_path_len >> 6) + 1);
    const uint8_t path_bytes = (uint8_t)(hops * hash_size);
    uint8_t frame[kTxFrameBytes];
    const uint32_t tag = uniqueTag();
    const size_t   n = ops::buildTraceFrame(tag, 0, hash_size, c.out_path, path_bytes, frame, sizeof(frame));
    if (n == 0) return false;
    {
        StoreLock l;
        traceStarted(g_trace_job, c.name, monotonicMs());
        memcpy(g_trace_key, c.pub, sizeof(g_trace_key));
        g_trace_tag     = tag;
        g_trace_pending = true;
    }
    if (!queueFrame(frame, n)) {
        StoreLock l;
        ops::traceCancel(g_trace_job);
        g_trace_pending = false;
        g_trace_serial++;
        return false;
    }
    ESP_LOGI(TAG, "trace \"%s\": %u hop(s), %u B queued", c.name, (unsigned)hops, (unsigned)n);
    return true;
}

void nodePingJob(ops::PingJob* out) {
    if (out == nullptr) return;
    StoreLock l;
    *out = g_ping;
}

bool nodeTraceResult(ops::TraceView* out) {
    if (out == nullptr) return false;
    StoreLock l;
    *out = g_trace;
    return g_trace.haveRtt;
}

bool nodePingPendingFor(const uint8_t pub[meshcore::kPubKeyBytes]) {
    StoreLock l;
    return g_ping.state == ops::PingState::Sent && memcmp(g_ping.key, pub, meshcore::kPubKeyBytes) == 0;
}

bool nodeTracePendingFor(const uint8_t pub[meshcore::kPubKeyBytes]) {
    StoreLock l;
    return g_trace_pending && memcmp(g_trace_key, pub, meshcore::kPubKeyBytes) == 0;
}

void nodeOpsCancel() {
    StoreLock l;
    ops::pingCancel(g_ping);
    if (g_trace_pending) {
        g_trace_pending = false;
        g_trace_serial++;
    }
}

bool nodeOpsPoll() {
    StoreLock l;
    const uint8_t st = (uint8_t)g_ping.state;
    if (st != g_ping_seen_state) {
        g_ping_seen_state = st;
        if (g_ping.state == ops::PingState::Replied || g_ping.state == ops::PingState::Timeout ||
            g_ping.state == ops::PingState::Failed) {
            char text[64];
            ops::pingText(text, sizeof(text), g_ping);
            if (text[0] != '\0') om::ui::noticeSet(text, 3000);
        }
    }
    static uint32_t seen_serial = 0;
    if (g_trace_serial != seen_serial) {
        seen_serial = g_trace_serial;
        return true;
    }
    return false;
}

}  // namespace mc
}  // namespace om
