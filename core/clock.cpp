// SPDX-License-Identifier: MIT
#include "clock.h"

namespace om {
namespace clock {

bool plausible(uint32_t epoch) { return epoch >= kMinPlausible && epoch <= kMaxPlausible; }

Start chooseStart(uint32_t rtc, uint32_t stored) {
    const bool rtc_ok    = plausible(rtc);
    const bool stored_ok = plausible(stored);
    if (rtc_ok && stored_ok) return (rtc >= stored) ? Start{Source::Rtc, rtc} : Start{Source::Stored, stored};
    if (rtc_ok) return {Source::Rtc, rtc};
    if (stored_ok) return {Source::Stored, stored};
    return {Source::None, 0};
}

bool shouldAdopt(uint32_t now, uint32_t seen) {
    if (!plausible(seen)) return false;
    if (now > kMaxPlausible) return false;          // our clock is nonsense in the other direction: do not compare
    if (!plausible(now)) return true;               // we have no time at all: any plausible one wins
    return seen > now + kAdoptSlack;
}

bool shouldWriteBack(uint32_t rtc, uint32_t known) {
    if (!plausible(known)) return false;
    if (!plausible(rtc)) return true;               // the RTC is empty: give it a time
    const uint32_t gap = (known > rtc) ? known - rtc : rtc - known;
    return gap > kWriteBackGap;
}

bool shouldPersist(uint32_t now, uint32_t stored) {
    if (!plausible(now)) return false;
    if (!plausible(stored)) return true;
    return now > stored + kPersistGap;              // never store a LOWER time: the stored value is a floor
}

uint32_t advertFloor(uint32_t now, uint32_t floor) {
    // A corrupt floor (a bit flip in NVS) must not poison the timestamp either: ignore what cannot be a time.
    if (!plausible(floor)) return now;
    return (now > floor) ? now : floor + 1;
}

}  // namespace clock
}  // namespace om
