// SPDX-License-Identifier: MIT
#include "time_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"   // taskENTER_CRITICAL: the radio task publishes, the main task consumes
#include "nvs.h"

#include "clock.h"

extern "C" {
#include "bsp/rtc.h"
}

namespace om {
namespace time_source {
namespace {

const char* TAG = "tm/clock";

// One namespace per concern, the same choice mc_link.cpp made: `mcid` holds what this NODE is (seed, advert
// floor, last good time), `mcch` holds the channels.
constexpr const char* kNvsNamespace = "mcid";
constexpr const char* kNvsEpochKey  = "epoch";

// central European time. The POSIX string is the one-timezone home: hour shifts are the C library's job, not ours.
// (CET-1 = UTC+1 in winter, CEST in summer from the last Sunday of March to the last Sunday of October.)
constexpr const char* kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";

// What the radio task recorded: recent ADVERT time samples, which the main task snapshots and decides on
// (core/clock.h owns the rules). The spinlock publishes the fixed-size table; the class of bug it keeps out
// (a half-updated sample read by the other task) is exactly the sort that shows up once and then hides.
portMUX_TYPE         g_mux = portMUX_INITIALIZER_UNLOCKED;
om::clock::Agreement g_agree;
om::clock::Warned    g_warned;   // main task only: "this sender was already reported once" (rule a)

// Where this boot's time came from: rules (a) and (b) differ by it. Set in begin(), read in service().
om::clock::Source g_boot_kind = om::clock::Source::None;

// Our knowledge of the coprocessor's RTC: its value at `s_rtc_us` and the monotonic time then, so we can tell
// how far off it is without reading the CH32 on every loop.
uint32_t g_rtc_epoch = 0;
int64_t  g_rtc_us    = 0;

// The stored floor, cached: service() runs every couple of seconds and must not open NVS that often.
uint32_t g_stored = 0;

char g_boot_source[8] = "none";

uint32_t systemNow() { return (uint32_t)time(nullptr); }

// Our own monotonic seconds: the timeline a sample is recorded on and projected along. It must NOT move when
// the wall clock is corrected, or "listened to it 10 minutes ago" would change meaning mid-flight.
uint32_t monotonicSeconds() { return (uint32_t)(esp_timer_get_time() / 1000000); }

// A public key folded to the 64-bit prefix the agreement table compares. 8 bytes is enough to tell two
// independent nodes apart on a leaf node's mesh (and the ticket names "pubkey prefix" as the identity).
uint64_t keyPrefix(const uint8_t* pub, size_t len) {
    uint64_t k = 0;
    for (size_t i = 0; i < 8 && i < len; i++) k |= (uint64_t)pub[i] << (8 * i);
    return k;
}

void setSystemTime(uint32_t epoch) {
    const struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
    settimeofday(&tv, nullptr);
}

// The RTC as it should be reading right now, from the value we last knew plus the monotonic time since.
// Only meaningful while this power-on is still running, which is exactly what we use it for.
uint32_t rtcEstimate() {
    if (g_rtc_epoch == 0) return 0;
    const int64_t elapsed = esp_timer_get_time() - g_rtc_us;
    return g_rtc_epoch + (uint32_t)(elapsed > 0 ? elapsed / 1000000 : 0);
}

void rememberRtc(uint32_t epoch) {
    g_rtc_epoch = epoch;
    g_rtc_us    = esp_timer_get_time();
}

uint32_t loadStoredEpoch() {
    nvs_handle_t h;
    uint32_t     v = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return 0;
    if (nvs_get_u32(h, kNvsEpochKey, &v) != ESP_OK) v = 0;
    nvs_close(h);
    return v;
}

void storeEpoch(uint32_t epoch) {
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "clock: cannot open NVS to store the time");
        return;
    }
    if (nvs_set_u32(h, kNvsEpochKey, epoch) != ESP_OK || nvs_commit(h) != ESP_OK) {
        ESP_LOGW(TAG, "clock: could not store the time floor in NVS");
    }
    nvs_close(h);
    g_stored = epoch;   // keep the cache honest even if the write failed: the RAM value is what we now believe
}

// UTC, always the same shape, so two log lines can be compared by eye.
void isoUtc(char* out, size_t n, uint32_t epoch) {
    time_t    t = (time_t)epoch;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%02dZ", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1,
             tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
}

// Local time, for the status bar and for a log line a human reads.
void localHhmm(char* out, size_t n, uint32_t epoch) {
    time_t    t = (time_t)epoch;
    struct tm tm_local;
    localtime_r(&t, &tm_local);
    snprintf(out, n, "%02d:%02d", tm_local.tm_hour, tm_local.tm_min);
}

}  // namespace

void begin() {
    setenv("TZ", kTimezone, 1);
    tzset();

    uint32_t rtc = 0;
    const esp_err_t r = bsp_rtc_get_time(&rtc);

    g_stored = loadStoredEpoch();
    const uint32_t stored = g_stored;
    const om::clock::Start s = om::clock::chooseStart(rtc, stored);
    g_boot_kind = s.source;   // rules (a) and (b) in service() differ by where this boot's time came from

    char rtc_iso[24] = "unreadable";
    if (r == ESP_OK) isoUtc(rtc_iso, sizeof(rtc_iso), rtc);

    if (s.source == om::clock::Source::Rtc) {
        setSystemTime(s.epoch);
        rememberRtc(s.epoch);
        char local[8];
        localHhmm(local, sizeof(local), s.epoch);
        snprintf(g_boot_source, sizeof(g_boot_source), "rtc");
        ESP_LOGI(TAG, "clock: from the coprocessor RTC: %s (%s local)", rtc_iso, local);
    } else if (s.source == om::clock::Source::Stored) {
        // The RTC lost (empty after a full power loss, or merely older than what we stored). The stored value is
        // a FLOOR, not the time: the device was off, so the real time is at least this and probably later. Say so
        // in the log rather than pretending it is exact.
        setSystemTime(s.epoch);
        char stored_iso[24], local[8];
        isoUtc(stored_iso, sizeof(stored_iso), s.epoch);
        localHhmm(local, sizeof(local), s.epoch);
        snprintf(g_boot_source, sizeof(g_boot_source), "stored");
        if (r == ESP_OK && om::clock::plausible(rtc)) {
            ESP_LOGW(TAG, "clock: the stored time is later than the RTC (%s vs %s): using %s, which is a FLOOR "
                          "(the device was off) until a peer's time arrives",
                     stored_iso, rtc_iso, stored_iso);
        } else {
            ESP_LOGW(TAG, "clock: RTC unusable (%s, %s): starting from the last known time %s, which is a FLOOR "
                          "(the device was off) until a peer's time arrives",
                     r == ESP_OK ? "out of range" : esp_err_to_name(r), rtc_iso, stored_iso);
        }
        // Give the CH32 a time too, so the launcher and the next boot start from something sane.
        if (bsp_rtc_set_time(s.epoch) == ESP_OK) {
            rememberRtc(s.epoch);
            ESP_LOGI(TAG, "clock: coprocessor RTC set from the stored floor (%s local)", local);
        }
    } else {
        snprintf(g_boot_source, sizeof(g_boot_source), "none");
        ESP_LOGW(TAG, "clock: no usable time yet (RTC %s, nothing stored). Adverts will carry the NVS floor "
                      "from mcid/advts until a peer's time is heard",
                 r == ESP_OK ? rtc_iso : esp_err_to_name(r));
    }

    if (s.epoch != 0 && om::clock::shouldPersist(s.epoch, stored)) storeEpoch(s.epoch);
}

void noteAdvert(uint32_t ts, const uint8_t* pub_key, size_t len) {
    if (pub_key == nullptr || len < 8) return;   // no usable identity -> cannot be a witness
    const uint64_t key   = keyPrefix(pub_key, len);
    const uint32_t local = monotonicSeconds();
    taskENTER_CRITICAL(&g_mux);
    om::clock::agreementRecord(g_agree, om::clock::Origin::Advert, key, ts, local);
    taskEXIT_CRITICAL(&g_mux);
}

void noteText(uint32_t ts) {
    // Recorded for symmetry and honesty, but a channel text is not a witness: the MAC proves the channel key
    // and the sender name is whatever the sender typed. agreementRecord drops it (rule b).
    const uint32_t local = monotonicSeconds();
    taskENTER_CRITICAL(&g_mux);
    om::clock::agreementRecord(g_agree, om::clock::Origin::Text, 0, ts, local);
    taskEXIT_CRITICAL(&g_mux);
}

void service() {
    const uint32_t mono_now = monotonicSeconds();

    // Snapshot the table: the radio task may be adding a sample while we decide, and a short critical
    // section around a fixed-size copy keeps the decision on ONE consistent set of witnesses.
    om::clock::Agreement snap;
    taskENTER_CRITICAL(&g_mux);
    snap = g_agree;
    taskEXIT_CRITICAL(&g_mux);

    if (g_boot_kind == om::clock::Source::Rtc) {
        // Rule (a): this boot's time is the battery-kept RTC's, so the mesh may NOT move it. A peer more than
        // 10 minutes off is worth one log line per sender, and that is all: no clock, RTC or NVS write.
        const uint32_t now = systemNow();
        for (size_t i = 0; i < om::clock::kAgreementSlots; i++) {
            const om::clock::Sample& s = snap.slot[i];
            if (s.key == 0) continue;
            const uint32_t peer = om::clock::projectSample(s.seen, s.local, mono_now);
            if (!om::clock::farFromRtc(now, peer)) continue;
            if (!om::clock::warnOnce(g_warned, s.key)) continue;
            char theirs[24], ours[24];
            isoUtc(theirs, sizeof(theirs), peer);
            isoUtc(ours, sizeof(ours), now);
            ESP_LOGW(TAG, "clock: a peer (%016llx) says %s, the RTC says %s (%+ld s off): NOT adopting a mesh "
                          "time on an RTC clock (S513/EST-429); recovery is manual, see PROGRESS",
                     (unsigned long long)s.key, theirs, ours, (long)((int64_t)peer - (int64_t)now));
        }
    } else {
        // Rules (b)+(c): adopt only a time two DIFFERENT advert keys agree on within 120 s, never below the
        // stored floor and never backwards. This is the ONLY path that writes the clock, the RTC or NVS.
        uint32_t agreed = 0;
        const uint32_t now = systemNow();
        if (om::clock::decideAdoption(snap, g_boot_kind, now, g_stored, mono_now, &agreed)) {
            setSystemTime(agreed);
            char iso[24], local[8];
            isoUtc(iso, sizeof(iso), agreed);
            localHhmm(local, sizeof(local), agreed);
            ESP_LOGI(TAG, "clock: now %s local (%s), %+ld s: two independent adverts agree", local, iso,
                     (long)((int64_t)agreed - (int64_t)now));

            // The CH32 RTC is the one clock that survives a power loss, so an agreed time goes back there too —
            // but only when it is meaningfully off, and only from this task (one owner of the coprocessor).
            const uint32_t rtc_now = rtcEstimate();
            if (om::clock::shouldWriteBack(rtc_now, agreed)) {
                if (bsp_rtc_set_time(agreed) == ESP_OK) {
                    rememberRtc(agreed);
                    ESP_LOGI(TAG, "clock: coprocessor RTC corrected to %s", iso);
                } else {
                    ESP_LOGW(TAG, "clock: could not correct the coprocessor RTC");
                }
            }
            storeEpoch(agreed);
            return;
        }
    }

    // Keep the stored floor fresh even on a quiet mesh: at most once an hour of clock progress (NVS wear).
    const uint32_t now = systemNow();
    if (!om::clock::plausible(now)) return;
    if (om::clock::shouldPersist(now, g_stored)) {
        storeEpoch(now);
        char local[8];
        localHhmm(local, sizeof(local), now);
        ESP_LOGI(TAG, "clock: stored %s local as the next boot's floor", local);
    }
}

const char* hhmm() {
    static char buf[8] = "--:--";
    const uint32_t now = systemNow();
    if (!om::clock::plausible(now)) {
        snprintf(buf, sizeof(buf), "--:--");
        return buf;
    }
    localHhmm(buf, sizeof(buf), now);
    return buf;
}

uint32_t epochNow() { return systemNow(); }

void hhmmOf(uint32_t epoch, char* out, size_t n) { localHhmm(out, n, epoch); }

bool haveTime() { return om::clock::plausible(systemNow()); }

const char* bootSource() { return g_boot_source; }

}  // namespace time_source
}  // namespace om
