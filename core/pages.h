// SPDX-License-Identifier: MIT
// The Tanmatsu app core: the pages, in one enum.
//
// One app of ours, started from the stock badge.team launcher, with its own Launcher page inside
// (BRIEF §3). Every page belongs either to the core (the Launcher) or to exactly one module; the
// router (router.h) is what turns a key into a page, and the registry says whether that page's
// module is running.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF, no drawing.
#pragma once

#include <stdint.h>

namespace tm {

enum class Page : uint8_t {
  Launcher = 0,   // the core's own page (the CD-style module list)
  Nodes,          // OM Lite
  Chat,           // OM Lite
  Map,            // OM Lite
  Rptr,           // OM Lite
  Settings,       // OM Lite (tabbed; the RPTR pages live on Rptr)
  Count,          // not a page: "none"
};

constexpr bool is_page(Page p) { return p != Page::Count; }

// A module declares the pages it draws as a bitmask (module.h: Module::pages). The router and the
// registry both use this one helper, so a page can never belong to two different answers.
constexpr uint16_t page_bit(Page p) {
  return static_cast<uint16_t>(1u << static_cast<uint8_t>(p));
}

constexpr bool covers(uint16_t pages, Page p) { return (pages & page_bit(p)) != 0; }

constexpr const char* page_name(Page p) {
  switch (p) {
    case Page::Launcher: return "launcher";
    case Page::Nodes:    return "nodes";
    case Page::Chat:     return "chat";
    case Page::Map:      return "map";
    case Page::Rptr:     return "rptr";
    case Page::Settings: return "settings";
    default:             return "none";
  }
}

}  // namespace tm
