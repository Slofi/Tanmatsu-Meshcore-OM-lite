// SPDX-License-Identifier: MIT
// The start menu's rules — see menu_cards.h for the contract.
#include "menu_cards.h"

namespace tm {

int menuFocusMove(int focused, MenuMove move, int count) {
  if (count <= 0) return 0;
  if (focused < 0) focused = 0;
  if (focused > count - 1) focused = count - 1;

  switch (move) {
    case MenuMove::Left:
      return focused > 0 ? focused - 1 : 0;
    case MenuMove::Right:
      return focused < count - 1 ? focused + 1 : count - 1;
    case MenuMove::Down:
    case MenuMove::Up:
      // S516/EST-482: the Settings bar is gone, so there is no row below or above the cards. The focus
      // stays where it is (clamped at the ends), exactly like Up on the old card row.
      return focused;
  }
  return focused;
}

MenuAction menuEnterAction(bool available) {
  return available ? MenuAction::Open : MenuAction::ComingLater;
}

MenuAction menuStopAction(bool available, bool running) {
  return (available && running) ? MenuAction::Stop : MenuAction::None;
}

}  // namespace tm
