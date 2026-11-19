// SPDX-License-Identifier: MIT
// The core facade — see core.h for the rules it wires together.
#include "core.h"

#include <stdio.h>
#include <string.h>

namespace tm {

StopResult Core::stop(const char* id) {
  StopResult r = registry_.stop(*this, id);
  // If the module that was on screen just stopped, the screen falls back to the Launcher. The page
  // never points at a stopped module (the router would then have nothing to draw).
  const Module* m = registry_.find(id);
  if (m && covers(m->pages, router_.current())) router_.home();
  return r;
}

bool Core::handleKey(Key k) {
  if (is_symbol(k)) {
    // Core-level route: a module never sees a symbol key through onKey(). If it is the OWN key of the
    // page on screen (S514/EST-444), the page's module closes its inner level; at the top it instead
    // resets its selection to the first row (S514/EST-454). The key is consumed either way.
    const RouteResult r = router_.symbol(k, registry_);
    if (r.action == RouteAction::Top) {
      const Module* m = registry_.moduleForPage(router_.current());
      if (m && registry_.isRunning(m->id) && m->onTop) m->onTop(*this, router_.current());
    }
    return true;
  }
  if (k == Key::Esc) {
    router_.back(registry_);
    return true;
  }

  const Module* m = registry_.moduleForPage(router_.current());
  if (m && registry_.isRunning(m->id) && m->onKey) return m->onKey(*this, k);
  return false;   // the Launcher page's keys are the shell's business
}

void Core::tick() { registry_.tick(*this); }

const char* Core::draw(Canvas& canvas) {
  if (router_.current() == Page::Launcher) return "launcher";   // the shell draws the Launcher page
  const char* id = registry_.drawPage(*this, router_.current(), canvas);
  return id ? id : "launcher";   // defensive: never leave the screen to a stopped module
}

const char* Core::firstLeak() const {
  for (size_t i = 0; i < registry_.count(); ++i) {
    const Module* m = registry_.at(i);
    if (m && alloc_.stats(m->id).blocks != 0) return m->id;
  }
  return nullptr;
}

const char* Core::leakLine(char* out, size_t n) const {
  if (!out || n == 0) return out;
  out[0] = '\0';
  const char* id = firstLeak();
  if (!id) return out;
  const Allocator::Stats s = alloc_.stats(id);
  ::snprintf(out, n, "leak: %s still holds %u blocks / %u bytes after stop", id,
             static_cast<unsigned>(s.blocks), static_cast<unsigned>(s.bytes));
  return out;
}

}  // namespace tm
