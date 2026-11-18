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

namespace {

// The sample projected to `local_now`, as a signed value: `seen` plus the monotonic seconds since it was
// heard. A sample heard "in the future" (never, with a monotonic clock) ages by 0 rather than going back.
int64_t projected64(uint32_t seen, uint32_t local_s, uint32_t local_now) {
    const int64_t age = (int64_t)local_now - (int64_t)local_s;
    return (int64_t)seen + (age > 0 ? age : 0);
}

bool projectedPlausible(int64_t p) { return p >= (int64_t)kMinPlausible && p <= (int64_t)kMaxPlausible; }

}  // namespace

uint32_t projectSample(uint32_t seen, uint32_t local_s, uint32_t local_now) {
    const int64_t p = projected64(seen, local_s, local_now);
    return projectedPlausible(p) ? (uint32_t)p : 0u;
}

void agreementRecord(Agreement& a, Origin origin, uint64_t key, uint32_t seen, uint32_t local_s) {
    // Rule (b): a channel text is never a witness - the sender name is self-declared and a channel key can
    // be public, so it says nothing about who holds the clock. Accept the call, drop the sample.
    if (origin != Origin::Advert) return;
    if (key == 0 || !plausible(seen)) return;

    // One sample per key prefix: a repeat replaces its own slot, so one node advertising often cannot
    // flush the second witness out of the fixed table.
    for (size_t i = 0; i < kAgreementSlots; i++) {
        if (a.slot[i].key == key) {
            a.slot[i].seen  = seen;
            a.slot[i].local = local_s;
            return;
        }
    }
    Sample& s = a.slot[a.next % kAgreementSlots];
    s.key     = key;
    s.seen    = seen;
    s.local   = local_s;
    a.next    = (uint8_t)((a.next + 1) % kAgreementSlots);
}

bool agreementTime(const Agreement& a, uint32_t local_now, uint32_t* out_epoch) {
    if (out_epoch != nullptr) *out_epoch = 0;
    for (size_t i = 0; i < kAgreementSlots; i++) {
        const Sample& x = a.slot[i];
        if (x.key == 0) continue;
        const int64_t tx = projected64(x.seen, x.local, local_now);
        if (!projectedPlausible(tx)) continue;
        for (size_t j = i + 1; j < kAgreementSlots; j++) {
            const Sample& y = a.slot[j];
            if (y.key == 0 || y.key == x.key) continue;   // two copies of ONE node are not two witnesses
            const int64_t ty = projected64(y.seen, y.local, local_now);
            if (!projectedPlausible(ty)) continue;
            const int64_t gap = (tx > ty) ? tx - ty : ty - tx;
            if (gap > (int64_t)kAgreementWindow) continue;
            if (out_epoch != nullptr) *out_epoch = (uint32_t)((tx > ty) ? tx : ty);
            return true;
        }
    }
    return false;
}

bool decideAdoption(const Agreement& a, Source boot, uint32_t now, uint32_t floor, uint32_t local_now,
                    uint32_t* out_epoch) {
    if (out_epoch != nullptr) *out_epoch = 0;
    if (boot == Source::Rtc) return false;   // (a) the battery-kept RTC is the authority this boot

    uint32_t agreed = 0;
    if (!agreementTime(a, local_now, &agreed)) return false;   // (b) two DIFFERENT advert witnesses
    if (plausible(floor) && agreed < floor) return false;      // (c) never undercut the stored floor
    if (!shouldAdopt(now, agreed)) return false;               // nor the clock we already have (never backwards)

    if (out_epoch != nullptr) *out_epoch = agreed;
    return true;
}

bool farFromRtc(uint32_t now, uint32_t peer) {
    if (!plausible(peer)) return false;
    if (!plausible(now)) return true;   // an RTC boot that produced no time: any peer is worth one line
    const uint32_t gap = (peer > now) ? peer - now : now - peer;
    return gap > kRtcWarnGap;
}

bool warnOnce(Warned& w, uint64_t key) {
    if (key == 0) return false;
    const size_t n = sizeof(w.key) / sizeof(w.key[0]);
    for (size_t i = 0; i < n; i++) {
        if (w.key[i] == key) return false;
    }
    w.key[w.next % n] = key;
    w.next            = (uint8_t)((w.next + 1) % n);
    return true;
}

}  // namespace clock
}  // namespace om
