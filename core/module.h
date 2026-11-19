// SPDX-License-Identifier: MIT
// The Tanmatsu app core, part 1: the interface every module implements (PLAN.md §2).
//
// Deliberately tiny — it is the whole memory-discipline story:
//   * a module ALLOCATES in start() and returns EVERYTHING in stop();
//   * it never touches a driver: the core owns the radio and hands out a lease on a resource;
//   * draw() is called only while the module is running AND on screen (the Launcher never draws
//     beside a module), tick() only while running.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF. The host test builds this with g++.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "keys.h"
#include "pages.h"
#include "settings_model.h"   // S514/EST-448: a module supplies its settings as DATA

namespace tm {

class Core;

// A surface a module may draw on. The core only needs the TYPE: on the device this is filled in by
// badge-bsp / LovyanGFX, on the host the test uses a dummy. NO drawing code lives in this tree —
// the look is still waiting for Filip's approval on the six previews (BRIEF work order step 1).
struct Canvas {
  virtual ~Canvas() = default;
};

// The shared things a module can ask the core for. The core always owns the driver behind each one
// (PLAN §2 rule 1: only core/radio talks to the LoRa driver and MeshCore); a module gets a LEASE, and
// at most ONE module holds a lease on a resource at a time. A second asker is refused at start.
enum class Resource : uint32_t {
  None     = 0,
  Radio    = 1u << 0,   // the MeshCore session / the LoRa handle — never the driver
  SdCard   = 1u << 1,
  Settings = 1u << 2,   // prefs / NVS
  Clock    = 1u << 3,
};

constexpr Resource operator|(Resource a, Resource b) {
  return static_cast<Resource>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr Resource operator&(Resource a, Resource b) {
  return static_cast<Resource>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool asks(Resource set, Resource one) { return (set & one) != Resource::None; }

// For the "busy" report: the first single resource in a set, so the Launcher can say WHICH one.
constexpr Resource resource_first(Resource set) {
  if (asks(set, Resource::Radio)) return Resource::Radio;
  if (asks(set, Resource::SdCard)) return Resource::SdCard;
  if (asks(set, Resource::Settings)) return Resource::Settings;
  if (asks(set, Resource::Clock)) return Resource::Clock;
  return Resource::None;
}

constexpr const char* resource_name(Resource r) {
  switch (r) {
    case Resource::Radio:    return "radio";
    case Resource::SdCard:   return "sd";
    case Resource::Settings: return "settings";
    case Resource::Clock:    return "clock";
    default:                 return "none";
  }
}

// The module's own record in the Launcher list. `pages` says which pages draw() paints (OM Lite owns
// Nodes/Chat/Map/RPTR/Settings), so the router and the registry agree on which module a symbol key
// reaches — there is no second table.
struct Module {
  const char* id;       // "om-lite"
  const char* name;     // "OM Lite"
  const char* meta;     // one dim line under the name in the Launcher list
  uint16_t pages;       // page_bit(p) for every page this module draws; 0 = no page (a service)
  bool available;       // false = the "later" row: listed, not startable
  Resource owns;        // resources this module asks the core for (granted at start, or refused)

  bool (*start)(Core&);        // allocate: buffers (PSRAM), tasks, subscriptions. false = failed.
  void (*stop)(Core&);         // free EVERYTHING start() took — the allocator proves it
  void (*tick)(Core&);         // called only while running
  void (*draw)(Canvas&);       // called only while running AND on screen
  bool (*onKey)(Core&, Key);   // non-symbol keys only; true = the module used the key
  // S514/EST-444 (Filip): this module's symbol key OF THE PAGE ON SCREEN, pressed while that page is
  // current. Go to the page's top level — close an open chat, a search field or a sheet. `page` says
  // WHICH of the module's pages owns the level (OM Lite owns five, and a level on Chat must not be
  // closed by the Nodes key). S514/EST-454 (Filip): when the page is ALREADY at its top, this is where
  // the module puts the selection back on the FIRST row. true = an inner level was open and is now
  // closed (the focus did not move); false = already at the top, and the module reset its own focus.
  // nullptr = no page of this module has a level or a list today. A symbol key is NEVER passed to onKey().
  bool (*onTop)(Core&, Page page);
  // S514/EST-448: THIS module's settings, as data. The Settings frame has one top tab per registry row
  // and draws exactly what this returns; a stopped or LATER module still answers (its values are stored),
  // and its own action rows say when they need it running. nullptr = no settings data yet, which the
  // frame shows as the honest one-line state (kSettingsPlaceholder), never an empty page.
  SettingsHook settings;
};

}  // namespace tm
