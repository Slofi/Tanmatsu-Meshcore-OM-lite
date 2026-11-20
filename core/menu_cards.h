// SPDX-License-Identifier: MIT
// The start menu's rules (S513/EST-432): pure, so the keys on the panel are host-tested.
//
// The menu is three app cards side by side (OM Lite, OPS-TOC, Mesh-Torry). S516/EST-482 removed the full-width
// Settings bar that EST-445 had put under them (Filip: "no need for it, that's what an F-key is for"): Settings
// stays on the diamond symbol key, so the menu has exactly three focus targets again.
//
// THIS REPLACES FILIP'S S499 TWO-AREA RULE, deliberately (EST-432): there is still no second app area, so Enter
// on a card opens it directly and Esc leaves the app. The key geometry is now:
//   * Left/Right move between the three cards and STOP at both ends (no wrapping);
//   * Up/Down stay on the card row (there is no row below any more);
//   * Enter opens the focused card.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF, no drawing.
#pragma once

#include <stdint.h>

namespace tm {

constexpr int kMenuCards = 3;   // OM Lite, OPS-TOC, Mesh-Torry (Filip's names and order)
constexpr int kMenuFocusCount = kMenuCards;

enum class MenuMove : uint8_t { Left = 0, Right, Up, Down };

enum class MenuAction : uint8_t {
  None = 0,      // nothing happens (s on a stopped or LATER card)
  Open,          // Enter on an available card: start it if stopped, then route to its home page
  ComingLater,   // Enter on a LATER card: a short notice, no state change
  Stop,          // s on a running card: core.stop, reversible, no confirm
};

// Move the focus one card. Clamped at both ends: Left at the first card stays, Right at the last card stays,
// Up/Down stay where they are. A focus outside the menu is clamped into it first.
int menuFocusMove(int focused, MenuMove move, int count = kMenuCards);

// Enter: an available card opens (stopped ones are started first); a LATER card only says so.
MenuAction menuEnterAction(bool available);

// s: only a running, available card can be stopped.
MenuAction menuStopAction(bool available, bool running);

}  // namespace tm
