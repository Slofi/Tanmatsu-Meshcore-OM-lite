// SPDX-License-Identifier: MIT
// The Tanmatsu app core, part 3: the registry (PLAN.md §2, rules 1-3).
//
// This is the table the Launcher draws and the only place a module starts or stops. The rules:
//   * states: Running / Stopped / Later (Later = available == false: listed, never startable);
//   * ONE OWNER PER RESOURCE: a start is refused when another running module holds a resource it asks
//     for, and the refusal names the resource;
//   * the CORE KEEPS THE RADIO: the LoRa driver and the MeshCore socket are the core's for the life of
//     the app; a module only ever holds a lease (Resource::Radio), one module at a time;
//   * the Launcher never draws beside a module: drawPage() draws the module that owns the current
//     page and nothing else, and the Launcher is the core's own page (Core::draw).
//
// Every start is paired with a stop that must return the module to zero counted bytes/blocks; when it
// does not, the stop reports the module by NAME (StopResult.leaked) instead of pretending success.
#pragma once

#include <stddef.h>

#include "alloc.h"
#include "module.h"

namespace tm {

class Core;

enum class ModuleState : uint8_t { Stopped = 0, Running, Later };

struct StartResult {
  enum Code : uint8_t { Ok = 0, AlreadyRunning, NotAvailable, NoSuchModule, ResourceBusy, StartFailed };
  Code code = Ok;
  Resource busy = Resource::None;   // set when code == ResourceBusy
  const char* leaked = nullptr;     // a failed start that left blocks behind: the module's name

  explicit operator bool() const { return code == Ok; }
};

struct StopResult {
  enum Code : uint8_t { Ok = 0, NotRunning, NoSuchModule };
  Code code = Ok;
  const char* leaked = nullptr;     // set when stop() left blocks behind: the leak report
  Allocator::Stats leak_stats;

  explicit operator bool() const { return code == Ok; }
};

class Registry {
 public:
  static constexpr size_t kMaxModules = 8;
  static constexpr size_t kNone = kMaxModules;   // "not found"

  // false = the table is full or the id is already there (both are programming errors, not runtime).
  bool add(const Module& m);

  size_t count() const { return n_; }
  const Module* at(size_t i) const { return i < n_ ? mods_[i] : nullptr; }
  const Module* find(const char* id) const;
  size_t indexOf(const char* id) const;

  ModuleState state(const char* id) const;
  bool isRunning(const char* id) const;
  size_t runningCount() const;

  // The module that owns a page, or nullptr (Page::Launcher belongs to the core).
  const Module* moduleForPage(Page page) const;

  StartResult start(Core& core, const char* id);
  StopResult stop(Core& core, const char* id);
  void stopAll(Core& core);   // app exit: stop in reverse start order, so dependencies fall last

  void tick(Core& core);      // every running module, in table order

  // Draws the one module that owns `page`; returns its id, or nullptr if nothing may draw there.
  // This is the whole "the Launcher never runs beside a module" rule: one page, one owner.
  const char* drawPage(Core& core, Page page, Canvas& canvas);

  Resource granted() const { return granted_; }

 private:
  void recomputeGrants();

  const Module* mods_[kMaxModules] = {};
  bool running_[kMaxModules] = {};
  size_t n_ = 0;
  Resource granted_ = Resource::None;
};

}  // namespace tm
