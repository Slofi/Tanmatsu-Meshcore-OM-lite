// SPDX-License-Identifier: MIT
// The Tanmatsu app core, part 5: the ONE owner (PLAN.md §1, §2).
//
// `Core` owns the counted allocator, the module registry and the page router, and is the only object
// the shell (`main.cpp`, the Launcher page) talks to. The wiring is the rules:
//   * the CORE owns the radio driver for the life of the app (ownsRadioDriver()); a module can only
//     hold a radio LEASE, and only one module at a time (Registry::start refuses the second);
//   * a symbol key or Esc is consumed here, BEFORE any module sees it, so no module can swallow the
//     page switch;
//   * draw() gives the screen to exactly one owner: "launcher", or the module on the current page;
//   * stopping the module that is on screen drops back to the Launcher page.
//
// No hardware, no drawing, no Arduino/IDF: this file builds on the host with g++ and on the device
// unchanged.
#pragma once

#include <stddef.h>

#include "alloc.h"
#include "module.h"
#include "registry.h"
#include "router.h"

namespace tm {

class Core {
 public:
  Allocator& alloc() { return alloc_; }
  Registry& registry() { return registry_; }
  Router& router() { return router_; }

  // PLAN §2 rule 1: the drivers are the core's, always. A module holds a handle, never a driver.
  bool ownsRadioDriver() const { return true; }

  StartResult start(const char* id) { return registry_.start(*this, id); }
  StopResult stop(const char* id);

  // true = the core consumed the key (a symbol key or Esc). A module sees only the rest, and only
  // while its page is on screen.
  bool handleKey(Key k);

  void tick();                        // every running module
  const char* draw(Canvas& canvas);   // "launcher" or the module id that drew; nullptr = nothing

  Page page() const { return router_.current(); }
  RouteResult goPage(Page p) { return router_.go(p, registry_); }

  // The one-line leak report the Launcher/boot log prints, e.g.
  //   "leak: om-lite still holds 2 blocks / 1536 bytes after stop"
  // Empty string when every module is back to zero.
  const char* leakLine(char* out, size_t n) const;
  const char* firstLeak() const;

 private:
  Allocator alloc_;
  Registry registry_;
  Router router_;
};

}  // namespace tm
