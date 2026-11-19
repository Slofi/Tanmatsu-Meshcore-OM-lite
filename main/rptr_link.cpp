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
    static Row rows[om::mc::kMaxContacts];
    size_t n = 0;
    {
        mc::StoreLock l;
        mc::ContactStore& cs = mc::contacts();
        for (size_t i = 0; i < cs.count && n < om::mc::kMaxContacts; ++i) {
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
    const size_t n = rptr::buildLogin(self, f.key, secret, nowMs(), pw,
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
    const uint32_t tag = esp_random();
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
