// SPDX-License-Identifier: MIT
#include "mc_link.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"   // esp_timer_get_time(): the advert schedule
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"   // the store lock
#include "freertos/task.h"   // uxTaskGetStackHighWaterMark: the stack evidence after each request   // esp_fill_random() for the one-time identity seed
#include "nvs.h"

#include "mc_channels.h"
#include "mc_identity.h"
#include "mc_crypto.h"
#include "mc_wire.h"
#include "radio_link.h"
#include "sd_history.h"   // step 5: the chat history on the card
#include "chat_layout.h"  // step 5 / S503: historyReadCount, the read-prefix rule for the seen point
#include "time_source.h"
#include "clock.h"

namespace om {
namespace mc {
namespace {

const char* TAG = "tm/mc";

// ONE home for the node's name (Filip, S501: renamed from the placeholder "EDCT" to "TNMT (SI)"). MeshCore
// forbids [ ] \ : , ? * inside an advert name; a space and parentheses are fine (AdvertDataHelpers.cpp:31-34).
const char kNodeName[] = "TNMT (SI)";

// MeshCore announces a node on a schedule; a flood advert is expensive on a shared mesh, so it is rare.
// Ours: one as soon as the radio is up (so the node is discoverable without anyone pressing a key), then
// every 3 hours. The test hook ('a') stays for a deliberate re-announce.
constexpr int64_t kAdvertIntervalUs = 3 * 60 * 60 * 1000000LL;

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
    const uint32_t now = (uint32_t)time(nullptr);
    const uint32_t ts  = om::clock::advertFloor(now, floor_ts);
    if (nvs_open("mcid", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_u32(h, "advts", ts) == ESP_OK) nvs_commit(h);
        nvs_close(h);
    }
    return ts;
}

meshcore::ChannelSet g_set;
bool                 g_loaded = false;

// The store the pages read (S502, step 4). The radio task writes it, the UI reads it; the mutex is created in
// begin() and the guard below is the ONLY way in.
ContactStore    g_contacts;
MessageRing     g_messages;
ReadState       g_read;
SemaphoreHandle_t g_store_mutex = nullptr;

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
enum class Pending { None, Text, Advert };
Pending g_pending         = Pending::None;
size_t  g_pending_channel = 0;
char    g_pending_sender[32] = {0};
char    g_pending_text[200]  = {0};
char    g_pending_name[32]   = {0};

// Duplicate suppression: a flood can arrive over two paths, and MeshCore itself keeps a seen-table for the
// same reason. Eight 4-byte fingerprints is plenty for one leaf node and costs 32 bytes.
constexpr size_t kSeenSlots = 8;
uint32_t         g_seen[kSeenSlots];
uint8_t          g_seen_next = 0;

uint32_t payloadFingerprint(const meshcore::View& v) {
    uint8_t full[32];
    if (!meshcore::sha256(v.payload, v.payload_len, full)) return 0;
    return (uint32_t)full[0] | ((uint32_t)full[1] << 8) | ((uint32_t)full[2] << 16) |
           ((uint32_t)full[3] << 24);
}

bool seenBefore(uint32_t fp) {
    for (size_t i = 0; i < kSeenSlots; i++) {
        if (g_seen[i] == fp && fp != 0) return true;
    }
    g_seen[g_seen_next] = fp;
    g_seen_next         = (uint8_t)((g_seen_next + 1) % kSeenSlots);
    return false;
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

}  // namespace

void begin() {
    if (g_store_mutex == nullptr) g_store_mutex = xSemaphoreCreateRecursiveMutex();
    loadOrCreateIdentity();
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

void onPacket(const uint8_t* buf, size_t len, int rssi_dbm, int snr_db) {
    meshcore::View v;
    if (!meshcore::decode(buf, len, &v)) {
        ESP_LOGW(TAG, "rx %u B: not a MeshCore frame", (unsigned)len);
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
        // A verified advert is a verified clock: this is the app's own way out of a 1970 RTC without Wi-Fi.
        om::time_source::noteSeen(ts, "advert");
        {
            StoreLock l;
            contactsUpsert(g_contacts, pub, name, type, v.path_bytes, ts, (uint32_t)time(nullptr), rssi_dbm,
                           snr_db * 10);
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
    if (seenBefore(payloadFingerprint(v))) {
        ESP_LOGI(TAG, "rx %u B on ch%u: duplicate, already shown", (unsigned)len, (unsigned)idx);
        return;
    }
    ESP_LOGI(TAG, "MSG ch%u \"%s\" from \"%s\": %s  (ts %u, %d dBm, snr %d dB, %u B)",
             (unsigned)idx, g_set.ch[idx].name, sender, text, (unsigned)ts, rssi_dbm, snr_db,
             (unsigned)len);
    // The ts is only a time if the MAC that opened it verified, which it just did: this frame is a bonafide
    // holder of the channel key. Adopt it like an advert's (core/clock.h decides whether it is worth it).
    om::time_source::noteSeen(ts, "text");
    {
        StoreLock l;
        Message m;
        m.channel   = (uint8_t)idx;
        m.outgoing  = false;
        m.ts        = ts;
        m.local_ts  = (uint32_t)time(nullptr);
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
    // floor if that was empty, and a peer's authenticated timestamp once one is heard. Logged, so a wrong one
    // is visible rather than silent.
    const uint32_t ts = (uint32_t)time(nullptr);

    uint8_t      frame[256];
    const size_t n = meshcore::buildGroupText(g_set.ch[i], ts, sender, text, 2, frame, sizeof(frame));
    if (n == 0) {
        ESP_LOGE(TAG, "tx: build failed (text too long? %u chars)", (unsigned)strlen(text));
        return -1;
    }
    const int sent = om::radio::send(frame, n);
    ESP_LOGI(TAG, "TX ch%u \"%s\" %u B ts %u -> %s", (unsigned)i, g_set.ch[i].name, (unsigned)n,
             (unsigned)ts, sent > 0 ? "ok" : "FAILED");
    if (sent > 0) {
        // Two things, both about our own message coming back to us over the mesh:
        //   1. register the payload in the seen-ring BEFORE the echo arrives, so the flood copy is dropped as a
        //      duplicate — otherwise every message shows twice, once as "you" and once under our node's name;
        //   2. keep the outgoing line in the store ourselves, so the chat has it the instant Enter is pressed.
        meshcore::View self;
        if (meshcore::decode(frame, n, &self)) (void)seenBefore(payloadFingerprint(self));
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
    if (!g_has_id) {
        ESP_LOGE(TAG, "advert: no identity (see the identity lines above)");
        return -1;
    }
    if (name == nullptr || name[0] == '\0') return -1;
    const uint32_t ts = nextAdvertTimestamp();
    uint8_t        frame[256];
    const size_t   n = meshcore::advertBuild(g_id, ts, name, 2, frame, sizeof(frame));
    if (n == 0) {
        ESP_LOGE(TAG, "advert: build failed (name too long?)");
        return -1;
    }
    const int sent = om::radio::send(frame, n);
    ESP_LOGI(TAG, "ADVERT TX \"%s\" %u B ts %u -> %s", name, (unsigned)n, (unsigned)ts,
             sent > 0 ? "ok" : "FAILED");
    return sent > 0 ? (int)n : -1;
}

bool queueText(size_t i, const char* text) {
    if (i >= g_set.count || text == nullptr) return false;
    if (g_pending != Pending::None) {
        ESP_LOGW(TAG, "tx: a %s request was still pending; it is replaced",
                 g_pending == Pending::Text ? "text" : "advert");
    }
    g_pending_channel = i;
    snprintf(g_pending_sender, sizeof(g_pending_sender), "%s", kNodeName);
    snprintf(g_pending_text, sizeof(g_pending_text), "%s", text);
    g_pending = Pending::Text;
    return true;
}

bool queueAdvert() {
    if (g_pending != Pending::None) {
        ESP_LOGW(TAG, "tx: a %s request was still pending; it is replaced",
                 g_pending == Pending::Text ? "text" : "advert");
    }
    snprintf(g_pending_name, sizeof(g_pending_name), "%s", kNodeName);
    g_pending = Pending::Advert;
    return true;
}

void serviceRequests() {
    // The schedule first: an advert at start and every kAdvertIntervalUs. This is what makes the node
    // discoverable on its own; the 'a' key only forces one early.
    static int64_t s_last_advert_us = 0;
    if (g_has_id && (s_last_advert_us == 0 || esp_timer_get_time() - s_last_advert_us >= kAdvertIntervalUs)) {
        if (g_pending == Pending::None) {
            g_pending = Pending::Advert;   // a queued request (a message) takes priority: it is not replaced
            snprintf(g_pending_name, sizeof(g_pending_name), "%s", kNodeName);
        }
    }

    const Pending what = g_pending;
    if (what == Pending::None) return;
    g_pending = Pending::None;
    if (what == Pending::Advert) s_last_advert_us = esp_timer_get_time();
    if (what == Pending::Text) {
        doText(g_pending_channel, g_pending_sender, g_pending_text);
    } else {
        doAdvert(g_pending_name);
    }
    // Evidence, not a guess: how much stack this task really needed for the work it just did.
    ESP_LOGI(TAG, "radio task stack: %u B free at the low-water mark",
             (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
}

const char* publicKeyHex() { return g_pub_hex; }

const char* nodeName() { return kNodeName; }

ContactStore& contacts() { return g_contacts; }
MessageRing&  messages() { return g_messages; }
ReadState&    readState() { return g_read; }

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

}  // namespace mc
}  // namespace om
