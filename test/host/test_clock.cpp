// SPDX-License-Identifier: MIT
// Host tests for the app's clock RULES (S502). Real exit code: 0 = every check passed.
//
// Each rule is tested on both sides of its boundary, because every one of them is a boundary: a timestamp that
// is 59 seconds ahead is noise, 61 is a time source; a floor that is 1 second higher must not be undercut.
// The failure this guards against is silent by nature (an advert that peers drop and nothing on our screen
// shows), so the tests care about the edges, not about the happy path alone.
//
// Build/run: sh test/host/run.sh (g++ -std=c++17 -Wall -Wextra -Werror).
#include <stdio.h>

#include "clock.h"

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

// Real, plausible instants used as fixtures (all UTC).
constexpr uint32_t kNow2026      = 1794603776u;   // 2026-11-13, the L2's value for us in S501
constexpr uint32_t kBoot1970     = 6u;            // the P4 after a power loss ("ts 6", measured S501)
constexpr uint32_t kJustUnderMin = om::clock::kMinPlausible - 1;
constexpr uint32_t kJustOverMax  = om::clock::kMaxPlausible + 1;

void test_plausible_window() {
  CHECK_EQ(om::clock::plausible(0), false);
  CHECK_EQ(om::clock::plausible(kBoot1970), false);
  CHECK_EQ(om::clock::plausible(kJustUnderMin), false);
  CHECK_EQ(om::clock::plausible(om::clock::kMinPlausible), true);   // the boundary is INSIDE
  CHECK_EQ(om::clock::plausible(kNow2026), true);
  CHECK_EQ(om::clock::plausible(om::clock::kMaxPlausible), true);
  CHECK_EQ(om::clock::plausible(kJustOverMax), false);
  CHECK_EQ(om::clock::plausible(0xFFFFFFFFu), false);               // a blank NVS / bit flip
}

void test_boot_choice() {
  using om::clock::Source;
  // The good case: the RTC survived the power loss.
  om::clock::Start a = om::clock::chooseStart(kNow2026, 0);
  CHECK(a.source == Source::Rtc);
  CHECK_EQ(a.epoch, kNow2026);

  // The RTC is empty (the observed failure) and we have a stored floor.
  om::clock::Start b = om::clock::chooseStart(kBoot1970, kNow2026 - 3600);
  CHECK(b.source == Source::Stored);
  CHECK_EQ(b.epoch, kNow2026 - 3600);

  // Both usable: the LATER one, because both mean "true at some moment in the past".
  om::clock::Start c = om::clock::chooseStart(kNow2026 - 7200, kNow2026 - 600);
  CHECK(c.source == Source::Stored);
  CHECK_EQ(c.epoch, kNow2026 - 600);
  om::clock::Start d = om::clock::chooseStart(kNow2026 - 600, kNow2026 - 7200);
  CHECK(d.source == Source::Rtc);
  CHECK_EQ(d.epoch, kNow2026 - 600);
  // A tie keeps the RTC: it is the clock that kept running.
  om::clock::Start e = om::clock::chooseStart(kNow2026, kNow2026);
  CHECK(e.source == Source::Rtc);

  // Nothing usable: say so rather than invent a time.
  om::clock::Start f = om::clock::chooseStart(kBoot1970, 0);
  CHECK(f.source == Source::None);
  CHECK_EQ(f.epoch, 0u);
  // …and a corrupt stored value does not become the time either.
  om::clock::Start g = om::clock::chooseStart(0, kJustOverMax);
  CHECK(g.source == Source::None);
}

void test_adopt_boundaries() {
  const uint32_t now = kNow2026;
  CHECK_EQ(om::clock::shouldAdopt(now, now), false);                                  // equal is not newer
  CHECK_EQ(om::clock::shouldAdopt(now, now + om::clock::kAdoptSlack), false);         // exactly the slack
  CHECK_EQ(om::clock::shouldAdopt(now, now + om::clock::kAdoptSlack + 1), true);      // one second past it
  CHECK_EQ(om::clock::shouldAdopt(now, now + 86400), true);                           // a day ahead: adopt
  CHECK_EQ(om::clock::shouldAdopt(now, now - 1), false);                              // never go backwards
  CHECK_EQ(om::clock::shouldAdopt(now, now - 86400 * 30), false);

  // Our clock unset (no time at all): any plausible timestamp wins, the first one included.
  CHECK_EQ(om::clock::shouldAdopt(0, kNow2026), true);
  CHECK_EQ(om::clock::shouldAdopt(kBoot1970, kNow2026), true);
  // …but an implausible one never does, whatever ours says.
  CHECK_EQ(om::clock::shouldAdopt(0, kBoot1970), false);
  CHECK_EQ(om::clock::shouldAdopt(0, kJustOverMax), false);
  CHECK_EQ(om::clock::shouldAdopt(now, 0xFFFFFFFFu), false);
  // A clock that is nonsense in OUR direction is not compared (no useful "newer" exists).
  CHECK_EQ(om::clock::shouldAdopt(kJustOverMax, kNow2026), false);
}

void test_write_back_boundaries() {
  const uint32_t t = kNow2026;
  CHECK_EQ(om::clock::shouldWriteBack(0, t), true);                                   // an empty RTC gets a time
  CHECK_EQ(om::clock::shouldWriteBack(t, t), false);
  CHECK_EQ(om::clock::shouldWriteBack(t, t + om::clock::kWriteBackGap), false);        // exactly the gap
  CHECK_EQ(om::clock::shouldWriteBack(t, t + om::clock::kWriteBackGap + 1), true);
  CHECK_EQ(om::clock::shouldWriteBack(t + om::clock::kWriteBackGap + 1, t), true);    // RTC ahead: also wrong
  CHECK_EQ(om::clock::shouldWriteBack(t, kBoot1970), false);                          // we learned nothing usable
  CHECK_EQ(om::clock::shouldWriteBack(kBoot1970, 0), false);
}

void test_persist_boundaries() {
  const uint32_t t = kNow2026;
  CHECK_EQ(om::clock::shouldPersist(t, 0), true);                                     // nothing stored yet
  CHECK_EQ(om::clock::shouldPersist(t, t), false);
  CHECK_EQ(om::clock::shouldPersist(t, t + om::clock::kPersistGap), false);            // exactly the gap
  CHECK_EQ(om::clock::shouldPersist(t + om::clock::kPersistGap + 1, t), true);
  CHECK_EQ(om::clock::shouldPersist(t - 1, t), false);                                // never store a LOWER time
  CHECK_EQ(om::clock::shouldPersist(kBoot1970, 0), false);                            // 1970 is not a time
  CHECK_EQ(om::clock::shouldPersist(t, kJustOverMax), true);                          // a corrupt floor is replaced
}

void test_advert_floor() {
  const uint32_t floor = kNow2026 - 1000;
  // The normal case: our clock is ahead of the floor, so the advert carries the real time.
  CHECK_EQ(om::clock::advertFloor(kNow2026, floor), kNow2026);
  // Our clock fell behind (a power loss, a reboot): one second past the floor, never the 1970 value.
  CHECK_EQ(om::clock::advertFloor(kBoot1970, floor), floor + 1);
  CHECK_EQ(om::clock::advertFloor(floor, floor), floor + 1);      // equal is not newer (MeshCore's rule)
  // Nothing has ever been sent AND the clock is unset: there is no better answer than the clock we have.
  CHECK_EQ(om::clock::advertFloor(kBoot1970, 0), kBoot1970);
  // A corrupt floor is ignored rather than poisoning the timestamp.
  CHECK_EQ(om::clock::advertFloor(kNow2026, 0xFFFFFFFFu), kNow2026);
  CHECK_EQ(om::clock::advertFloor(kBoot1970, kJustOverMax), kBoot1970);
  // A plausible clock below the minimum is not used as a timestamp when a floor exists (the whole point).
  CHECK_EQ(om::clock::advertFloor(om::clock::kMinPlausible - 1, floor), floor + 1);
}

// S513/EST-429: a mesh time is a CANDIDATE, not a clock. Every check below is one of the ticket's six
// cases; together they define "one wrong node cannot move us into the future". Armed against the OLD rule
// (any plausible timestamp more than 60 s ahead, whatever its origin): cases 1, 2, 4, 5 and 6 turn red.
void test_mesh_agreement() {
  namespace clk = om::clock;
  constexpr uint64_t kNodeA = 0x0A0B0C0D0E0F1011ull;   // two advert public-key prefixes
  constexpr uint64_t kNodeB = 0x1A1B1C1D1E1F2021ull;
  const uint32_t     t      = kNow2026;
  const uint32_t     local0 = 10000;                        // our monotonic seconds when A was heard
  const uint32_t     far    = t + 5u * 365u * 24u * 3600u;  // five years ahead (still < kMaxPlausible)

  // (1) ONE node five years ahead, on an RTC boot: nothing changes. The disagreement is worth one log line
  //     per sender (farFromRtc / warnOnce), never an adoption.
  {
    clk::Agreement a{};
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, far, local0);
    uint32_t out = 1234;
    CHECK_EQ(clk::decideAdoption(a, clk::Source::Rtc, t, t, local0, &out), false);
    CHECK_EQ(out, 0u);
    CHECK_EQ(clk::farFromRtc(t, far), true);
    CHECK_EQ(clk::farFromRtc(t, t + clk::kRtcWarnGap), false);      // exactly 10 min off is not "off"
    CHECK_EQ(clk::farFromRtc(t, t + clk::kRtcWarnGap + 1), true);
    CHECK_EQ(clk::farFromRtc(t, t - clk::kRtcWarnGap - 1), true);   // behind is a disagreement too
    // The same node twice is still one sender, even on an RTC boot.
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, far + 30, local0 + 30);
    CHECK_EQ(clk::decideAdoption(a, clk::Source::Rtc, t, t, local0 + 30, &out), false);
    clk::Warned w{};
    CHECK_EQ(clk::warnOnce(w, kNodeA), true);
    CHECK_EQ(clk::warnOnce(w, kNodeA), false);
    CHECK_EQ(clk::warnOnce(w, kNodeB), true);
  }

  // (2) ONE node ahead with NO source (or only a stored floor): not adopted either — one witness is not two.
  {
    clk::Agreement a{};
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, t + 3600, local0);
    uint32_t out = 1234;
    CHECK_EQ(clk::decideAdoption(a, clk::Source::None, 0, 0, local0 + 30, &out), false);
    CHECK_EQ(clk::decideAdoption(a, clk::Source::None, kBoot1970, 0, local0 + 30, &out), false);
    CHECK_EQ(clk::decideAdoption(a, clk::Source::Stored, t - 3600, 0, local0 + 30, &out), false);
  }

  // (3) TWO different nodes agreeing: adopted, and the times are compared AFTER projecting each sample by
  //     how long ago it was heard. The LATER projection wins (never undercut a correct clock).
  {
    clk::Agreement a{};
    const uint32_t near = t + 3600;
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, near, local0);
    clk::agreementRecord(a, clk::Origin::Advert, kNodeB, near + 60, local0 + 10);
    uint32_t out = 0;
    CHECK_EQ(clk::agreementTime(a, local0 + 30, &out), true);
    CHECK_EQ(out, near + 80);   // A projects to near+30, B to near+80
    out = 0;
    CHECK_EQ(clk::decideAdoption(a, clk::Source::None, kBoot1970, 0, local0 + 30, &out), true);
    CHECK_EQ(out, near + 80);

    // The 120 s window is a boundary: exactly 120 agrees, 121 does not.
    clk::Agreement edge{};
    clk::agreementRecord(edge, clk::Origin::Advert, kNodeA, near, local0);
    clk::agreementRecord(edge, clk::Origin::Advert, kNodeB, near + clk::kAgreementWindow, local0);
    CHECK_EQ(clk::agreementTime(edge, local0, &out), true);
    clk::Agreement over{};
    clk::agreementRecord(over, clk::Origin::Advert, kNodeA, near, local0);
    clk::agreementRecord(over, clk::Origin::Advert, kNodeB, near + clk::kAgreementWindow + 1, local0);
    CHECK_EQ(clk::agreementTime(over, local0, &out), false);

    // Heard 10 min apart but claiming the SAME instant: the projection, not the raw ts, is what agrees.
    clk::Agreement proj{};
    clk::agreementRecord(proj, clk::Origin::Advert, kNodeA, near, local0);
    clk::agreementRecord(proj, clk::Origin::Advert, kNodeB, near + 600, local0 + 600);
    CHECK_EQ(clk::agreementTime(proj, local0 + 600, &out), true);
    CHECK_EQ(out, near + 600);
  }

  // (4) The SAME node twice: one witness, no adoption — even when its two own timestamps agree perfectly.
  {
    clk::Agreement a{};
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, t + 3600, local0);
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, t + 3605, local0 + 5);
    uint32_t out = 1234;
    CHECK_EQ(clk::agreementTime(a, local0 + 10, &out), false);
    CHECK_EQ(clk::decideAdoption(a, clk::Source::None, kBoot1970, 0, local0 + 10, &out), false);
  }

  // (5) Two different nodes TEN MINUTES apart: still no agreement (the window is 120 s, not "both say 2026").
  {
    clk::Agreement a{};
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, t + 3600, local0);
    clk::agreementRecord(a, clk::Origin::Advert, kNodeB, t + 3600 + 600, local0);
    uint32_t out = 1234;
    CHECK_EQ(clk::agreementTime(a, local0, &out), false);
    CHECK_EQ(clk::decideAdoption(a, clk::Source::None, kBoot1970, 0, local0, &out), false);
  }

  // (6) A channel text alone: never a witness, however many senders there are; and one advert plus one text
  //     is still only ONE witness. (A MAC proves the channel key, not the sender and not the time.)
  {
    const uint64_t kText = 0xDEADBEEFDEADBEEFull;   // any key argument must be ignored for a text
    clk::Agreement a{};
    clk::agreementRecord(a, clk::Origin::Text, kText, t + 3600, local0);
    clk::agreementRecord(a, clk::Origin::Text, kText + 1, t + 3600, local0);
    uint32_t out = 1234;
    CHECK_EQ(clk::agreementTime(a, local0, &out), false);
    CHECK_EQ(clk::decideAdoption(a, clk::Source::None, kBoot1970, 0, local0, &out), false);

    clk::Agreement b{};
    clk::agreementRecord(b, clk::Origin::Advert, kNodeA, t + 3600, local0);
    clk::agreementRecord(b, clk::Origin::Text, kText, t + 3600, local0);
    CHECK_EQ(clk::decideAdoption(b, clk::Source::None, kBoot1970, 0, local0, &out), false);
  }

  // (c) The stored floor still wins over two agreeing witnesses, and the clock still never goes backwards.
  {
    const uint32_t near = t + 3600;
    clk::Agreement a{};
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, near, local0);
    clk::agreementRecord(a, clk::Origin::Advert, kNodeB, near + 5, local0);
    uint32_t out = 1234;
    CHECK_EQ(clk::decideAdoption(a, clk::Source::Stored, near - 3600, near + 10, local0, &out), false);
    CHECK_EQ(clk::decideAdoption(a, clk::Source::Stored, near + 7200, 0, local0, &out), false);
    // With the floor below and our clock behind, the same two witnesses ARE adopted.
    CHECK_EQ(clk::decideAdoption(a, clk::Source::Stored, near - 3600, near - 7200, local0, &out), true);
    CHECK_EQ(out, near + 5);
  }

  // One slot per key: a node that advertises often cannot flush the second witness out of the fixed table.
  {
    clk::Agreement a{};
    for (uint32_t i = 0; i < 20; i++) {
      clk::agreementRecord(a, clk::Origin::Advert, kNodeA, t + 3600 + i, local0 + i);
    }
    clk::agreementRecord(a, clk::Origin::Advert, kNodeB, t + 3630, local0 + 30);
    uint32_t out = 0;
    CHECK_EQ(clk::agreementTime(a, local0 + 30, &out), true);
  }

  // A time that cannot be one, or a sample with no key, never enters the table.
  {
    clk::Agreement a{};
    clk::agreementRecord(a, clk::Origin::Advert, kNodeA, kBoot1970, local0);   // 1970 is not a time
    clk::agreementRecord(a, clk::Origin::Advert, kNodeB, t + 3600, local0);
    uint32_t out = 0;
    CHECK_EQ(clk::agreementTime(a, local0, &out), false);
    clk::agreementRecord(a, clk::Origin::Advert, 0, t + 3600, local0);        // no key -> no witness
    CHECK_EQ(clk::agreementTime(a, local0, &out), false);
  }

  // projectSample: seen + the monotonic age, and 0 when the result cannot be a time.
  CHECK_EQ(clk::projectSample(t, local0, local0 + 90), t + 90);
  CHECK_EQ(clk::projectSample(t, local0 + 90, local0), t);            // heard "in the future": ages by 0
  CHECK_EQ(clk::projectSample(kNow2026, 0, 0xFFFFFFFFu), 0u);         // would land past 2100: not a time
}

}  // namespace

int main() {
  test_plausible_window();
  test_boot_choice();
  test_adopt_boundaries();
  test_write_back_boundaries();
  test_persist_boundaries();
  test_advert_floor();
  test_mesh_agreement();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
