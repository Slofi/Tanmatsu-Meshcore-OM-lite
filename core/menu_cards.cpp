// SPDX-License-Identifier: MIT
// The start menu's rules — see menu_cards.h for the contract.
#include "menu_cards.h"

namespace tm {

int menuFocusMove(int focused, MenuMove move, int count) {
  if (count <= 0) return 0;
  if (focused < 0) focused = 0;
  if (focused >= count) focused = count - 1;

  // Up is the previous card and Down the next, i.e. the same two directions as Left/Right (the ticket's
  // "Up/Down as aliases"): the clue row is horizontal, so the vertical keys are a convenience, not a second axis.
  const bool next = (move == MenuMove::Right || move == MenuMove::Down);
  int        to   = focused + (next ? 1 : -1);
  if (to < 0) to = 0;                 // clamp, never wrap
  if (to >= count) to = count - 1;    // clamp, never wrap
  return to;
}

MenuAction menuEnterAction(bool available) {
  return available ? MenuAction::Open : MenuAction::ComingLater;
}

MenuAction menuStopAction(bool available, bool running) {
  return (available && running) ? MenuAction::Stop : MenuAction::None;
}

}  // namespace tm
