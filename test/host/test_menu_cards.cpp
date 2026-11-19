// SPDX-License-Identifier: MIT
// Host tests for the start menu's rules (S513/EST-432, extended by EST-445).
// Real exit code: 0 = every check passed.
//
// The menu is three app cards plus a full-width Settings bar under them:
//   1. Left/Right move between the three cards and STOP at both ends — no wrapping;
//   2. Down from any card lands on the Settings bar, Up from the bar returns to the first card;
//   3. Enter on a card opens (available) or says "coming later" (LATER); Enter on the bar opens Settings;
//   4. s stops a running card and does nothing on a stopped or LATER card.
//
// Build/run: sh test/host/run.sh (g++ -std=c++17 -Wall -Wextra -Werror).
#include <stdio.h>

#include "menu_cards.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,      \
             #got, #want, g_, w_);                                                    \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

// ------------------------------------------------------------------------------------- 1. focus movement
void test_focus_cards_and_settings_bar() {
  printf("test  Left/Right over the cards, Down to the Settings bar, Up back\n");

  // The row starts on OM Lite (0); Left is clamped at the first card.
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Left), 0);
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Up), 0);          // Up on the card row stays on the card
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Right), 1);
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Right), 2);
  CHECK_EQ(tm::menuFocusMove(2, tm::MenuMove::Right), 2);       // clamped at the last card

  // EST-445: Down from ANY card lands on the full-width Settings bar (index 3).
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Down), tm::kMenuSettingsFocus);
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Down), tm::kMenuSettingsFocus);
  CHECK_EQ(tm::menuFocusMove(2, tm::MenuMove::Down), tm::kMenuSettingsFocus);
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Down), tm::kMenuSettingsFocus);   // clamped on the bar

  // Up from the bar returns to the first card; Left/Right on the bar do nothing.
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Up), 0);
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Left), 3);
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Right), 3);
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Up), 0);

  // a focus outside the menu is clamped into it first
  CHECK_EQ(tm::menuFocusMove(-3, tm::MenuMove::Right), 1);
  CHECK_EQ(tm::menuFocusMove(99, tm::MenuMove::Left), 3);   // 99 -> the bar; Left on the bar stays

  // degenerate rows never index outside
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Down, 0), 0);
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Down, 1), 1);
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Up, 1), 0);
}

// ------------------------------------------------------------------------------- 2./3./4. Enter and stop
void test_enter_and_stop_actions() {
  printf("test  Enter opens available / notices LATER / opens Settings; s stops only a running card\n");

  CHECK(tm::menuEnterAction(true) == tm::MenuAction::Open);              // OM Lite
  CHECK(tm::menuEnterAction(false) == tm::MenuAction::ComingLater);      // OPS-TOC / Mesh-Torry

  // The Settings bar is not a card and answers its own action, never indexing past the three cards.
  CHECK(tm::menuEnterActionAt(tm::kMenuSettingsFocus, false, tm::kMenuCards) == tm::MenuAction::OpenSettings);
  CHECK(tm::menuEnterActionAt(0, true, tm::kMenuCards) == tm::MenuAction::Open);
  CHECK(tm::menuEnterActionAt(1, false, tm::kMenuCards) == tm::MenuAction::ComingLater);
  CHECK(tm::menuEnterActionAt(9, true, tm::kMenuCards) == tm::MenuAction::None);

  CHECK(tm::menuStopAction(true, true) == tm::MenuAction::Stop);         // a running module
  CHECK(tm::menuStopAction(true, false) == tm::MenuAction::None);        // already stopped
  CHECK(tm::menuStopAction(false, false) == tm::MenuAction::None);       // LATER: nothing to stop
  CHECK(tm::menuStopAction(false, true) == tm::MenuAction::None);        // LATER stays untouchable
}

}  // namespace

int main() {
  test_focus_cards_and_settings_bar();
  test_enter_and_stop_actions();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
