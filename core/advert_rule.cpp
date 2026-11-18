// SPDX-License-Identifier: MIT
#include "advert_rule.h"

#include <stdio.h>

namespace om {
namespace mc {

AdvertDecision advertRequest(const AdvertGate& g, uint32_t now_ms) {
    AdvertDecision d;
    // A queued-but-not-yet-transmitted advert comes first: its send may still be in the air, and the 5 s clock has
    // not started for it, so "how long until the next one" has no answer yet. Refuse, whatever the clock says.
    if (g.in_flight) {
        d.verdict = AdvertVerdict::Busy;
        return d;
    }
    if (g.ever_sent) {
        // Unsigned subtraction is wrap-safe: the caller's clock is a monotonic 32-bit ms counter that wraps after
        // ~49 days, and "how long since the last advert" must still be right across that.
        const uint32_t elapsed = now_ms - g.last_tx_ms;
        if (elapsed < kAdvertGapMs) {
            const uint32_t left = kAdvertGapMs - elapsed;
            d.verdict = AdvertVerdict::Wait;
            // Whole seconds, rounded UP: with 100 ms left the answer is "wait 1 s", never "wait 0 s" — a 0 would
            // read as "you may send", which is exactly what this rule says you may not do yet.
            d.wait_s = (left + 999u) / 1000u;
            return d;
        }
    }
    d.verdict = AdvertVerdict::Send;
    return d;
}

void advertQueued(AdvertGate& g) { g.in_flight = true; }

void advertResolved(AdvertGate& g, uint32_t now_ms, bool ok) {
    g.in_flight = false;
    if (!ok) return;   // nothing was transmitted: the gap counts from the air, so it does not start here
    g.ever_sent  = true;
    g.last_tx_ms = now_ms;
}

bool advertRefusalText(const AdvertDecision& d, char* out, size_t n) {
    if (out == nullptr || n == 0) return false;
    out[0] = '\0';
    switch (d.verdict) {
        case AdvertVerdict::Wait:
            snprintf(out, n, "Advert already sent, wait %u s", (unsigned)d.wait_s);
            return true;
        case AdvertVerdict::Busy:
            // The ticket's sentence would be untrue here (nothing has been transmitted yet), so this case says what
            // is actually true instead of reusing it.
            snprintf(out, n, "Advert already queued");
            return true;
        case AdvertVerdict::Send:
        default:
            return false;
    }
}

}  // namespace mc
}  // namespace om
