// SPDX-License-Identifier: MIT
// S514/EST-454 (Filip): the page's own symbol key, pressed while the page is ALREADY at its top level,
// puts the selection back on the FIRST row. Inside an inner level the same key only closes that level
// (one press, one step) and must not also jump.
//
// EST-444 split the decision (the router answers RouteAction::Top) from the effect (the page owner's
// Module::onTop returns true when it closed a level, false when it was already at the top). THIS file is
// the second half: the pure arithmetic of the "already at the top" answer, so the host test drives the
// same rule the panel runs. The router stays generic; no page and no key is named here.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF.
#pragma once

#include "chat_cards.h"   // om::mc::kCardCols: the Chat list's two column scrolls

namespace tm {

// One list on one page (Nodes, RPTR). `inner` is the level above the list (the Nodes search bar, an
// RPTR login/status page). `row` is the list's focus.
struct ListTop {
  bool inner = false;
  int  row   = 0;
};

// Returns true when an inner level was open and has now been closed: the focus is left exactly where it
// was, so the key did one step. Returns false when the page was already at its top, and then the
// selection moves to the first row. Scrolling follows the row: both pages draw from it.
inline bool ownTopList(ListTop& s) {
  if (s.inner) {
    s.inner = false;
    return true;
  }
  s.row = 0;
  return false;
}

// The Chat list: two columns of cards, each with its own scroll. At the top the key returns to the first
// channel card (column 0, row 0) and both column scrolls go to 0. An open channel/DM is the inner level:
// closing it leaves the focus AND the column scrolls alone.
struct CardListTop {
  bool inner = false;
  int  col   = om::mc::kColChannels;
  int  row   = 0;
  int  scroll[om::mc::kCardCols] = {0, 0};
};

inline bool ownTopCardList(CardListTop& s) {
  if (s.inner) {
    s.inner = false;
    return true;
  }
  s.col = om::mc::kColChannels;
  s.row = 0;
  for (int i = 0; i < om::mc::kCardCols; ++i) s.scroll[i] = 0;
  return false;
}

}  // namespace tm
