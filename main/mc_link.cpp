// SPDX-License-Identifier: MIT
#include "mc_link.h"

#include <stdio.h>
#include <string.h>
// No <time.h> here: chrome.h -> pages.h defines `namespace tm`, and <time.h> defines `struct tm` — the two cannot
// live in one translation unit (time_source.h says the same). The radio task asks time_source::epochNow() instead.

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"   // esp_timer_get_time(): the advert schedule
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"   // the store lock
#include "freertos/task.h"   // uxTaskGetStackHighWaterMark: the stack evidence after each request   // esp_fill_random() for the one-time identity seed
#include "nvs.h"

#include "advert_rule.h"   // S513/EST-435: one advert per 5 s, host-tested
#include "contact_blob.h"  // S513/EST-437: the contact table as one versioned NVS blob
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
uint32_t g_advint_s = 3u * 60u * 60u;

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
constexpr uint32_t    kContactsRetryMs      = 10u * 1000u;         // after a failed NVS write

bool     g_contacts_dirty_struct = false;
bool     g_contacts_dirty_soft   = false;
bool     g_contacts_write_failed = false;
uint32_t g_contacts_last_write_ms  = 0;
uint32_t g_contacts_last_attempt_ms = 0;
uint8_t  g_contact_blob[kContactBlobMaxBytes];   // restore reads it, the main task encodes it

// Caller holds the store lock (or is the single main-task reader before the radio task exists).
void markContactsDirty(bool structural) {
    if (structural) g_contacts_dirty_struct = true;
    else g_contacts_dirty_soft = true;
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

// S513/EST-437: the contact table's one NVS blob. Read at boot before the radio task exists; a wrong blob is
// ignored whole (decode writes into a scratch table), so a damaged namespace cannot half-fill the Nodes list.
size_t restoreContacts() {
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
    g_contacts_write_failed = false;
    if (dropped > 0) {
        ESP_LOGW(TAG, "contacts: dropped %u record(s) without a full key", (unsigned)dropped);
    }
    ESP_LOGI(TAG, "contacts: %u restored from NVS", (unsigned)g_contacts.count);
    return g_contacts.count;
}

// S513/EST-437: the MAIN task's write. The radio task only raises a flag; the encode and the NVS write are here.
void contactsPersistTick() {
    if (g_store_mutex == nullptr) return;   // begin() has not run yet
    const uint32_t now = monotonicMs();

    bool     structural = false;
    bool     soft       = false;
    size_t   bytes      = 0;
    uint32_t count      = 0;
    {
        StoreLock l;
        structural = g_contacts_dirty_struct;
        soft       = g_contacts_dirty_soft;
        if (!structural && !soft) return;
        // A soft-only change waits for the floor; a structural one goes on this pass. The failure retry is its own
        // 10 s clock, so a broken NVS does not turn into a write storm from the UI task.
        if (!structural && g_contacts_last_write_ms != 0 &&
            (uint32_t)(now - g_contacts_last_write_ms) < kContactsSoftWriteMs) {
            return;
        }
        if (g_contacts_write_failed && g_contacts_last_attempt_ms != 0 &&
            (uint32_t)(now - g_contacts_last_attempt_ms) < kContactsRetryMs) {
            return;
        }
        bytes = contactsEncode(g_contacts, g_contact_blob, sizeof(g_contact_blob));
        if (bytes == 0) {
            // The table is over kMaxContacts or the buffer is wrong; drop the flags rather than retry for ever.
            ESP_LOGE(TAG, "contacts: encode failed (%u rows), not written", (unsigned)g_contacts.count);
            g_contacts_dirty_struct = false;
            g_contacts_dirty_soft   = false;
            return;
        }
        count = (uint32_t)g_contacts.count;
        // Cleared BEFORE the lock is released: a mutation that lands while NVS is written sets a flag again.
        g_contacts_dirty_struct = false;
        g_contacts_dirty_soft   = false;
    }

    nvs_handle_t h;
    esp_err_t    r = nvs_open(kContactsNvsNamespace, NVS_READWRITE, &h);
    if (r == ESP_OK) {
        r = nvs_set_blob(h, kContactsNvsKey, g_contact_blob, bytes);
        if (r == ESP_OK) r = nvs_commit(h);
        nvs_close(h);
    }
    g_contacts_last_attempt_ms = now;
    if (r == ESP_OK) {
        g_contacts_last_write_ms = now;
        g_contacts_write_failed  = false;
        ESP_LOGI(TAG, "contacts: %u stored to NVS (%u B)", (unsigned)count, (unsigned)bytes);
    } else {
        g_contacts_write_failed = true;
        ESP_LOGE(TAG, "contacts: NVS write failed (%s); retrying in %u s", esp_err_to_name(r),
                 (unsigned)(kContactsRetryMs / 1000));
        StoreLock l;
        if (structural) g_contacts_dirty_struct = true;
        else g_contacts_dirty_soft = true;
    }
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

void begin() {
    if (g_store_mutex == nullptr) g_store_mutex = xSemaphoreCreateRecursiveMutex();
    if (g_tx_lock == nullptr) g_tx_lock = xSemaphoreCreateMutex();   // S513: the send queue, created once
    if (g_dm_lock == nullptr) g_dm_lock = xSemaphoreCreateMutex();   // S513/EST-439: the pending DM table
    // S513/EST-437: the last-heard/SNR floor starts at boot, so the first soft change after a restart waits its
    // 10 minutes like every other one. Add/rename/route changes are not affected: those write on the next pass.
    g_contacts_last_write_ms = monotonicMs();
    loadOrCreateIdentity();
    loadNodeName();          // EST-445: mcid/name, default "TNMT (SI)"
    loadAdvertInterval();    // EST-445: mcid/advint, default 3 h
    loadNewNodeNotice();     // S514/EST-447: mcset/newnode, default On
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
        const bool locked = g_tx_lock != nullptr;
        if (locked) xSemaphoreTake(g_tx_lock, portMAX_DELAY);
        seen = g_seen.seen(meshcore::frameFingerprint(v), &ours);
        if (locked) xSemaphoreGive(g_tx_lock);
        if (seen) {
            ESP_LOGI(TAG, "rx %u B type %u: %s, dropped", (unsigned)len, (unsigned)v.type(),
                     ours ? "our own echo" : "duplicate, already shown");
            return;
        }
    }
    // ------------------------------------------------- S513/EST-439: the PATH answer and the delivered ACK
    // A PATH packet is the recipient's answer to a flooded DM: it carries the route back TO that contact and, when
    // its extra type is ACK, the delivered mark for the message we sent (Mesh.cpp:160-180). Opening it is the
    // only way a route is ever learned, and storing it is a STRUCTURAL change to the contact table.
    if (v.type() == meshcore::kTypePath) {
        if (!g_has_id) {
            ESP_LOGW(TAG, "rx %u B PATH but this node has no identity yet, dropped", (unsigned)len);
            return;
        }
        static uint8_t peer_pubs[kMaxContacts * meshcore::kPubKeyBytes];
        const size_t   peer_count = snapshotContactPubs(peer_pubs, kMaxContacts);
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
        if (!g_has_id) {
            ESP_LOGW(TAG, "rx %u B TXT_MSG but this node has no identity yet, dropped", (unsigned)len);
            return;
        }
        // A snapshot of the contact table, because dmOpen walks it and two contacts may share pub_key[0]. The
        // arrays are static: the radio task is the only caller, and 32 contacts must not land on its stack.
        static uint8_t peer_pubs[kMaxContacts * meshcore::kPubKeyBytes];
        static char    peer_names[kMaxContacts][kNameBytes];
        size_t         peer_count = 0;
        {
            StoreLock l;
            peer_count = g_contacts.count;
            for (size_t i = 0; i < peer_count; i++) {
                memcpy(&peer_pubs[i * meshcore::kPubKeyBytes], g_contacts.c[i].pub, meshcore::kPubKeyBytes);
                snprintf(peer_names[i], sizeof(peer_names[i]), "%s", g_contacts.c[i].name);
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

        const uint8_t* peer_pub = &peer_pubs[(size_t)dm.peer * meshcore::kPubKeyBytes];
        char           peer_name[kNameBytes];
        snprintf(peer_name, sizeof(peer_name), "%s", peer_names[dm.peer]);
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
            m.direct   = (v.path_bytes == 0);
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
            }
            (void)dmPush(g_dms, m);
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
        // type != nullptr: keep EVERY verified node type here (repeaters included), not only chat nodes — the
        // Nodes page is a list of what the radio can hear.
        if (!meshcore::advertParse(v, pub, &ts, name, sizeof(name), &type)) {
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
            const int      idx =
                contactsUpsert(g_contacts, pub, name, type, v.path_bytes, ts, heard, rssi_dbm, snr_db * 10);
            if (idx >= 0) {
                // Add / rename / route change is worth an immediate NVS write; a repeat advert that only moves
                // last-heard and SNR waits for the 10-minute floor.
                const bool structural = before < 0 || strcmp(old_name, g_contacts.c[idx].name) != 0 ||
                                        old_path != g_contacts.c[idx].path_len ||
                                        old_type != g_contacts.c[idx].type;
                markContactsDirty(structural);
                if (before < 0 && g_new_node_notice) {
                    char text[64];
                    snprintf(text, sizeof(text), "New node: %s (%s)", name[0] != '\0' ? name : "?",
                             advertTypeName(type));
                    om::ui::noticeSet(text, 3000);
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
        m.direct    = (v.path_bytes == 0);
        // A message longer than the field is CUT, not dropped: the field widths are the UI's, not MeshCore's.
        snprintf(m.sender, sizeof(m.sender), "%.*s", (int)(sizeof(m.sender) - 1), sender);
        snprintf(m.text, sizeof(m.text), "%.*s", (int)(sizeof(m.text) - 1), text);
        const uint32_t seq = pushAndLog((size_t)idx, m);
        (void)seq;
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
        // is already accounted for: the frame was registered above, and RX drops it as our own.
        StoreLock l;
        Message   m;
        m.channel  = (uint8_t)i;
        m.outgoing = true;
        m.ts       = ts;
        m.local_ts = ts;
        m.direct   = true;
        m.snr_x10  = 0;
        snprintf(m.sender, sizeof(m.sender), "%.*s", (int)(sizeof(m.sender) - 1), sender);
        snprintf(m.text, sizeof(m.text), "%.*s", (int)(sizeof(m.text) - 1), text);
        const uint32_t seq = pushAndLog(i, m);
        (void)seq;
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
        {
            StoreLock l;
            DmMessage* m = findDmBySeq(due.msg_id);
            if (m != nullptr) {
                m->delivery = om::ui::DmState::NotConfirmed;
                changed      = true;
                if (m->log_id != 0) {
                    (void)sdhistory::appendDmState(m->peer_pub, m->log_id, m->delivery,
                                                   (uint32_t)om::time_source::epochNow());
                }
                ESP_LOGI(TAG, "DM: message %u has NO CONFIRMATION after %u attempts", (unsigned)due.msg_id,
                         (unsigned)meshcore::kDmMaxAttempts);
            }
        }
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
    static int64_t s_last_advert_us = 0;
    // Off (0) means NO scheduled advert at all; the manual paths still work. `s_last_advert_us == 0` is the
    // first pass after a boot, so a non-Off interval announces immediately, as before.
    if (g_has_id && g_advint_s > 0) {
        const int64_t interval_us = (int64_t)g_advint_s * 1000000LL;
        if (s_last_advert_us == 0 || esp_timer_get_time() - s_last_advert_us >= interval_us) {
            xSemaphoreTake(g_tx_lock, portMAX_DELAY);
            if (advertRequest(g_advert_gate, monotonicMs()).verdict == AdvertVerdict::Send && !g_txq.hasText() &&
                g_txq.pushAdvert(g_node_name)) {
                advertQueued(g_advert_gate);
                s_last_advert_us = esp_timer_get_time();
            }
            xSemaphoreGive(g_tx_lock);
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

}  // namespace mc
}  // namespace om
