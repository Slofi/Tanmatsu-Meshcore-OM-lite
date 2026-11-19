// SPDX-License-Identifier: MIT
// S515/EST-468 host tests: the message LED's rule (core/message_led_rule.h).
//
// Time in, LED frame out. The failures that would be silent on the glass: blinking with the screen ON, blinking
// with nothing unread, blinking with the row set to Off, a blink that never stops, a 20 s schedule that drifts
// with the main loop, and an I2C write between bursts (the frame must say nothing CHANGED).
#include <stdio.h>
#include <string.h>

#include "message_led_rule.h"

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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,       \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

om::ui::MessageLedIn in(bool off, uint32_t unread, uint8_t led) {
  om::ui::MessageLedIn i;
  i.screen_off = off;
  i.unread     = unread;
  i.led        = led;
  return i;
}

// ---- 1. the Settings value: Off / 1..6, wrapping both ways -------------------------------
void testSettingValue() {
  CHECK(strcmp(om::ui::messageLedLabel(0), "Off") == 0);
  CHECK(strcmp(om::ui::messageLedLabel(1), "1") == 0);
  CHECK(strcmp(om::ui::messageLedLabel(6), "6") == 0);
  CHECK(strcmp(om::ui::messageLedLabel(7), "Off") == 0);   // an invalid byte reads as Off
  CHECK_EQ(om::ui::kMessageLedDefault, 0);
  uint8_t v = 0;
  for (int i = 1; i <= 6; i++) {
    v = om::ui::messageLedCycle(v);
    CHECK_EQ(v, i);
  }
  CHECK_EQ(om::ui::messageLedCycle(v), 0);   // 6 -> Off
  CHECK(om::ui::messageLedValid(0));
  CHECK(om::ui::messageLedValid(6));
  CHECK(!om::ui::messageLedValid(7));
  CHECK_EQ(om::ui::kMessageLedBrightness, 15);
  CHECK_EQ(om::ui::kMessageLedOnMs, 120);
  CHECK_EQ(om::ui::kMessageLedOffMs, 180);
  CHECK_EQ(om::ui::kMessageLedGroupMs, 20000);
  CHECK_EQ(om::ui::kMessageLedBurstMs, 900);
}

// ---- 2. it is active ONLY with the screen off AND unread AND a chosen LED ------------------
void testGate() {
  om::ui::MessageLed rule;
  // Screen on, unread, LED chosen: nothing.
  CHECK(!rule.step(0, in(false, 3, 2)).manual);
  // Screen off but nothing unread: nothing.
  CHECK(!rule.step(0, in(true, 0, 2)).manual);
  // Screen off, unread, but Off: nothing.
  CHECK(!rule.step(0, in(true, 3, 0)).manual);
  // An out-of-range value is Off too.
  CHECK(!rule.step(0, in(true, 3, 7)).manual);
  // All three: manual, the chosen LED, first blink on.
  const om::ui::MessageLedFrame f = rule.step(0, in(true, 3, 2));
  CHECK(f.manual);
  CHECK(f.lit);
  CHECK_EQ(f.led, 2);
  CHECK(f.changed);
}

// ---- 3. three short blinks at once, then every 20 s, with NO change between bursts ----------
void testBurstSchedule() {
  om::ui::MessageLed rule;
  // Blink 1: on [0,120), off [120,300). Blink 2: on [300,420). Blink 3: on [600,720).
  CHECK(rule.step(0, in(true, 1, 3)).lit);
  CHECK(rule.step(119, in(true, 1, 3)).lit);
  CHECK(!rule.step(120, in(true, 1, 3)).lit);
  CHECK(rule.step(300, in(true, 1, 3)).lit);
  CHECK(!rule.step(420, in(true, 1, 3)).lit);
  CHECK(rule.step(600, in(true, 1, 3)).lit);
  CHECK(!rule.step(720, in(true, 1, 3)).lit);
  // The long dark gap: no change, so the shell writes nothing.
  const uint32_t gap[] = {900, 1000, 5000, 12345, 19999};
  for (uint32_t t : gap) {
    const om::ui::MessageLedFrame f = rule.step(t, in(true, 1, 3));
    CHECK(f.manual);
    CHECK(!f.lit);
    CHECK(!f.changed);
  }
  // 20 s after the first burst started: the next burst, at once.
  const om::ui::MessageLedFrame next = rule.step(20000, in(true, 1, 3));
  CHECK(next.lit);
  CHECK(next.changed);
  CHECK(rule.step(40300, in(true, 1, 3)).lit);   // 20 s + 300 ms = blink 2 of the second burst
}

// ---- 4. it STOPS the moment the screen goes on, the unread drops, or Off is chosen ---------
void testStops() {
  {
    om::ui::MessageLed rule;
    CHECK(rule.step(0, in(true, 5, 1)).manual);
    const om::ui::MessageLedFrame f = rule.step(50, in(false, 5, 1));
    CHECK(!f.manual);
    CHECK(!f.lit);
    CHECK(f.changed);
    CHECK(!rule.step(75, in(false, 5, 1)).changed);   // stays handed back
  }
  {
    om::ui::MessageLed rule;
    CHECK(rule.step(0, in(true, 5, 1)).manual);
    CHECK(!rule.step(50, in(true, 0, 1)).manual);
  }
  {
    om::ui::MessageLed rule;
    CHECK(rule.step(0, in(true, 5, 1)).manual);
    CHECK(!rule.step(50, in(true, 5, 0)).manual);
  }
  // Re-arming after a stop starts a fresh burst AT ONCE (not on the old 20 s grid).
  {
    om::ui::MessageLed rule;
    CHECK(rule.step(0, in(true, 5, 1)).lit);
    CHECK(rule.step(400, in(true, 5, 1)).lit);      // blink 2
    CHECK(rule.step(1000, in(false, 5, 1)).manual == false);
    const om::ui::MessageLedFrame f = rule.step(1050, in(true, 5, 1));
    CHECK(f.manual);
    CHECK(f.lit);
  }
}

// ---- 5. a LED choice change while active restarts the burst on the new LED -----------------
void testChoiceChange() {
  om::ui::MessageLed rule;
  CHECK(rule.step(0, in(true, 1, 1)).lit);
  CHECK(rule.step(5000, in(true, 1, 1)).manual);   // dark gap
  const om::ui::MessageLedFrame f = rule.step(5001, in(true, 1, 4));
  CHECK(f.manual);
  CHECK(f.lit);
  CHECK_EQ(f.led, 4);
  CHECK(f.changed);
}

// ---- 6. the Settings preview: three blinks, then the LEDs go back --------------------------
void testPreview() {
  om::ui::MessageLed rule;
  CHECK(!rule.step(0, in(false, 0, 0)).manual);
  rule.preview(1000, 4);
  om::ui::MessageLedFrame f = rule.step(1000, in(false, 0, 0));
  CHECK(f.manual);
  CHECK(f.lit);
  CHECK_EQ(f.led, 4);
  CHECK(f.changed);
  CHECK(rule.step(1120, in(false, 0, 0)).lit == false);
  CHECK(rule.step(1300, in(false, 0, 0)).lit);     // blink 2
  CHECK(rule.step(1600, in(false, 0, 0)).lit);     // blink 3
  CHECK(rule.step(1720, in(false, 0, 0)).lit == false);
  const om::ui::MessageLedFrame back = rule.step(1900, in(false, 0, 0));
  CHECK(!back.manual);
  CHECK(!back.lit);
  CHECK(back.changed);
  CHECK(!rule.step(1925, in(false, 0, 0)).changed);

  // A second preview while one is running restarts on the new LED.
  rule.preview(0, 2);
  CHECK(rule.step(100, in(false, 0, 0)).lit);
  rule.preview(150, 5);
  f = rule.step(150, in(false, 0, 0));
  CHECK(f.manual);
  CHECK(f.lit);
  CHECK_EQ(f.led, 5);

  // An invalid preview request is ignored.
  om::ui::MessageLed rule2;
  rule2.preview(0, 0);
  CHECK(!rule2.step(10, in(false, 0, 0)).manual);
}

// ---- 7. the frame grid is independent of how often the shell calls step() ------------------
void testPollIndependence() {
  // A 25 ms poll, like the task's, must produce the same lit schedule as an exact-time query.
  om::ui::MessageLed polled;
  int lit_start = -1, lit_stop = -1;
  for (uint32_t t = 0; t <= 30000; t += 25) {
    const bool lit = polled.step(t, in(true, 2, 6)).lit;
    if (lit && lit_start < 0) lit_start = (int)t;
    if (!lit && lit_start >= 0 && lit_stop < 0) lit_stop = (int)t;
  }
  CHECK_EQ(lit_start, 0);
  CHECK_EQ(lit_stop, 125);   // the 120 ms on window ends at the first 25 ms sample past it
  om::ui::MessageLed exact;
  CHECK(exact.step(0, in(true, 2, 6)).lit);
  CHECK(exact.step(119, in(true, 2, 6)).lit);
  CHECK(exact.step(120, in(true, 2, 6)).lit == false);
}

// ---- 8. a 32-bit millisecond wrap must not stall the schedule -------------------------------
void testTimeWrap() {
  const uint32_t base = 0xFFFFFF00u;
  om::ui::MessageLed rule;
  CHECK(rule.step(base, in(true, 1, 1)).lit);
  CHECK(!rule.step(base + 120u, in(true, 1, 1)).lit);    // wraps past 2^32
  CHECK(rule.step(base + 300u, in(true, 1, 1)).lit);
}

}  // namespace

int main() {
  testSettingValue();
  testGate();
  testBurstSchedule();
  testStops();
  testChoiceChange();
  testPreview();
  testPollIndependence();
  testTimeWrap();
  if (g_failures != 0) {
    printf("message-led: %d/%d checks FAILED\n", g_failures, g_checks);
    return 1;
  }
  printf("message-led: %d checks, 0 failures\n", g_checks);
  return 0;
}
