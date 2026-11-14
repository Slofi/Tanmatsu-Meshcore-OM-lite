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

// What the radio task recorded and the main task has not applied yet. One slot, two tasks: the pointer-sized
// fields are published under a spinlock rather than trusted to be "probably written in the right order" — the
// cost is a handful of instructions per packet, and the class of bug (a mangled label, a half-updated ts) is
// exactly the sort that shows up once and then hides.
portMUX_TYPE      g_mux         = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t g_seen_ts     = 0;
volatile bool     g_seen        = false;
char              g_seen_what[16] = {0};

// Our knowledge of the coprocessor's RTC: its value at `s_rtc_us` and the monotonic time then, so we can tell
// how far off it is without reading the CH32 on every loop.
uint32_t g_rtc_epoch = 0;
int64_t  g_rtc_us    = 0;

// The stored floor, cached: service() runs every couple of seconds and must not open NVS that often.
uint32_t g_stored = 0;

char g_boot_source[8] = "none";

uint32_t systemNow() { return (uint32_t)time(nullptr); }

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

void noteSeen(uint32_t ts, const char* what) {
    if (!om::clock::plausible(ts)) return;
    if (!om::clock::shouldAdopt(systemNow(), ts)) return;   // cheaper to filter here: no wake-up for noise

    // Copy the label OUTSIDE the critical section, then publish everything with one short locked step.
    char label[sizeof(g_seen_what)];
    snprintf(label, sizeof(label), "%s", what != nullptr ? what : "mesh");

    taskENTER_CRITICAL(&g_mux);
    for (size_t i = 0; i < sizeof(g_seen_what); i++) g_seen_what[i] = label[i];
    g_seen_ts = ts;
    g_seen    = true;
    taskEXIT_CRITICAL(&g_mux);
}

void service() {
    uint32_t ts = 0;
    char     what[16] = {0};
    taskENTER_CRITICAL(&g_mux);
    const bool pending = g_seen;
    if (pending) {
        ts    = g_seen_ts;
        g_seen = false;
        for (size_t i = 0; i < sizeof(what); i++) what[i] = g_seen_what[i];
        what[sizeof(what) - 1] = '\0';
    }
    taskEXIT_CRITICAL(&g_mux);

    if (pending) {
        const uint32_t before = systemNow();
        if (om::clock::shouldAdopt(before, ts)) {
            setSystemTime(ts);
            char iso[24], local[8];
            isoUtc(iso, sizeof(iso), ts);
            localHhmm(local, sizeof(local), ts);
            ESP_LOGI(TAG, "clock: now %s local (%s), %+ld s from a %s", local, iso,
                     (long)((int64_t)ts - (int64_t)before), what);

            // The CH32 RTC is the one clock that survives a power loss, so a learned time goes back there too —
            // but only when it is meaningfully off, and only from this task (one owner of the coprocessor).
            const uint32_t rtc_now = rtcEstimate();
            if (om::clock::shouldWriteBack(rtc_now, ts)) {
                if (bsp_rtc_set_time(ts) == ESP_OK) {
                    rememberRtc(ts);
                    ESP_LOGI(TAG, "clock: coprocessor RTC corrected to %s", iso);
                } else {
                    ESP_LOGW(TAG, "clock: could not correct the coprocessor RTC");
                }
            }
            storeEpoch(ts);
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
