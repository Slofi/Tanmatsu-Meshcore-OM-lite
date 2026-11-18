// SPDX-License-Identifier: MIT
// Host tests for the start menu's rules (S513/EST-432). Real exit code: 0 = every check passed.
//
// The ticket's three rules are three groups, so a broken one has one obvious failing line:
//   1. Left/Right (Up/Down as aliases) move between the three cards and STOP at both ends — no wrapping;
//   2. Enter on an available card opens, on a LATER card only says "coming later";
//   3. s stops a running card and does nothing on a stopped or LATER one.
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
                  #got, #want, g_, w_);                                              \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

// ------------------------------------------------------------------------------------- 1. focus movement
void test_focus_stops_at_both_ends() {
  printf("test  Left/Right (Up/Down aliases) move between the cards, no wrap\n");

  // the row starts on OM Lite (0)
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Left), 0);        // clamped at the first card
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Up), 0);          // Up is the same direction as Left
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Right), 1);
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Right), 2);
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Down), 2);        // Down is the same direction as Right
  CHECK_EQ(tm::menuFocusMove(2, tm::MenuMove::Right), 2);       // clamped at the last card
  CHECK_EQ(tm::menuFocusMove(2, tm::MenuMove::Down), 2);

  CHECK_EQ(tm::menuFocusMove(2, tm::MenuMove::Left), 1);
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Left), 0);
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Up), 0);
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Left), 0);         // still no wrap back to 2

  // an out-of-range focus is clamped into the row first, then moved
  CHECK_EQ(tm::menuFocusMove(-3, tm::MenuMove::Right), 1);
  CHECK_EQ(tm::menuFocusMove(99, tm::MenuMove::Left), 1);

  // degenerate rows never index outside
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Right, 1), 0);
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Left, 1), 0);
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Right, 0), 0);
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Left, 0), 0);
}

// ------------------------------------------------------------------------------- 2./3. Enter and stop
void test_enter_and_stop_actions() {
  printf("test  Enter opens available, notices LATER; s stops only a running card\n");

  CHECK(tm::menuEnterAction(true) == tm::MenuAction::Open);              // OM Lite
  CHECK(tm::menuEnterAction(false) == tm::MenuAction::ComingLater);      // OPS-TOC / Mesh-Torry

  CHECK(tm::menuStopAction(true, true) == tm::MenuAction::Stop);         // a running module
  CHECK(tm::menuStopAction(true, false) == tm::MenuAction::None);        // already stopped
  CHECK(tm::menuStopAction(false, false) == tm::MenuAction::None);       // LATER: nothing to stop
  CHECK(tm::menuStopAction(false, true) == tm::MenuAction::None);        // LATER stays untouchable
}

}  // namespace

int main() {
  test_focus_stops_at_both_ends();
  test_enter_and_stop_actions();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
