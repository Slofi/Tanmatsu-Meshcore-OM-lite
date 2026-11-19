// SPDX-License-Identifier: MIT
// The page router — see router.h for the rules.
#include "router.h"

#include "keys.h"
#include "registry.h"

namespace tm {

RouteResult Router::go(Page target, Registry& registry) {
  RouteResult r;
  r.page = current_;

  if (!is_page(target)) return r;   // Page::Count = "no page"

  if (target == Page::Launcher) {
    if (current_ == Page::Launcher) return r;   // already home
    current_ = Page::Launcher;
    r.action = RouteAction::Switched;
    r.page = current_;
    r.module = nullptr;
    return r;
  }

  const Module* m = registry.moduleForPage(target);
  if (!m) return r;                            // no module owns that page
  r.module = m->id;

  if (!m->available) return r;                 // the "LATER" row: listed, not a destination
  if (!registry.isRunning(m->id)) {
    r.action = RouteAction::OfferStart;        // stopped: offer Start, never auto-start
    return r;                                  // the page does NOT change
  }
  if (current_ == target) return r;            // already there

  current_ = target;
  r.action = RouteAction::Switched;
  r.page = current_;
  return r;
}

RouteResult Router::symbol(Key key, Registry& registry) {
  const Page target = page_for_symbol(key);
  RouteResult r;
  r.page = current_;

  // S514/EST-444 (Filip): the symbol key of the page you are ALREADY on goes to that page's top
  // level. The router only DECIDES that it is the page's own key; the module that owns the inner
  // level closes it (Core calls Module::onTop). No page change; at the top the module resets its own
  // selection to the first row (S514/EST-454), so the router still names no page and no row.
  if (target != Page::Count && is_own_symbol(key, current_)) {
    const Module* m = registry.moduleForPage(current_);
    if (m != nullptr && m->available && registry.isRunning(m->id)) {
      r.action = RouteAction::Top;
      r.module = m->id;
    }
    return r;   // the Launcher (no module) and a stopped owner change nothing
  }

  return go(target, registry);
}

RouteResult Router::back(Registry& registry) {
  (void)registry;   // the parent is the Launcher whatever state any module is in: no lookup can change it
  RouteResult r;
  r.page = current_;
  if (current_ == Page::Launcher) return r;   // the root: no level above it

  current_ = Page::Launcher;
  r.action = RouteAction::Switched;
  r.page   = Page::Launcher;
  r.module = nullptr;
  return r;
}

void Router::home() { current_ = Page::Launcher; }

}  // namespace tm
