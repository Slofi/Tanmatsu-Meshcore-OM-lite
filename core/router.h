// SPDX-License-Identifier: MIT
// The Tanmatsu app core, part 4: the page router (PLAN.md §2 rule 3, §5).
//
// The six page keys (X/F1 and the five symbol keys) and Esc are a CORE-level route: the key arrives here
// before any module can see it, so a module cannot swallow the page switch. The router only DECIDES; it never
// starts anything — when the target page's module is stopped it answers OfferStart and the caller (the
// Launcher's own row or action) asks the user first.
//
// Esc is UP, not undo (Filip, S502: "folder up button, layer up, not just back"). Every module page has
// one parent, the Launcher, so Esc from anywhere goes there in ONE step — it does not retrace the pages
// that were visited on the way. At the Launcher there is no level above, so Esc changes nothing. A
// level INSIDE a module (an open chat channel above its channel list) belongs to the module and is
// handled there, before this router is asked.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF.
#pragma once

#include <stdint.h>

#include "keys.h"
#include "pages.h"

namespace tm {

class Registry;

enum class RouteAction : uint8_t {
  Noop = 0,     // nothing changes (already there, no such page, later module, or at the root)
  Switched,     // `page` is now current
  OfferStart,   // the page's module is STOPPED: offer Start, do not auto-start (PLAN §2 rule 3)
  Top,          // the page's OWN symbol key: close an inner level, or first row at the top (EST-444/454)
};

struct RouteResult {
  RouteAction action = RouteAction::Noop;
  Page page = Page::Launcher;    // the page that is current AFTER the call
  const char* module = nullptr;  // the module the key was aimed at (nullptr for the Launcher)
};

class Router {
 public:
  Page current() const { return current_; }

  // A page request (a symbol key, or the hint bar's gear). One decision, no side effects.
  RouteResult go(Page target, Registry& registry);

  // A symbol key: the ONE caller of page_for_symbol() in the core.
  // The page's OWN key (is_own_symbol) answers Top and does NOT call go(): the page is already there,
  // and its module decides whether an inner level closes (Module::onTop) or nothing changes.
  RouteResult symbol(Key key, Registry& registry);

  // Esc: up one level. The parent of every module page is the Launcher; the Launcher has no parent.
  RouteResult back(Registry& registry);

  // The app's landing page and the fallback when the module on screen stops.
  void home();

 private:
  Page current_ = Page::Launcher;
};

}  // namespace tm
