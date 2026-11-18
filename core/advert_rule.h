// SPDX-License-Identifier: MIT
// How often this node may announce itself: at most ONE advert per 5 s (S513/EST-435).
//
// Why it is a rule and not a counter in `mc_link.cpp`: Filip flooded the mesh with five adverts by pressing the
// key five times ("limit one advert to 5 seconds"). A flood advert is a signed broadcast every node repeats, so a
// held key is not a local annoyance — it is traffic on a shared, duty-cycled band. The decision must therefore be
// the same in three places that can ask for an advert (the Nodes page's 'a', the start menu's 'a', the 3-hour
// schedule) and it must be provable without a radio: so it lives here, takes `now` as an ARGUMENT, and never calls
// a clock, NVS or the radio itself.
//
// The clock the caller passes is MONOTONIC milliseconds since boot (the device uses esp_timer). It restarts at 0
// on every boot, which is correct here: a fresh boot has not transmitted anything yet.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace mc {

// 5 s, as the ticket asks. One constant: the rule, the notice text and the tests all read it.
constexpr uint32_t kAdvertGapMs = 5000;

// What the app knows about our own adverts. No clock inside, no policy inside: three values the rule reads.
struct AdvertGate {
    bool     ever_sent  = false;   // we have transmitted at least one advert this boot
    bool     in_flight  = false;   // one is queued (or on the air) and its result is not known yet
    uint32_t last_tx_ms = 0;       // monotonic ms of the last advert that radio::send() said it took
};

enum class AdvertVerdict : uint8_t {
    Send = 0,   // allowed now
    Wait = 1,   // too soon after the last TRANSMITTED advert; `wait_s` says how long
    Busy = 2,   // an advert is still queued/on the air: refused even if the 5 s have passed
};

struct AdvertDecision {
    AdvertVerdict verdict = AdvertVerdict::Send;
    uint32_t      wait_s  = 0;   // whole seconds left, rounded UP; 0 unless verdict == Wait
};

// May we send an advert at `now_ms`? Pure: reads the gate, returns the decision.
// The order matters: a queued advert is refused FIRST, because the 5 s clock has not even started for it.
AdvertDecision advertRequest(const AdvertGate& g, uint32_t now_ms);

// A request was accepted and queued. Call with the send queue's success, never on a refusal.
void advertQueued(AdvertGate& g);

// The radio task resolved the queued advert. `ok` is radio::send() > 0: only a real transmission starts the 5 s
// clock. A failed attempt clears `in_flight` and leaves `last_tx_ms` alone — nothing went on the air.
void advertResolved(AdvertGate& g, uint32_t now_ms, bool ok);

// The sentence the refusal shows, so the wording is pinned by a test instead of living in a key handler.
// Returns false (and writes an empty string) when the decision is Send — a send has nothing to say.
bool advertRefusalText(const AdvertDecision& d, char* out, size_t n);

}  // namespace mc
}  // namespace om
