// SPDX-License-Identifier: MIT
// The Tanmatsu app core: the ONE key table (PLAN.md §5, BRIEF §5).
//
// The five physical symbol keys (triangle, square, circle, "cloud", diamond) are the global page
// switch. Nothing else in the app reads a symbol key: `page_for_symbol` below is the only switch, so
// changing the map is one edit here and the router test pins it.
//
// WHAT IS *NOT* IN THIS FILE: hardware codes.
// The BSP's real codes for the arrows, Enter/Esc/Backspace and the five symbol keys are not known yet.
// Work-order step 2 logs every key on the glass (scancode / ascii / pressed, like the CardKB) and THAT
// log is the source of truth (PLAN §5, open question 10). Until then every code is `kUnlogged`, and a
// test asserts that no code in this table is a guessed constant. Filling it in must touch this file
// only.
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
  SymTriangle, SymSquare, SymCircle, SymCloud, SymDiamond,
  Char,
};

// The one "code" a key may have before step 2's logger has seen it.
constexpr uint16_t kUnlogged = 0xFFFF;

struct KeyEntry {
  Key key;
  const char* name;   // what the logger and the tests print
  uint16_t code;      // kUnlogged until the on-glass logger fills it in
};

// The ONE table. Names are frozen by the host test; codes are the logger's business.
//
// CODES FILLED FROM THE GLASS (DeepSeek, S499, 2026-11-13, Tanmatsu on USB + our step-2 shell running):
// the app's  log, one press per key, read over the P4 console. They are
// bsp_input_navigation_key_t values, NOT guesses. Esc does not appear as a navigation key on this board —
// it arrives as a KEYBOARD event with ASCII 0x1B, which is how main.cpp maps it.
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
    {Key::SymTriangle, "triangle", 21},   // [key] nav key=21
    {Key::SymSquare, "square", 22},       // [key] nav key=22
    {Key::SymCircle, "circle", 23},       // [key] nav key=23
    {Key::SymCloud, "cloud", 24},         // [key] nav key=24
    {Key::SymDiamond, "diamond", 25},     // [key] nav key=25
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

constexpr bool is_symbol(Key k) {
  return k == Key::SymTriangle || k == Key::SymSquare || k == Key::SymCircle ||
         k == Key::SymCloud || k == Key::SymDiamond;
}

// The keys the CORE consumes before a module ever sees them: the page switch and Esc (back).
constexpr bool is_core_key(Key k) { return is_symbol(k) || k == Key::Esc; }

// The five-key route, one switch (PLAN §5 / BRIEF §5). A non-symbol key has no page: Page::Count.
constexpr Page page_for_symbol(Key k) {
  switch (k) {
    case Key::SymTriangle: return Page::Launcher;
    case Key::SymSquare:   return Page::Nodes;
    case Key::SymCircle:   return Page::Chat;
    case Key::SymCloud:    return Page::Map;
    case Key::SymDiamond:  return Page::Rptr;
    default:               return Page::Count;
  }
}

}  // namespace tm
