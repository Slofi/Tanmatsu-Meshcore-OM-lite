// SPDX-License-Identifier: MIT
// The start menu's rules (S513/EST-432): pure, so the keys on the panel are host-tested.
//
// The menu is three app cards side by side (OM Lite, OPS-TOC, Mesh-Torry). This header holds the decisions
// the shell (main.cpp) makes from a key; the drawing lives in main/launcher_page.*.
//
// THIS REPLACES FILIP'S S499 TWO-AREA RULE, deliberately (EST-432): with three app cards there is no second
// area to move to, and picking is the main action, so Enter opens the focused app directly and Esc leaves the
// app. Left/Right (Up/Down are aliases) move between the cards and STOP at both ends — no wrapping.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF, no drawing.
#pragma once

#include <stdint.h>

namespace tm {

constexpr int kMenuCards = 3;   // OM Lite, OPS-TOC, Mesh-Torry (Filip's names and order)

enum class MenuMove : uint8_t { Left = 0, Right, Up, Down };

enum class MenuAction : uint8_t {
  None = 0,     // nothing happens (s on a stopped or LATER card)
  Open,         // Enter on an available card: start it if stopped, then route to its home page
  ComingLater,  // Enter on a LATER card: a short notice, no state change
  Stop,         // s on a running card: core.stop, reversible, no confirm
};

// Move the focus one card. Clamped at both ends: Left/Up at the first card stays, Right/Down at the last
// card stays. A `count` of 0 or a focused index outside the row is clamped into it first.
int menuFocusMove(int focused, MenuMove move, int count = kMenuCards);

// Enter: an available card opens (stopped ones are started first); a LATER card only says so.
MenuAction menuEnterAction(bool available);

// s: only a running, available card can be stopped.
MenuAction menuStopAction(bool available, bool running);

}  // namespace tm
