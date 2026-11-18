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

#include <stddef.h>
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

// Is a received timestamp plausible AND worth moving to (forward by more than kAdoptSlack)?
// ⚠️ Since S513/EST-429 this is only the LAST gate: a timestamp alone is not a time source. A verified
// advert proves who signed it, never that their clock is right, so adoption also needs two agreeing advert
// witnesses (decideAdoption below) and no RTC boot. Kept separate because it is the "never backwards" rule.
bool shouldAdopt(uint32_t now, uint32_t seen);

// Should the coprocessor's RTC be corrected to `known`?
bool shouldWriteBack(uint32_t rtc, uint32_t known);

// Should `now` be stored in NVS (so the next cold boot starts from it)?
bool shouldPersist(uint32_t now, uint32_t stored);

// The advert timestamp: never lower than one we have already sent (MeshCore's replay protection compares
// against the stored value), and one second past the floor if our own clock is behind it.
uint32_t advertFloor(uint32_t now, uint32_t floor);

// ---- Mesh time agreement (S513/EST-429) ------------------------------------------------------------
//
// A verified advert proves WHO signed it, NEVER that their clock is right. One node with a clock in 2030
// used to move us there for good: shouldAdopt() only goes forward, chooseStart() takes the later of the
// RTC and the stored floor, and the advert floor (mcid/advts) is monotonic. So a mesh time is now a
// CANDIDATE until two DIFFERENT advert keys agree on it; a channel text never counts (its sender name is
// self-declared and a channel key can be public), and an RTC boot adopts nothing at all.
//
// The table is small and FIXED on purpose: 8 recent advert samples is plenty for a leaf node, kept one
// per public-key prefix so a repeat from one chatty node replaces its own slot instead of crowding out
// the second witness. `local` is OUR monotonic second, so projecting a sample forward still works after
// our wall clock jumps (and is what "after accounting for when each was heard" means).
constexpr size_t   kAgreementSlots  = 8;
constexpr uint32_t kAgreementWindow = 120u;  // s: two independent nodes "agree" within this
constexpr uint32_t kRtcWarnGap      = 600u;  // s: on an RTC boot, a peer this far off is logged once

enum class Origin : uint8_t {
    Advert,  // a signed node advert: the only origin that may count toward agreement
    Text,    // a channel text: the MAC proves the key, not the time -> accepted and ignored
};

struct Sample {
    uint64_t key   = 0;   // first 8 bytes of the advert's public key; 0 = free slot
    uint32_t seen  = 0;   // the epoch the peer claimed
    uint32_t local = 0;   // our MONOTONIC second when it was heard (not the wall clock: it does not jump)
};

struct Agreement {
    Sample  slot[kAgreementSlots] = {};
    uint8_t next                  = 0;   // ring write index
};

// Record one sample. A text (Origin::Text) is accepted and dropped: there is no key to tell two senders
// apart. Implausible times and a zero key are dropped.
void agreementRecord(Agreement& a, Origin origin, uint64_t key, uint32_t seen, uint32_t local_s);

// One sample projected to `local_now`: seen + the monotonic seconds since it was heard.
// Returns 0 when the result cannot be a time (so callers can simply use plausible()).
uint32_t projectSample(uint32_t seen, uint32_t local_s, uint32_t local_now);

// Do at least two DIFFERENT advert keys agree within kAgreementWindow (after projection)? If so, write
// the LATER of the two projections to *out_epoch: the safer side, never undercutting a correct clock.
bool agreementTime(const Agreement& a, uint32_t local_now, uint32_t* out_epoch);

// The whole adoption rule in one place: (a) an RTC boot adopts nothing, (b) two different advert keys
// must agree, (c) the agreed time may not undercut the stored floor and still has to move us forward.
bool decideAdoption(const Agreement& a, Source boot, uint32_t now, uint32_t floor, uint32_t local_now,
                    uint32_t* out_epoch);

// Rule (a)'s log line: is a peer's projected time more than kRtcWarnGap from ours?
bool farFromRtc(uint32_t now, uint32_t peer);

// Rule (a)'s "once per sender": a tiny fixed set of advert key prefixes already reported. True the first
// time a key is seen, false afterwards. A full table reuses its oldest slot (a key may be reported again
// after 16 different senders — acceptable for a log line).
struct Warned {
    uint64_t key[16] = {};
    uint8_t  next    = 0;
};
bool warnOnce(Warned& w, uint64_t key);

}  // namespace clock
}  // namespace om
