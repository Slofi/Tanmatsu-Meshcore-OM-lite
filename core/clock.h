// SPDX-License-Identifier: MIT
// The app's clock, as RULES rather than calls: which time to believe at boot, when to accept a time from the
// mesh, when to push it back to the coprocessor's RTC, and how the advert timestamp is floored. Pure logic,
// no IDF and no libc time calls, so the device and the host tests run the SAME decisions.
//
// Why this exists (S501/S502): the P4's system clock restarts at 0 after a power loss and MeshCore drops an
// advert that is not newer than the last one it stored for that node, while channel texts keep working. That
// is a silent, one-sided failure: peers stop seeing us and nothing on our screen looks wrong. The Tanmatsu
// does have a battery-backed RTC, in the CH32 coprocessor (bsp/rtc.h) — the app simply never asked it.
//
// ⚠️ `om::clock`, NOT `tm::clock`: the app's core namespace `tm` collides with libc's `struct tm`, and `clock`
// is libc's `clock_t clock(void)`. Any TU that includes <time.h> and a `namespace tm` fails to compile
// ("redeclared as different kind of entity"), which is exactly the TU a clock needs. Renaming the whole core
// namespace is a later cleanup; new TIME code stays out of `tm`.
#pragma once

#include <stdint.h>

namespace om {
namespace clock {

// A timestamp is only a TIME if it could be one. Below the minimum is the P4 at boot (1970-ish); above the
// maximum is a bit flip or a broken peer. 2024-01-01 and 2100-01-01, UTC.
constexpr uint32_t kMinPlausible = 1704067200u;
constexpr uint32_t kMaxPlausible = 4102444800u;

// A peer's time must beat ours by more than this before we move the clock: mesh timestamps are whole seconds
// and a few seconds of sniffed traffic must not make the clock jitter.
constexpr uint32_t kAdoptSlack = 60u;

// Only push a learned time back into the CH32 RTC when it is this far off. The RTC runs on its own, so a
// small difference is not worth a coprocessor write.
constexpr uint32_t kWriteBackGap = 300u;

// NVS wear: store the last good time at most once per hour of clock progress.
constexpr uint32_t kPersistGap = 3600u;

enum class Source {
    None,     // nothing usable: the app runs with the 1970 clock and says so
    Rtc,      // the coprocessor's RTC — the good case, it survives a full power loss
    Stored,   // the last good time we stored in NVS: a floor, not the real time (the device was off)
};

struct Start {
    Source   source;
    uint32_t epoch;   // 0 when source == None
};

bool plausible(uint32_t epoch);

// Which time to believe at boot. Both the RTC and the stored value mean "this was true at some moment in the
// past", so the true current time is >= both: the LATER one is the better estimate. The RTC wins a tie because
// it is the clock that kept running.
Start chooseStart(uint32_t rtc, uint32_t stored);

// A timestamp we RECEIVED and could authenticate (an advert's signature, a channel text's MAC) is a time
// source. Accept it only if it is plausible and moves us forward by more than kAdoptSlack.
bool shouldAdopt(uint32_t now, uint32_t seen);

// Should the coprocessor's RTC be corrected to `known`?
bool shouldWriteBack(uint32_t rtc, uint32_t known);

// Should `now` be stored in NVS (so the next cold boot starts from it)?
bool shouldPersist(uint32_t now, uint32_t stored);

// The advert timestamp: never lower than one we have already sent (MeshCore's replay protection compares
// against the stored value), and one second past the floor if our own clock is behind it.
uint32_t advertFloor(uint32_t now, uint32_t floor);

}  // namespace clock
}  // namespace om
