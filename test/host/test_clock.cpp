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

}  // namespace

int main() {
  test_plausible_window();
  test_boot_choice();
  test_adopt_boundaries();
  test_write_back_boundaries();
  test_persist_boundaries();
  test_advert_floor();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
