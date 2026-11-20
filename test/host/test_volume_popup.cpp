// SPDX-License-Identifier: MIT
// S516/EST-478: the volume keys' rules (core/volume_popup.h) and the screen key-lock that ignores them.
//
// Four rules the ticket names, each with exact instants:
//   1. Up/Down step the stored volume by 10, clamped 0..100 (the Settings row's own rule);
//   2. the pop-up text: "Volume 50 %", "Muted" at 0, "Sound off" while the master switch is Off;
//   3. N presses coalesce into ONE NVS write, only after 2 s with no presses;
//   4. a locked screen ignores the volume keys (only the power button acts).
//
// Build/run: sh test/host/run.sh (g++ -std=c++17 -Wall -Wextra -Werror).
#include <stdio.h>
#include <string.h>

#include "screen_power.h"
#include "volume_popup.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,    \
                  #got, #want, g_, w_);                                              \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

#define CHECK_STR(got, want)                                                        \
  do {                                                                              \
    ++g_checks;                                                                     \
    if (strcmp((got), (want)) != 0) {                                               \
      printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (got), (want)); \
      ++g_failures;                                                                 \
      return;                                                                       \
    }                                                                               \
  } while (0)

// ---------------------------------------------------------------------------- 1. step + clamp
void test_step_and_clamp() {
  printf("test  volume up/down = 10 %% steps on the stored volume, clamped at both ends\n");
  CHECK_EQ(tm::volume::step(70, tm::volume::kStep), 80);
  CHECK_EQ(tm::volume::step(70, -tm::volume::kStep), 60);
  CHECK_EQ(tm::volume::step(90, tm::volume::kStep), 100);
  CHECK_EQ(tm::volume::step(100, tm::volume::kStep), 100);   // no wrap: 100 up stays 100
  CHECK_EQ(tm::volume::step(10, -tm::volume::kStep), 0);
  CHECK_EQ(tm::volume::step(0, -tm::volume::kStep), 0);      // no wrap: 0 down stays 0
  CHECK_EQ(tm::volume::step(0, tm::volume::kStep), 10);
  CHECK_EQ(tm::volume::step(95, tm::volume::kStep), 100);    // a non-multiple stored value still clamps
  CHECK_EQ(tm::volume::step(5, -tm::volume::kStep), 0);
  CHECK_EQ(tm::volume::step(120, -tm::volume::kStep), 90);  // 120 is first clamped to 100, then -10
  // The step is the Settings row's own function, so the two can never drift.
  CHECK_EQ(tm::volume::step(40, tm::volume::kStep), tm::soundcfg::stepVolume(40, 10));
  CHECK_EQ(tm::volume::kStep, 10);
}

// ---------------------------------------------------------------------------- 2. the pop-up text
void test_popup_text() {
  printf("test  pop-up text: Volume 0/50/100 %%, Muted at 0, Sound off while the master is Off\n");
  char t[32];

  tm::volume::popupText(t, sizeof(t), 0, true);
  CHECK_STR(t, "Muted");
  tm::volume::popupText(t, sizeof(t), 50, true);
  CHECK_STR(t, "Volume 50 %");
  tm::volume::popupText(t, sizeof(t), 100, true);
  CHECK_STR(t, "Volume 100 %");

  // Sound Off WINS over Muted: the master switch is the bigger fact. The level is still what the keys set.
  tm::volume::popupText(t, sizeof(t), 0, false);
  CHECK_STR(t, "Sound off");
  tm::volume::popupText(t, sizeof(t), 50, false);
  CHECK_STR(t, "Sound off");

  CHECK_EQ(tm::volume::barPercent(0), 0);
  CHECK_EQ(tm::volume::barPercent(50), 50);
  CHECK_EQ(tm::volume::barPercent(100), 100);
  CHECK_EQ(tm::volume::barPercent(120), 100);   // clamped for the drawing only

  // The plate: 420x54, centred, 12 px above the 24 px hint bar: 390..444 on the 800x480 glass.
  int x = -1, y = -1;
  tm::volume::popupRect(800, 480, 24, &x, &y);
  CHECK_EQ(tm::volume::kPopupW, 420);
  CHECK_EQ(tm::volume::kPopupH, 54);
  CHECK_EQ(x, 190);
  CHECK_EQ(y, 390);
  // The message pop-up is at most 150 px tall and centred, so its lowest edge is 315: the two fit.
  CHECK(y > 315);
}

// ---------------------------------------------------------------------------- 3. coalesced NVS write
void test_coalesced_save() {
  printf("test  N presses -> ONE NVS write, only after 2 s with no presses\n");
  tm::volume::VolumeSave s;
  CHECK(!s.due(0));

  // Ten presses over 0.9 s: every one moves the deadline, nothing is due while they keep coming.
  for (int i = 1; i <= 10; ++i) s.changed((uint8_t)(i * 10), (uint32_t)(1000 + i * 100));
  CHECK(!s.due(2001));
  CHECK(!s.due(2500));
  CHECK(!s.due(3999));   // the LAST press was at 2000 ms; 1999 ms have passed
  CHECK(s.due(4000));    // exactly 2 s after the last press

  uint8_t written = 0;
  CHECK(s.take(4000, &written));   // ONE write …
  CHECK_EQ(written, 100);          // … carrying the LAST value, not the first
  CHECK(!s.take(4000, &written));  // and not a second one
  CHECK(!s.due(99999));
}

void test_clamp_press_still_moves_the_window() {
  printf("test  a clamp press re-arms the write window but schedules no redundant write\n");
  tm::volume::VolumeSave s;
  s.changed(100, 1000);
  s.touch(2000);              // a 100 % UP press at 2 s: no level change, still a press
  CHECK(!s.due(3999));
  CHECK(s.due(4000));

  uint8_t written = 0;
  CHECK(s.take(4000, &written));
  CHECK_EQ(written, 100);
  s.touch(5000);              // nothing pending: a clamp press schedules nothing
  CHECK(!s.due(9000));
}

// ---------------------------------------------------------------------------- 4. locked ignores
void test_locked_ignores() {
  printf("test  locked screen (key-lock) ignores the volume keys; only the power button acts\n");
  om::ui::ScreenPower p;
  CHECK(tm::volume::volumeKeyActs(p.off));   // unlocked: the keys act

  CHECK(p.onEvent(om::ui::ScreenEv::PowerPress) == om::ui::ScreenAct::Off);
  CHECK_EQ(p.off, 1);
  CHECK(!tm::volume::volumeKeyActs(p.off));   // locked: the keys do NOT act
  CHECK(p.onEvent(om::ui::ScreenEv::Key) == om::ui::ScreenAct::Swallow);
  CHECK_EQ(p.off, 1);                         // still dark
  CHECK(p.onEvent(om::ui::ScreenEv::PowerRelease) == om::ui::ScreenAct::None);

  CHECK(p.onEvent(om::ui::ScreenEv::PowerPress) == om::ui::ScreenAct::OnByPower);
  CHECK(!p.off);
  CHECK(tm::volume::volumeKeyActs(p.off));
}

// ---------------------------------------------------------------------------- the pop-up clock
void test_popup_clock() {
  printf("test  the pop-up stays 1.5 s after the last press and redraws on every press\n");
  tm::volume::VolumePopup pop;
  CHECK(!pop.showing(1000));
  pop.press(1000, 70);
  CHECK(pop.showing(1000));
  CHECK(pop.showing(2499));
  CHECK(!pop.showing(2500));                 // exactly 1500 ms
  CHECK(!pop.tick(2499));                    // still alive: no repaint
  CHECK(pop.tick(2500));                     // ran out once
  CHECK(!pop.tick(2500));                    // … and only once

  // A second press re-arms the whole 1.5 s and moves the level.
  pop.press(3000, 80);
  pop.press(4000, 90);
  CHECK_EQ(pop.level, 90);
  CHECK(pop.showing(5499));
  CHECK(!pop.showing(5500));

  // The 32-bit millisecond clock wraps: the signed difference still measures the gap.
  pop.press(0xFFFFFF00u, 50);
  CHECK(pop.showing(0xFFFFFF00u + 1499u));
  CHECK(!pop.showing(0xFFFFFF00u + 1500u));

  // ~6/s ceiling: the first press passes, then no repeat faster than 160 ms.
  tm::volume::StepGate g;
  CHECK(g.due(1000));
  CHECK(!g.due(1159));
  CHECK(g.due(1160));
  CHECK(!g.due(1200));
  CHECK(g.due(1320));
}

}  // namespace

int main() {
  test_step_and_clamp();
  test_popup_text();
  test_coalesced_save();
  test_clamp_press_still_moves_the_window();
  test_locked_ignores();
  test_popup_clock();
  if (g_failures == 0) {
    printf("volume-popup: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  printf("volume-popup: %d/%d checks FAILED\n", g_failures, g_checks);
  return 1;
}
