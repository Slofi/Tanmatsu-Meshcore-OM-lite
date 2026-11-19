// SPDX-License-Identifier: MIT
// The start menu's rules — see menu_cards.h for the contract.
#include "menu_cards.h"

namespace tm {

int menuFocusMove(int focused, MenuMove move, int count) {
  if (count <= 0) return 0;
  const int settings = count;                 // the Settings bar is the row after the cards
  if (focused < 0) focused = 0;
  if (focused > settings) focused = settings;

  switch (move) {
    case MenuMove::Left:
      // On the Settings bar Left/Right do nothing: the bar has no horizontal neighbour, and the ticket
      // only says Down reaches it.
      if (focused == settings) return settings;
      return focused > 0 ? focused - 1 : 0;
    case MenuMove::Right:
      if (focused == settings) return settings;
      return focused < count - 1 ? focused + 1 : count - 1;
    case MenuMove::Down:
      // Down from any card lands on the Settings bar; Down on the bar stays there (clamp).
      return settings;
    case MenuMove::Up:
      // Up from the bar returns to the first card; Up on the card row stays on the card (clamp at the top).
      return focused == settings ? 0 : focused;
  }
  return focused;
}

MenuAction menuEnterAction(bool available) {
  return available ? MenuAction::Open : MenuAction::ComingLater;
}

MenuAction menuEnterActionAt(int focused, bool available, int count) {
  if (count <= 0) return MenuAction::None;
  if (focused == count) return MenuAction::OpenSettings;
  if (focused < 0 || focused > count) return MenuAction::None;
  return menuEnterAction(available);
}

MenuAction menuStopAction(bool available, bool running) {
  return (available && running) ? MenuAction::Stop : MenuAction::None;
}

}  // namespace tm
