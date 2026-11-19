// SPDX-License-Identifier: MIT
// The start menu's rules (S513/EST-432): pure, so the keys on the panel are host-tested.
//
// The menu is three app cards side by side (OM Lite, OPS-TOC, Mesh-Torry) with a full-width Settings bar
// under them (EST-445). This header holds the decisions the shell (main.cpp) makes from a key; the drawing
// lives in main/launcher_page.*.
//
// THIS REPLACES FILIP'S S499 TWO-AREA RULE, deliberately (EST-432/EST-445): there is still no second app
// area, so Enter on a card opens it directly and Esc leaves the app. The key geometry is now:
//   * Left/Right move between the three cards and STOP at both ends (no wrapping);
//   * Down from ANY card lands on the Settings bar under them, which is a fourth focus target;
//   * Up from the Settings bar returns to the first card; Up on the card row stays put;
//   * Enter on the Settings bar opens Settings on General.
// The Settings bar is not a module and never starts/stops anything.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF, no drawing.
#pragma once

#include <stdint.h>

namespace tm {

constexpr int kMenuCards = 3;   // OM Lite, OPS-TOC, Mesh-Torry (Filip's names and order)
constexpr int kMenuSettingsFocus = kMenuCards;   // the full-width Settings bar under the cards
constexpr int kMenuFocusCount = kMenuCards + 1;

enum class MenuMove : uint8_t { Left = 0, Right, Up, Down };

enum class MenuAction : uint8_t {
  None = 0,      // nothing happens (s on a stopped or LATER card)
  Open,          // Enter on an available card: start it if stopped, then route to its home page
  ComingLater,   // Enter on a LATER card: a short notice, no state change
  Stop,          // s on a running card: core.stop, reversible, no confirm
  OpenSettings,  // Enter on the Settings bar: open Settings on General
};

// Move the focus one target. Clamped at both ends: Left at the first card stays, Right at the last card
// stays, Down always lands on the Settings bar, Up from the bar returns to the first card. A focus outside
// the menu is clamped into it first. `count` is the CARD count; the Settings bar is index `count`.
int menuFocusMove(int focused, MenuMove move, int count = kMenuCards);

// Enter: an available card opens (stopped ones are started first); a LATER card only says so.
MenuAction menuEnterAction(bool available);

// Enter on the row that currently has focus, where `available` is the focused CARD's availability. The
// Settings bar answers OpenSettings; a card answers menuEnterAction(available). This is the one call the
// shell needs, so a menu focus of `kMenuSettingsFocus` can never index past the three-card array.
MenuAction menuEnterActionAt(int focused, bool available, int count = kMenuCards);

// s: only a running, available card can be stopped.
MenuAction menuStopAction(bool available, bool running);

}  // namespace tm
