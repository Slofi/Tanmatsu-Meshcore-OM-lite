// SPDX-License-Identifier: MIT
// Host tests for the advert rate rule (S513/EST-435). The device asks this same code before it queues an advert,
// so what is pinned here is exactly what happens when a key is pressed five times in a row:
//   the first press sends; every press inside 5 s is refused with the whole seconds left; +5.0 s sends again; a
//   second advert is refused while the first is still queued; and a FAILED send does not start the 5 s clock.
#include <stdio.h>
#include <string.h>

#include "advert_rule.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                       \
      return;                                                             \
    }                                                                     \
  } while (0)

#define CHECK_EQ(got, want)                                                          \
  do {                                                                               \
    ++g_checks;                                                                      \
    const long g_ = static_cast<long>(got);                                          \
    const long w_ = static_cast<long>(want);                                         \
    if (g_ != w_) {                                                                  \
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,        \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

constexpr uint32_t kGap = om::mc::kAdvertGapMs;   // 5000, from the rule itself (not copied)

// 1. The first press sends, and the five presses Filip made are the case in the ticket: +0.0, +0.4, +0.9, +1.5,
//    +2.0 s must produce ONE transmission and four refusals.
void firstPressSendsTheRestRefused() {
    om::mc::AdvertGate g;
    om::mc::AdvertDecision d = om::mc::advertRequest(g, 1000);
    CHECK(d.verdict == om::mc::AdvertVerdict::Send);
    CHECK_EQ(d.wait_s, 0);
    om::mc::advertQueued(g);
    CHECK(g.in_flight);
    om::mc::advertResolved(g, 1300, true);   // on the air 300 ms later
    CHECK(!g.in_flight);
    CHECK(g.ever_sent);

    // the four presses that used to flood the mesh
    const uint32_t press_ms[] = {1300, 1700, 2200, 2800, 3300};
    const uint32_t want_s[]   = {5, 5, 5, 4, 3};
    for (size_t i = 0; i < sizeof(press_ms) / sizeof(press_ms[0]); i++) {
        d = om::mc::advertRequest(g, press_ms[i]);
        CHECK(d.verdict == om::mc::AdvertVerdict::Wait);
        CHECK_EQ(d.wait_s, want_s[i]);
    }
    // nothing above queued or transmitted anything, so the gate still names the SAME transmission
    CHECK_EQ(g.last_tx_ms, 1300);
}

// 2. Every tenth of a second from +1.0 to +4.9 s is refused, with the whole seconds rounded UP; +5.0 s sends.
//    This is the boundary the ticket names, walked at 100 ms steps so an off-by-one in the ceil cannot hide.
void boundaryWalk() {
    om::mc::AdvertGate g;
    om::mc::advertQueued(g);
    om::mc::advertResolved(g, 0, true);   // transmitted at t = 0

    for (uint32_t ms = 1000; ms <= 4900; ms += 100) {
        const om::mc::AdvertDecision d = om::mc::advertRequest(g, ms);
        CHECK(d.verdict == om::mc::AdvertVerdict::Wait);
        const uint32_t want = (kGap - ms + 999u) / 1000u;   // the ticket's "whole seconds left"
        CHECK_EQ(d.wait_s, want);
        CHECK(d.wait_s >= 1);   // never "wait 0 s": a zero would read as permission
    }
    // the last millisecond before the gap is still a refusal, and it says "1 s"
    om::mc::AdvertDecision d = om::mc::advertRequest(g, 4999);
    CHECK(d.verdict == om::mc::AdvertVerdict::Wait);
    CHECK_EQ(d.wait_s, 1);
    // exactly 5.0 s sends
    d = om::mc::advertRequest(g, 5000);
    CHECK(d.verdict == om::mc::AdvertVerdict::Send);
    CHECK_EQ(d.wait_s, 0);
}

// 3. A second advert is refused while the first is still in the queue — even long after the 5 s — and the
//    refusal's text says "queued", not "sent", because nothing has been transmitted yet.
void queuedAdvertBlocksASecond() {
    om::mc::AdvertGate g;
    om::mc::advertQueued(g);
    const om::mc::AdvertDecision d = om::mc::advertRequest(g, 600000);   // ten minutes later
    CHECK(d.verdict == om::mc::AdvertVerdict::Busy);
    CHECK_EQ(d.wait_s, 0);

    char text[64];
    CHECK(om::mc::advertRefusalText(d, text, sizeof(text)));
    CHECK(strcmp(text, "Advert already queued") == 0);
}

// 4. A FAILED send does not start the 5 s clock: nothing went on the air. It also clears in_flight, or the node
//    could never try again.
void failedSendDoesNotStartTheClock() {
    om::mc::AdvertGate g;
    om::mc::advertQueued(g);
    om::mc::advertResolved(g, 2000, false);
    CHECK(!g.in_flight);
    CHECK(!g.ever_sent);
    CHECK_EQ(g.last_tx_ms, 0);
    const om::mc::AdvertDecision d = om::mc::advertRequest(g, 2001);
    CHECK(d.verdict == om::mc::AdvertVerdict::Send);
}

// 5. The refusal sentence, pinned word for word: "Advert already sent, wait N s" (the ticket's text).
void refusalTextIsTheTickets() {
    om::mc::AdvertGate g;
    om::mc::advertQueued(g);
    om::mc::advertResolved(g, 0, true);
    char text[64];

    om::mc::AdvertDecision d = om::mc::advertRequest(g, 1000);
    CHECK(om::mc::advertRefusalText(d, text, sizeof(text)));
    CHECK(strcmp(text, "Advert already sent, wait 4 s") == 0);

    d = om::mc::advertRequest(g, 4500);
    CHECK(om::mc::advertRefusalText(d, text, sizeof(text)));
    CHECK(strcmp(text, "Advert already sent, wait 1 s") == 0);

    // a send has nothing to say, and an empty buffer is never written past
    d = om::mc::advertRequest(g, 5000);
    CHECK(!om::mc::advertRefusalText(d, text, sizeof(text)));
    CHECK_EQ(text[0], 0);
    CHECK(!om::mc::advertRefusalText(d, nullptr, 0));
}

// 6. The clock is a 32-bit monotonic counter and wraps after ~49 days: the gap must survive the wrap.
void wrapSafe() {
    om::mc::AdvertGate g;
    om::mc::advertQueued(g);
    om::mc::advertResolved(g, 0xFFFFFF00u, true);   // 256 ms before the wrap
    om::mc::AdvertDecision d = om::mc::advertRequest(g, 0x00000100u);   // 512 ms later, across the wrap
    CHECK(d.verdict == om::mc::AdvertVerdict::Wait);
    CHECK_EQ(d.wait_s, 5);
    d = om::mc::advertRequest(g, 0x0000126Cu);   // 4748 ms after 0xFFFFFF00 -> 252 ms still to go
    CHECK(d.verdict == om::mc::AdvertVerdict::Wait);
    CHECK_EQ(d.wait_s, 1);
}

// 7. The 3-hour schedule asks the SAME rule: a manual advert 2 s ago defers it, and it is not refused forever —
//    once the 5 s have passed the schedule's own request is allowed again.
void scheduleRespectsTheSameLimit() {
    om::mc::AdvertGate g;
    om::mc::advertQueued(g);
    om::mc::advertResolved(g, 100000, true);   // the user pressed 'a'
    CHECK(om::mc::advertRequest(g, 102000).verdict == om::mc::AdvertVerdict::Wait);   // schedule would ask here
    CHECK(om::mc::advertRequest(g, 104999).verdict == om::mc::AdvertVerdict::Wait);
    CHECK(om::mc::advertRequest(g, 105000).verdict == om::mc::AdvertVerdict::Send);   // and here
}

}  // namespace

int main() {
    firstPressSendsTheRestRefused();
    boundaryWalk();
    queuedAdvertBlocksASecond();
    failedSendDoesNotStartTheClock();
    refusalTextIsTheTickets();
    wrapSafe();
    scheduleRespectsTheSameLimit();
    if (g_failures == 0) {
        printf("test_advert: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    printf("test_advert: %d checks, %d FAILURES\n", g_checks, g_failures);
    return 1;
}
