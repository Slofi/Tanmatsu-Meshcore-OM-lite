// SPDX-License-Identifier: MIT
// RPTR part 1's device glue: see rptr_link.h. Every frame is handed to the radio task; nothing here transmits.
#include "rptr_link.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

#include "mc_identity.h"
#include "mc_link.h"
#include "rptr_wire.h"
#include "dm_send.h"       // S514 (Claude): dmOpenPath, for a reply that comes back as a PATH
#include "time_source.h"   // S514 (Claude): the request timestamp is epoch seconds, as upstream

namespace om {
namespace rptrlink {
namespace {

// The L2 takes upstream's airtime estimate. This path has no packet-size model, so it uses the L2's own STATUS
// FLOOR as the estimate: a login waits 8 + 2.5 s, a status request 8 s. Slower than a perfect estimate, never
// shorter than an answer that is on its way (EST-364's lesson on the L2).
constexpr uint32_t kEstimateMs = 8000;
constexpr int kRemembered = 4;
constexpr uint32_t kRememberedMagic = 0x52505431;   // "RPT1"

struct Remembered {
    uint32_t magic;
    char     name[rptr::kNameMax + 1];
    char     password[rptr::kPasswordMax + 1];
};

SemaphoreHandle_t s_lock = nullptr;
rptr::FoundList   s_found;
rptr::LoginMachine s_login;
rptr::Status      s_status;
uint32_t          s_statusSerial = 0;
bool              s_statusPending = false;
uint32_t          s_statusSentMs = 0;
uint32_t          s_statusDeadlineMs = 0;

// EST-470: part 2a's settings. ONE CLI line in flight, one read ladder, one last answer for the glass.
rptr::Settings    s_settings;
uint32_t          s_settingsSerial = 0;
rptr::CliPending  s_cli;
int               s_readStep = 0;          // next line of the read ladder
int               s_readStop = 0;          // where this pass stops (the whole ladder, or one row after a change)
bool              s_clock_sync = false;     // EST-470: send `clock sync` once after a login, as OM does
char              s_second[64] = "";        // the second half of a change (`set lon`), sent only after an OK
char              s_answer[96] = "";        // the repeater's last words, or "no answer"
bool              s_answerBad = false;
uint32_t          s_answerSerial = 0;

char     s_target[rptr::kNameMax + 1] = "";
uint8_t  s_targetKey[rptr::kPubKeyBytes] = {};
uint8_t  s_targetSecret[meshcore::kSecretBytes] = {};
uint8_t  s_targetPath[meshcore::kMaxPath] = {};
uint8_t  s_targetPathLen = 0xFF;
bool     s_haveTarget = false;
char     s_pendingPassword[rptr::kPasswordMax + 1] = "";

Remembered s_mem[kRemembered];
bool       s_remember = true;
nvs_handle_t s_nvs = 0;
bool         s_nvsOk = false;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// S514 (Claude): upstream puts getCurrentTimeUnique() (epoch SECONDS, strictly increasing) in the first four bytes
// of a login (BaseChatMesh.cpp:608) and of a request (:694), and the repeater refuses anything not newer than the
// last value it stored for this client (reference/meshcore-repeater-v1.18.0/MyMesh.cpp:114 login, :676 request).
// v53 sent milliseconds-since-boot for the login and a RANDOM number for the status request, so a request was
// refused about half the time and every login after a reboot looked like a replay.
uint32_t uniqueTimestamp() {
    static uint32_t last = 0;
    uint32_t        t    = (uint32_t)om::time_source::epochNow();
    if (t <= last) t = last + 1;
    last = t;
    return t;
}

void lockInit() {
    if (s_lock == nullptr) s_lock = xSemaphoreCreateMutex();
}

int findRemembered(const char* name) {
    if (name == nullptr || name[0] == '\0') return -1;
    for (int i = 0; i < kRemembered; ++i) {
        if (s_mem[i].magic == kRememberedMagic && strcmp(s_mem[i].name, name) == 0) return i;
    }
    return -1;
}

void loadRemembered() {
    for (int i = 0; i < kRemembered; ++i) s_mem[i] = Remembered();
    s_remember = true;
    s_nvsOk = nvs_open("mcrp", NVS_READWRITE, &s_nvs) == ESP_OK;
    if (!s_nvsOk) return;
    uint8_t rem = 1;   // the L2's default: On
    if (nvs_get_u8(s_nvs, "rem", &rem) == ESP_OK) s_remember = (rem != 0);
    for (int i = 0; i < kRemembered; ++i) {
        char key[4] = {'e', (char)('0' + i), 0, 0};
        Remembered r;
        size_t len = sizeof(r);
        if (nvs_get_blob(s_nvs, key, &r, &len) != ESP_OK || len != sizeof(r)) continue;
        if (r.magic != kRememberedMagic || r.name[0] == '\0') continue;
        r.name[rptr::kNameMax] = '\0';
        r.password[rptr::kPasswordMax] = '\0';
        s_mem[i] = r;
    }
}

void saveRemembered(int i) {
    if (!s_nvsOk) return;
    char key[4] = {'e', (char)('0' + i), 0, 0};
    s_mem[i].magic = kRememberedMagic;
    nvs_set_blob(s_nvs, key, &s_mem[i], sizeof(Remembered));
    nvs_commit(s_nvs);
}

void rememberPassword(const char* name, const char* password) {
    if (!s_remember || name == nullptr || name[0] == '\0' || password == nullptr || password[0] == '\0')
        return;
    int slot = findRemembered(name);
    if (slot < 0) {
        slot = 0;
        for (int i = 0; i < kRemembered; ++i) {
            if (s_mem[i].magic != kRememberedMagic) {
                slot = i;
                break;
            }
        }
    }
    s_mem[slot] = Remembered();
    snprintf(s_mem[slot].name, sizeof(s_mem[slot].name), "%s", name);
    snprintf(s_mem[slot].password, sizeof(s_mem[slot].password), "%s", password);
    saveRemembered(slot);
    ESP_LOGI("rptr", "password remembered for this repeater (never printed)");
}

// EST-470: one CLI line on the air, under the lock that already protects the target and the pending slot.
bool sendCliLocked(const char* line, int row, bool is_read, uint32_t now) {
    if (s_cli.used || line == nullptr || line[0] == '\0') return false;
    if (!s_haveTarget || !s_login.loggedIn()) return false;
    meshcore::Identity self;
    if (!mc::selfIdentity(&self)) return false;
    // The timestamp is the login's own strictly-increasing clock: the repeater drops anything not newer than the
    // last value it stored for this client (MyMesh.cpp:714 `sender_timestamp >= client->last_timestamp`, and an
    // EQUAL one is treated as a retry that deliberately answers nothing, :727-731).
    uint8_t      frame[192];
    const size_t n = rptr::buildCli(self, s_targetKey, s_targetSecret, uniqueTimestamp(), line,
                                    s_targetPathLen == 0xFF ? nullptr : s_targetPath, s_targetPathLen, frame,
                                    sizeof(frame));
    if (n == 0) {
        ESP_LOGW("rptr", "cli: \"%s\" could not be built", line);
        return false;
    }
    if (!mc::queueRptrFrame(frame, n)) {
        ESP_LOGW("rptr", "cli: \"%s\" not queued (radio busy)", line);
        return false;
    }
    s_cli.begin(line, (uint8_t)row, is_read, now, rptr::requestDeadlineMs(kEstimateMs, 0, rptr::kStatusMinMs));
    ESP_LOGI("rptr", "cli sent: \"%s\" (one line at a time, %lu ms deadline)", line,
             (unsigned long)rptr::requestDeadlineMs(kEstimateMs, 0, rptr::kStatusMinMs));
    return true;
}

int readLadderIndex(const char* cmd) {
    if (cmd == nullptr) return -1;
    for (int i = 0; i < rptr::kSetReadCount; ++i) {
        if (strcmp(rptr::setReadCmd(i), cmd) == 0) return i;
    }
    return -1;
}

void noteAnswer(const char* text, bool bad) {
    snprintf(s_answer, sizeof(s_answer), "%s", text != nullptr ? text : "");
    s_answerBad = bad;
    ++s_answerSerial;
}

// The contact behind a full public key, or nullptr. Used for the name and the stored route.
const mc::Contact* contactForKey(const uint8_t pub[meshcore::kPubKeyBytes]) {
    mc::ContactStore& cs = mc::contacts();
    for (size_t i = 0; i < cs.count; ++i) {
        if (memcmp(cs.c[i].pub, pub, meshcore::kPubKeyBytes) == 0) return &cs.c[i];
    }
    return nullptr;
}

}  // namespace

void begin() {
    lockInit();
    loadRemembered();
    ESP_LOGI("rptr", "remembered repeaters: %d, Remember passwords %s", rememberedCount(),
             s_remember ? "On" : "Off");
}

void lock() {
    lockInit();
    if (s_lock != nullptr) xSemaphoreTake(s_lock, portMAX_DELAY);
}

void unlock() {
    if (s_lock != nullptr) xSemaphoreGive(s_lock);
}

// The list also shows repeater CONTACTS heard from adverts (the ticket's second half of item 2), not only the
// answers to this scan. They enter the SAME FoundList, so the row index the page uses is the index loginStart()
// resolves — one list, one order. A contact is merged at most once per two seconds; addAnswer() keys on the
// public-key prefix, so a repeater that both advertises and answers a scan is still one row.
void mergeContacts() {
    struct Row {
        uint8_t pub[meshcore::kPubKeyBytes];
        char    name[rptr::kNameMax + 1];
    };
    // S515/EST-463: only REPEATERS are merged, and the radio may hold 300 of them (kMaxRadio). This used to be
    // sized by the contact table, which is now 2,000 rows and would have cost ~130 KB of .bss for a list that
    // can never be longer than the repeaters in the table.
    static Row rows[om::mc::kMaxRadio];
    size_t n = 0;
    {
        mc::StoreLock l;
        mc::ContactStore& cs = mc::contacts();
        for (size_t i = 0; i < cs.count && n < om::mc::kMaxRadio; ++i) {
            if (cs.c[i].type != 2) continue;   // ADV_TYPE_REPEATER only
            memcpy(rows[n].pub, cs.c[i].pub, meshcore::kPubKeyBytes);
            snprintf(rows[n].name, sizeof(rows[n].name), "%s", cs.c[i].name);
            ++n;
        }
    }
    if (n == 0) return;
    lock();
    for (size_t i = 0; i < n; ++i) {
        if (s_found.indexOf(rows[i].pub) >= 0) continue;
        s_found.addAnswer(rows[i].pub, (int)meshcore::kPubKeyBytes, rows[i].name, true, false, 0, false, 0, 0,
                          nowMs());
    }
    unlock();
}

void tick() {
    lock();
    s_found.tick(nowMs());
    s_login.tick(nowMs());
    if (s_statusPending && (int32_t)(nowMs() - s_statusDeadlineMs) >= 0) {
        s_statusPending = false;   // one honest "no answer yet" on the page; r tries again
        ESP_LOGW("rptr", "status: no answer within %lu ms", (unsigned long)(s_statusDeadlineMs - s_statusSentMs));
    }
    // EST-470 item 4: ONE line at a time. A lost answer changes NOTHING on the repeater's side, so the row keeps
    // the value it was READ as and the page says "no answer"; the next line then goes out (a read that timed out
    // is skipped, never retried in a loop).
    const uint32_t now = nowMs();
    if (s_cli.used && s_cli.timedOut(now)) {
        ESP_LOGW("rptr", "cli: no answer to \"%s\" within %lu ms", s_cli.line,
                 (unsigned long)(s_cli.deadlineMs - s_cli.sentMs));
        s_cli.clear();
        // A lost answer changes NOTHING on the repeater's side, so the row keeps the value it was READ as and the
        // page says "no answer". The step already moved past the line when it went out, so the next pass sends the
        // next read instead of sitting on this one. A paired change stops here: its first half is unproven.
        s_second[0] = '\0';
        noteAnswer("no answer", true);
    }
    // EST-470: `clock sync` goes out once per login, BEFORE the reads (OM sends it automatically too). Skipped
    // while our own clock is not real: the repeater would only answer "ERR: clock cannot go backwards".
    if (s_clock_sync && !s_cli.used && s_login.loggedIn() && s_haveTarget) {
        if (om::time_source::epochNow() < (int64_t)rptr::kClockRealSince) {
            s_clock_sync = false;
            ESP_LOGW("rptr", "clock sync skipped: this device's clock is not set");
        } else if (sendCliLocked(rptr::kCliClockSync, (int)rptr::SetRow::ClockSync, false, now)) {
            s_clock_sync = false;
        }
    }
    // The ladder: one read in flight at a time, only once logged in, and only when nothing else is pending.
    if (!s_cli.used && s_login.loggedIn() && s_haveTarget && s_readStep < s_readStop) {
        const char* cmd = rptr::setReadCmd(s_readStep);
        if (cmd == nullptr) {
            s_readStep = s_readStop;
        } else if (sendCliLocked(cmd, rptr::setReadRow(cmd), true, now)) {
            ++s_readStep;   // the answer (or its deadline) releases the next line
        }
    }
    unlock();
    static uint32_t last_merge = 0;
    const uint32_t t = nowMs();
    if ((uint32_t)(t - last_merge) >= 2000) {
        last_merge = t;
        mergeContacts();
    }
}

bool scanStart() {
    uint8_t frame[32];
    const uint32_t tag = esp_random();
    const size_t n = rptr::buildScan(tag, frame, sizeof(frame));
    if (n == 0) return false;
    if (!mc::queueRptrFrame(frame, n)) return false;
    lock();
    s_found.begin(tag, nowMs());
    unlock();
    ESP_LOGI("rptr", "scan: discover tag %08lx, listening %lu ms", (unsigned long)tag,
             (unsigned long)rptr::kScanWindowMs);
    return true;
}

bool loginStart(int found_index, const char* password) {
    char pw[rptr::kPasswordMax + 1];
    rptr::passwordFilter(password, pw, sizeof(pw));
    if (pw[0] == '\0') return false;

    lock();
    if (found_index < 0 || found_index >= s_found.count) {
        unlock();
        return false;
    }
    const rptr::Found f = s_found.rows[found_index];
    unlock();
    if (f.keyLen < rptr::kPubKeyBytes) {
        ESP_LOGW("rptr", "login: this answer carried no full key, cannot derive the secret");
        return false;
    }

    meshcore::Identity self;
    if (!mc::selfIdentity(&self)) return false;

    uint8_t secret[meshcore::kSecretBytes];
    if (!meshcore::identitySharedSecret(self, f.key, secret)) return false;

    uint8_t path[meshcore::kMaxPath];
    uint8_t path_len = 0xFF;
    char name[rptr::kNameMax + 1] = "";
    {
        mc::StoreLock l;
        const mc::Contact* c = contactForKey(f.key);
        if (c != nullptr) {
            snprintf(name, sizeof(name), "%s", c->name);
            if (c->out_path_len != mc::kOutPathUnknown) {
                path_len = c->out_path_len;
                const size_t bytes = (size_t)(path_len & 63) * (size_t)((path_len >> 6) + 1);
                memcpy(path, c->out_path, bytes);
            }
        }
    }
    if (name[0] == '\0') {
        snprintf(name, sizeof(name), "%02x%02x%02x", f.prefix[0], f.prefix[1], f.prefix[2]);
    }

    uint8_t frame[128];
    const size_t n = rptr::buildLogin(self, f.key, secret, uniqueTimestamp(), pw,
                                      path_len == 0xFF ? nullptr : path, path_len, frame, sizeof(frame));
    if (n == 0 || !mc::queueRptrFrame(frame, n)) {
        ESP_LOGW("rptr", "login: nothing went on the air");
        return false;
    }

    lock();
    snprintf(s_target, sizeof(s_target), "%s", name);
    memcpy(s_targetKey, f.key, sizeof(s_targetKey));
    memcpy(s_targetSecret, secret, sizeof(s_targetSecret));
    if (path_len != 0xFF) {
        s_targetPathLen = path_len;
        memcpy(s_targetPath, path, (size_t)(path_len & 63) * (size_t)((path_len >> 6) + 1));
    } else {
        s_targetPathLen = 0xFF;
    }
    s_haveTarget = true;
    s_status = rptr::Status();
    s_statusPending = false;
    // EST-470: a login to a repeater starts with NOTHING read, so every settings row shows "…" until its own
    // answer arrives; the read ladder starts as soon as the login succeeds.
    s_settings.reset();
    ++s_settingsSerial;
    s_cli.clear();
    s_readStep = 0;
    s_readStop = 0;
    s_clock_sync = true;   // EST-470: the repeater's clock is synced right after the login, as OM does
    s_answer[0] = '\0';
    s_answerBad = false;
    ++s_answerSerial;
    snprintf(s_pendingPassword, sizeof(s_pendingPassword), "%s", pw);
    s_login.begin(nowMs(), kEstimateMs);
    unlock();
    ESP_LOGI("rptr", "login sent to \"%s\" (%s), waiting %lu ms", name,
             path_len == 0xFF ? "flood" : "direct", (unsigned long)(kEstimateMs + rptr::kLoginExtraMs));
    return true;
}

void logout() {
    lock();
    s_login.reset();
    s_pendingPassword[0] = '\0';
    s_haveTarget = false;
    s_target[0] = '\0';
    memset(s_targetKey, 0, sizeof(s_targetKey));
    memset(s_targetSecret, 0, sizeof(s_targetSecret));
    s_status = rptr::Status();
    s_statusPending = false;
    s_settings.reset();
    ++s_settingsSerial;
    s_cli.clear();
    s_readStep = 0;
    s_readStop = 0;
    s_clock_sync = false;
    s_second[0] = '\0';
    s_answer[0] = '\0';
    s_answerBad = false;
    ++s_answerSerial;
    unlock();
    ESP_LOGI("rptr", "session closed");
}

bool statusRequest() {
    lock();
    if (!s_haveTarget || !s_login.loggedIn()) {
        unlock();
        return false;
    }
    meshcore::Identity self;
    if (!mc::selfIdentity(&self)) {
        unlock();
        return false;
    }
    const uint32_t tag = uniqueTimestamp();   // the request's timestamp: must be newer than the last (see above)
    uint8_t frame[128];
    const size_t n = rptr::buildStatusRequest(self, s_targetKey, s_targetSecret, tag, esp_random(),
                                              s_targetPathLen == 0xFF ? nullptr : s_targetPath,
                                              s_targetPathLen, frame, sizeof(frame));
    if (n == 0) {
        unlock();
        return false;
    }
    const bool queued = mc::queueRptrFrame(frame, n);
    if (queued) {
        s_statusPending = true;
        s_statusSentMs = nowMs();
        s_statusDeadlineMs = s_statusSentMs + rptr::requestDeadlineMs(kEstimateMs, 0, rptr::kStatusMinMs);
    }
    unlock();
    if (queued) ESP_LOGI("rptr", "status request queued");
    return queued;
}

void onControl(const meshcore::View& pkt, int snr_db) {
    lock();
    const uint32_t tag = s_found.tag;
    const bool listening = s_found.listening;
    unlock();
    if (!listening) return;

    rptr::Found f;
    if (!rptr::parseScanAnswer(pkt, tag, (int8_t)(snr_db * 4), &f)) return;

    char name[rptr::kNameMax + 1] = "";
    bool in_contacts = false;
    {
        mc::StoreLock l;
        const mc::Contact* c = contactForKey(f.key);
        if (c != nullptr) {
            snprintf(name, sizeof(name), "%s", c->name);
            in_contacts = true;
        }
    }
    lock();
    const int at = s_found.addAnswer(f.key, f.keyLen, name, in_contacts, f.hasSnrIn, f.snrIn4, f.hasSnrOut,
                                     f.snrOut4, f.hops, nowMs());
    unlock();
    if (at >= 0) {
        ESP_LOGI("rptr", "answer %02x%02x%02x: snr in %d/4 out %d/4, key %u B, %d found", f.prefix[0],
                 f.prefix[1], f.prefix[2], (int)f.snrIn4, (int)f.snrOut4, (unsigned)f.keyLen,
                 at + 1);
    }
}

bool handleReply(const uint8_t* plain, size_t plain_len, bool login_busy, bool status_pending);

bool onResponse(const meshcore::View& pkt) {
    lock();
    const bool login_busy = s_login.busy();
    const bool status_pending = s_statusPending;
    const bool have = s_haveTarget;
    uint8_t key[rptr::kPubKeyBytes];
    uint8_t secret[meshcore::kSecretBytes];
    memcpy(key, s_targetKey, sizeof(key));
    memcpy(secret, s_targetSecret, sizeof(secret));
    unlock();
    if (!have) return false;
    if (pkt.payload_len < 2 || pkt.payload[1] != key[0]) return false;

    meshcore::Identity self;
    if (!mc::selfIdentity(&self)) return false;

    uint8_t plain[meshcore::kMaxPayload];
    size_t plain_len = 0;
    if (!rptr::openResponse(self, key, secret, pkt, plain, sizeof(plain), &plain_len)) return false;
    return handleReply(plain, plain_len, login_busy, status_pending);
}

// The decrypted reply data of a login or a status request, whichever is pending. The same bytes arrive either as
// a RESPONSE datagram or inside a PATH return (S514: a repeater answers a FLOODED login with
// createPathReturn(..., PAYLOAD_TYPE_RESPONSE, reply_data, ...), reference/meshcore-repeater-v1.18.0/MyMesh.cpp:602-606).
bool handleReply(const uint8_t* plain, size_t plain_len, bool login_busy, bool status_pending) {
    if (login_busy) {
        rptr::LoginAnswer a;
        if (!rptr::parseLoginAnswer(plain, plain_len, &a)) return false;
        lock();
        s_login.succeed(a, nowMs());
        const uint32_t rtt = s_login.rttMs;
        if (s_login.loggedIn() && s_pendingPassword[0] != '\0') {
            rememberPassword(s_target, s_pendingPassword);
            s_pendingPassword[0] = '\0';
        }
        unlock();
        ESP_LOGI("rptr", "login: %s, rtt %lu ms, fw %u, permissions %u",
                 rptr::loginStateText(a.ok ? (a.admin ? rptr::LoginState::Admin : rptr::LoginState::Guest)
                                           : rptr::LoginState::Refused),
                 (unsigned long)rtt, (unsigned)a.fwLevel, (unsigned)a.permissions);
        return true;
    }

    if (status_pending) {
        rptr::Status st;
        if (!rptr::parseStatusAnswer(plain, plain_len, &st)) return false;
        lock();
        st.atMs = nowMs();
        st.rttMs = st.atMs - s_statusSentMs;
        st.haveRtt = true;
        st.fwLevel = s_login.answer.fwLevel;
        st.clock = s_login.answer.clock;
        st.clockValid = s_login.answer.clockValid;
        s_status = st;
        s_statusPending = false;
        ++s_statusSerial;
        unlock();
        ESP_LOGI("rptr", "status: up %lu s, %u mV, air %lu s, rx %lu tx %lu, noise %d",
                 (unsigned long)st.uptimeSec, (unsigned)st.battMv, (unsigned long)st.airtimeSec,
                 (unsigned long)st.rxPackets, (unsigned long)st.txPackets, (int)st.noiseFloor);
        return true;
    }
    return false;
}

// S514 (Claude; Filip: "stuck on sending, no reply"): v53 dropped the repeater's login answer, because a FLOODED
// login is answered with a PATH packet (route back + the RESPONSE as its extra) and PATH packets were only offered
// to the contact table, where a scanned repeater is not. The login target's own key is tried here; the route it
// carries is kept for the next request, and an embedded RESPONSE goes to the same parser as a RESPONSE datagram.
bool onPathReply(const meshcore::View& pkt) {
    lock();
    const bool login_busy     = s_login.busy();
    const bool status_pending = s_statusPending;
    const bool have           = s_haveTarget;
    uint8_t    key[rptr::kPubKeyBytes];
    memcpy(key, s_targetKey, sizeof(key));
    unlock();
    if (!have) return false;
    if (pkt.payload_len < 2 || pkt.payload[1] != key[0]) return false;

    meshcore::Identity self;
    if (!mc::selfIdentity(&self)) return false;
    static meshcore::DmPath p;   // radio task only; large (extra buffer), so not on its stack
    p = meshcore::DmPath{};
    if (!meshcore::dmOpenPath(self, pkt, key, 1, &p)) return false;

    lock();
    if (p.out_path_len != 0xFF) {
        s_targetPathLen = p.out_path_len;
        memcpy(s_targetPath, p.out_path, (size_t)(p.out_path_len & 63) * (size_t)((p.out_path_len >> 6) + 1));
    }
    unlock();
    ESP_LOGI("rptr", "reply came as a PATH (route 0x%02X, extra type %u, %u B)", (unsigned)p.out_path_len,
             (unsigned)p.extra_type, (unsigned)p.extra_len);
    if (p.extra_type == rptr::kTypeResponse) (void)handleReply(p.extra, p.extra_len, login_busy, status_pending);
    return true;   // it was ours either way: the route alone is worth keeping
}

bool settingsReadStart() {
    lock();
    if (!s_haveTarget || !s_login.loggedIn()) {
        unlock();
        return false;
    }
    // A read in flight is dropped first: 'r' means "read it all again", and the old answer would land on a row
    // this pass has already asked about.
    s_cli.clear();
    s_readStep = 0;
    s_readStop = rptr::kSetReadCount;
    unlock();
    ESP_LOGI("rptr", "settings: reading the repeater's %d value(s)", rptr::kSetReadCount);
    return true;
}

bool settingsSend(const char* line, int row, bool is_read, const char* second) {
    lock();
    const bool sent = sendCliLocked(line, row, is_read, nowMs());
    if (sent) {
        s_second[0] = '\0';
        if (second != nullptr && second[0] != '\0') snprintf(s_second, sizeof(s_second), "%s", second);
    }
    if (sent && row == (int)rptr::SetRow::Reboot) {
        // `reboot` is the ONE line that can never answer: CommonCLI.cpp:199-200 calls board->reboot(), which
        // does not return, so no reply is ever built. Holding the slot for 8 s would only paint "no answer"
        // over a command that worked, so the answer here is the SEND itself.
        s_cli.clear();
        s_second[0] = '\0';
        noteAnswer("reboot sent - it is off the mesh for ~10 s", false);
    }
    unlock();
    return sent;
}

// EST-470: the CLI answer. This runs on the radio task, BEFORE mc_link's DM code, for two reasons that S514 got
// wrong once already for the login reply: the answering node is usually NOT in the contact table, and a CLI answer
// must never land in a DM thread. Everything that is not a CLI-typed text from the login target is refused here
// and falls through to the DM path untouched.
bool onCliAnswer(const meshcore::View& pkt) {
    if (pkt.type() != rptr::kTypeTxtMsg) return false;
    lock();
    const bool    have = s_haveTarget && s_login.loggedIn();
    uint8_t       key[rptr::kPubKeyBytes];
    uint8_t       secret[meshcore::kSecretBytes];
    memcpy(key, s_targetKey, sizeof(key));
    memcpy(secret, s_targetSecret, sizeof(secret));
    unlock();
    if (!have) return false;
    if (pkt.payload_len < 2 || pkt.payload[1] != key[0]) return false;

    meshcore::Identity self;
    if (!mc::selfIdentity(&self)) return false;

    static uint8_t plain[meshcore::kMaxPayload];   // radio task only, and large: not on its stack
    size_t         plain_len = 0;
    if (!rptr::openCliAnswer(self, key, secret, pkt, plain, sizeof(plain), &plain_len)) return false;

    rptr::CliReply reply;
    if (!rptr::parseCliReply(plain, plain_len, &reply)) return false;   // a PLAIN text: the DM code's

    const bool bad = rptr::cliAnswerKind(reply.text) != rptr::CliAnswer::Ok;
    lock();
    const bool    pending = s_cli.used;
    const bool    was_read = s_cli.isRead;
    const uint8_t row = s_cli.row;
    if (was_read) {
        // A read's answer becomes the row's VALUE - and only when it parses as that line's format.
        if (rptr::settingsApplyRead(&s_settings, s_cli.line, reply.text)) ++s_settingsSerial;
    }
    // A read's own text ("> RPT-Test-A") is not a notice: the VALUE is the feedback. An error on a read is.
    if (!was_read || bad) noteAnswer(reply.text, bad);
    if (pending) {
        s_cli.clear();
        if (!was_read && !bad && s_second[0] != '\0') {
            // The first half of a PAIRED change (`set lat`) answered OK, so the second (`set lon`) goes out now,
            // on its own; the answer to THAT one is what triggers the re-read. A refused or lost first half drops
            // the second, so the repeater never ends up with a half-applied position.
            char next[64];
            snprintf(next, sizeof(next), "%s", s_second);
            s_second[0] = '\0';
            if (!sendCliLocked(next, (int)row, false, nowMs())) {
                snprintf(s_second, sizeof(s_second), "%s", next);   // radio busy: try again on the next answer
                noteAnswer("the second line is waiting for the radio", true);
            }
        } else if (!was_read) {
            s_second[0] = '\0';
            // A change: the answer is a notice, and the row is read again so the glass shows the repeater's own
            // new value. Reboot gets no answer at all (CommonCLI.cpp:199-200 never returns), so it is a change
            // with nothing to re-read.
            if (row == (uint8_t)rptr::SetRow::Reboot) {
                s_readStep = s_readStop;
            } else {
                const int idx = readLadderIndex(rptr::setRowReadCmd((int)row));
                if (idx >= 0) {
                    s_readStep = idx;
                    s_readStop = idx + (row == (uint8_t)rptr::SetRow::Position ? 2 : 1);
                }
            }
        }
    }
    unlock();
    ESP_LOGI("rptr", "cli answer: \"%s\"%s", reply.text, bad ? " (not OK)" : "");
    return true;
}

const rptr::Settings& settings() { return s_settings; }
uint32_t settingsSerial() { return s_settingsSerial; }
const rptr::CliPending& cli() { return s_cli; }
bool cliBusy() { return s_cli.used; }
const char* lastAnswer() { return s_answer; }
bool lastAnswerBad() { return s_answerBad; }
uint32_t answerSerial() { return s_answerSerial; }

const rptr::FoundList& found() { return s_found; }
const rptr::LoginMachine& login() { return s_login; }
const rptr::Status& status() { return s_status; }
uint32_t statusSerial() { return s_statusSerial; }
bool statusPending() { return s_statusPending; }
const char* target() { return s_target; }
bool haveTarget() { return s_haveTarget; }

bool rememberEnabled() { return s_remember; }

bool setRememberEnabled(bool on) {
    s_remember = on;
    if (s_nvsOk) {
        nvs_set_u8(s_nvs, "rem", on ? 1 : 0);
        nvs_commit(s_nvs);
    }
    return true;
}

int rememberedCount() {
    int n = 0;
    for (int i = 0; i < kRemembered; ++i)
        if (s_mem[i].magic == kRememberedMagic) ++n;
    return n;
}

bool hasRemembered(const char* name) { return findRemembered(name) >= 0; }

bool passwordFor(const char* name, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return false;
    out[0] = '\0';
    const int i = findRemembered(name);
    if (i < 0) return false;
    snprintf(out, cap, "%s", s_mem[i].password);
    return out[0] != '\0';
}

bool forgetAllPasswords() {
    for (int i = 0; i < kRemembered; ++i) s_mem[i] = Remembered();
    if (s_nvsOk) {
        for (int i = 0; i < kRemembered; ++i) {
            char key[4] = {'e', (char)('0' + i), 0, 0};
            nvs_erase_key(s_nvs, key);
        }
        nvs_commit(s_nvs);
    }
    ESP_LOGI("rptr", "all remembered passwords forgotten");
    return true;
}

}  // namespace rptrlink
}  // namespace om
