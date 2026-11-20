// SPDX-License-Identifier: MIT
// The Tanmatsu app core: the ONE key table (PLAN.md §5, BRIEF §5).
//
// The SIX page keys are the X (F1) key and the five physical symbol keys (triangle, square, circle,
// "cloud", diamond). They are the global page switch. Nothing else in the app reads a page key:
// `page_for_symbol` below is the only switch, so changing the map is one edit here and the router test
// pins it.
//
// S516/EST-478 (Filip, 2026-11-20): the function keys moved one up — F1/X is the Launcher, triangle
// Nodes, square Chat, circle Map, cloud RPTR and diamond Settings. The comma is NOT a Settings key any
// more: outside a text field it does nothing, inside one it is the character it is.
//
// The two volume keys are NOT page keys: they act on every page (and on the Launcher) through the
// shell's global volume handler (core/volume_popup.h owns their rules).
//
// WHAT IS *NOT* IN THIS FILE: hardware codes that have not been read off the glass.
// The code for each key is the BSP enum value, read from `bsp/input.h` and (where noted) confirmed by
// the on-device logger (`[key] nav key=N`). An unlogged code is `kUnlogged`, and a test asserts that no
// code in this table is a guessed constant. Filling one in must touch this file only.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pages.h"

namespace tm {

enum class Key : uint16_t {
  None = 0,
  Up, Down, Left, Right, Enter, Esc, Backspace,
  SymX, SymTriangle, SymSquare, SymCircle, SymCloud, SymDiamond,
  VolUp, VolDown,
  Char,
};

// The one "code" a key may have before the logger has seen it.
constexpr uint16_t kUnlogged = 0xFFFF;

struct KeyEntry {
  Key key;
  const char* name;   // what the logger and the tests print
  uint16_t code;      // kUnlogged until the on-glass logger fills it in
};

// The ONE table. Names are frozen by the host test; codes are the BSP enum / the logger's business.
//
// CODES FILLED FROM THE GLASS (DeepSeek, S499, 2026-11-13, Tanmatsu on USB + our step-2 shell running):
// the app's log, one press per key, read over the P4 console. They are bsp_input_navigation_key_t values,
// NOT guesses. Esc does not appear as a navigation key on this board — it arrives as a KEYBOARD event
// with ASCII 0x1B, which is how main.cpp maps it.
// S516/EST-478: the enum in managed_components/badgeteam__badge-bsp/bsp/input.h:154-200 counted in order
// (NONE 0, ESC 1 … SPACE_R 19, F1 20 … F12 31, GAMEPAD_A 32 … JOYSTICK_PRESS 36, VOLUME_UP 37,
// VOLUME_DOWN 38). The X key's F1 = 20 was seen once in tonight's logs (nav key=20) but has not yet been
// pressed on the glass by Filip while looking at a label, so its PLACE on the physical X key is still
// unconfirmed; VOLUME_UP 37 was seen 41 times tonight, VOLUME_DOWN 38 never yet. Those are honest
// observation gaps, not guesses: each number is the BSP enum's own value.
// Still unlogged (press them next time the shell is on): Left, Right, Backspace.
constexpr KeyEntry kKeys[] = {
    {Key::None, "none", 0},
    {Key::Up, "up", 4},            // [key] nav key=4
    {Key::Down, "down", 5},        // [key] nav key=5
    {Key::Left, "left", kUnlogged},
    {Key::Right, "right", kUnlogged},
    {Key::Enter, "enter", 13},      // [key] nav key=13
    {Key::Esc, "esc", kUnlogged},
    {Key::Backspace, "backspace", kUnlogged},
    {Key::SymX, "x", 20},                 // F1; [key] nav key=20 (seen once, not yet pinned to the key)
    {Key::SymTriangle, "triangle", 21},   // [key] nav key=21
    {Key::SymSquare, "square", 22},       // [key] nav key=22
    {Key::SymCircle, "circle", 23},       // [key] nav key=23
    {Key::SymCloud, "cloud", 24},         // [key] nav key=24
    {Key::SymDiamond, "diamond", 25},     // [key] nav key=25
    {Key::VolUp, "volumeup", 37},         // BSP enum: VOLUME_UP; [key] nav key=37 (41 tonight)
    {Key::VolDown, "volumedown", 38},     // BSP enum: VOLUME_DOWN; never seen on the glass yet
    {Key::Char, "char", kUnlogged},
};
constexpr size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

constexpr const char* key_name(Key k) {
  for (size_t i = 0; i < kKeyCount; ++i)
    if (kKeys[i].key == k) return kKeys[i].name;
  return "?";
}

constexpr uint16_t key_code(Key k) {
  for (size_t i = 0; i < kKeyCount; ++i)
    if (kKeys[i].key == k) return kKeys[i].code;
  return kUnlogged;
}

// The logger's lookup. An unlogged code is Key::None, never a guess: a placeholder must not be able to
// act as a real key.
constexpr Key key_from_code(uint16_t code) {
  if (code == kUnlogged) return Key::None;
  for (size_t i = 0; i < kKeyCount; ++i)
    if (kKeys[i].key != Key::None && kKeys[i].code == code) return kKeys[i].key;
  return Key::None;
}

// The six page keys: the X (F1) key and the five symbol keys. `is_symbol` keeps its old name because the
// router and every comment already use it; X is a page key for routing purposes even though it is not a
// printed symbol.
constexpr bool is_symbol(Key k) {
  return k == Key::SymX || k == Key::SymTriangle || k == Key::SymSquare || k == Key::SymCircle ||
         k == Key::SymCloud || k == Key::SymDiamond;
}

// The two volume keys: global, never routed to a page.
constexpr bool is_volume_key(Key k) { return k == Key::VolUp || k == Key::VolDown; }

// The ONE Settings key (S516/EST-478): the diamond. A typed comma is never it.
constexpr bool is_settings_key(Key k) { return k == Key::SymDiamond; }

// The keys the CORE consumes before a module ever sees them: the page switch, the volume keys and Esc
// (back).
constexpr bool is_core_key(Key k) { return is_symbol(k) || is_volume_key(k) || k == Key::Esc; }

// The six-key route, one switch (PLAN §5 / BRIEF §5). A non-page key has no page: Page::Count.
constexpr Page page_for_symbol(Key k) {
  switch (k) {
    case Key::SymX:        return Page::Launcher;
    case Key::SymTriangle: return Page::Nodes;
    case Key::SymSquare:   return Page::Chat;
    case Key::SymCircle:   return Page::Map;
    case Key::SymCloud:    return Page::Rptr;
    case Key::SymDiamond:  return Page::Settings;
    default:               return Page::Count;
  }
}

// S514/EST-444 (Filip): the page key OF THE PAGE ALREADY ON SCREEN returns to that page's top level.
// One predicate, so the router and a test cannot disagree about what "own key" means. S516/EST-478: this
// includes X on the Launcher and the diamond on Settings.
constexpr bool is_own_symbol(Key k, Page page) {
  return is_symbol(k) && page_for_symbol(k) == page;
}

}  // namespace tm
