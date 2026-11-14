// SPDX-License-Identifier: MIT
// The registry — see registry.h for the rules it enforces.
#include "registry.h"

#include <string.h>

#include "core.h"

namespace tm {

bool Registry::add(const Module& m) {
  if (n_ >= kMaxModules || !m.id) return false;
  if (indexOf(m.id) != kNone) return false;
  mods_[n_] = &m;
  running_[n_] = false;
  ++n_;
  return true;
}

const Module* Registry::find(const char* id) const {
  const size_t i = indexOf(id);
  return i == kNone ? nullptr : mods_[i];
}

size_t Registry::indexOf(const char* id) const {
  if (!id) return kNone;
  for (size_t i = 0; i < n_; ++i)
    if (::strcmp(mods_[i]->id, id) == 0) return i;
  return kNone;
}

ModuleState Registry::state(const char* id) const {
  const size_t i = indexOf(id);
  if (i == kNone) return ModuleState::Stopped;          // unknown = not running
  if (!mods_[i]->available) return ModuleState::Later;  // later wins over any leftover bit
  return running_[i] ? ModuleState::Running : ModuleState::Stopped;
}

bool Registry::isRunning(const char* id) const { return state(id) == ModuleState::Running; }

size_t Registry::runningCount() const {
  size_t n = 0;
  for (size_t i = 0; i < n_; ++i)
    if (running_[i]) ++n;
  return n;
}

const Module* Registry::moduleForPage(Page page) const {
  if (page == Page::Launcher || page == Page::Count) return nullptr;
  for (size_t i = 0; i < n_; ++i)
    if (covers(mods_[i]->pages, page)) return mods_[i];
  return nullptr;
}

StartResult Registry::start(Core& core, const char* id) {
  StartResult r;
  const size_t i = indexOf(id);
  if (i == kNone) {
    r.code = StartResult::NoSuchModule;
    return r;
  }
  const Module* m = mods_[i];
  if (!m->available) {
    r.code = StartResult::NotAvailable;
    return r;
  }
  if (running_[i]) {
    r.code = StartResult::AlreadyRunning;
    return r;
  }

  // One owner per resource. The core owns the driver behind every resource; this is the lease check.
  const Resource clash = m->owns & granted_;
  if (clash != Resource::None) {
    r.code = StartResult::ResourceBusy;
    r.busy = resource_first(clash);
    return r;
  }

  running_[i] = true;
  recomputeGrants();

  if (m->start && !m->start(core)) {
    running_[i] = false;
    recomputeGrants();
    r.code = StartResult::StartFailed;
    const Allocator::Stats s = core.alloc().stats(m->id);
    if (s.blocks != 0) r.leaked = m->id;   // a failed start that leaks is still a leak, by name
  }
  return r;
}

StopResult Registry::stop(Core& core, const char* id) {
  StopResult r;
  const size_t i = indexOf(id);
  if (i == kNone) {
    r.code = StopResult::NoSuchModule;
    return r;
  }
  if (!running_[i]) {
    r.code = StopResult::NotRunning;
    return r;
  }

  const Module* m = mods_[i];
  if (m->stop) m->stop(core);
  running_[i] = false;
  recomputeGrants();

  // "Stop frees memory" is this check, not a promise in a comment.
  const Allocator::Stats s = core.alloc().stats(m->id);
  if (s.blocks != 0) {
    r.leaked = m->id;
    r.leak_stats = s;
  }
  return r;
}

void Registry::stopAll(Core& core) {
  for (size_t k = n_; k > 0; --k) {
    const size_t i = k - 1;
    if (running_[i]) stop(core, mods_[i]->id);
  }
}

void Registry::tick(Core& core) {
  for (size_t i = 0; i < n_; ++i)
    if (running_[i] && mods_[i]->tick) mods_[i]->tick(core);
}

const char* Registry::drawPage(Core& core, Page page, Canvas& canvas) {
  (void)core;   // the draw hook is void (*)(Canvas&) (PLAN §2); the module reaches the core via start()
  const Module* m = moduleForPage(page);
  if (!m || !isRunning(m->id) || !m->draw) return nullptr;
  m->draw(canvas);
  return m->id;
}

void Registry::recomputeGrants() {
  granted_ = Resource::None;
  for (size_t i = 0; i < n_; ++i)
    if (running_[i]) granted_ = granted_ | mods_[i]->owns;
}

}  // namespace tm
