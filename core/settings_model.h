// SPDX-License-Identifier: MIT
// Settings as DATA (S514/EST-448): the one shape a module returns, and the one shape the shell draws.
//
// EST-445 built a flat, shell-owned row array. That made the Settings page OM Lite's page and every app's
// settings a string in main.cpp. This header inverts it: each owner (the core for General, a module for its
// own tab) returns a list of SECTIONS, each with a header and rows. A row carries
//   * its label,
//   * a value getter that reads the REAL source on every draw (never a copy stored for display), and
//   * a kind: Info (read only), Toggle, Cycle, SubPage, Action,
//   * the handler the shell runs for Toggle/Cycle/Action.
// The Settings frame draws whatever it is given, so adding EST-446's channels or EST-447's contacts is a
// change in the OM Lite provider, not in the frame.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF, no drawing, no NVS. The host tests drive the same structs.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tm {

class Core;

// The five row kinds. A section header is NOT a row kind any more: it belongs to SettingsSection, so a
// header can never accidentally become focusable or be counted as a value.
enum class SettingKind : uint8_t {
  Info = 0,   // read only; Enter does nothing
  Toggle,     // one on/off value; Enter flips it through the handler
  Cycle,      // a small set of choices; Enter advances it
  SubPage,    // Enter opens the shell's sub-page (the name editor today)
  Action,     // Enter runs the handler (Advert now)
};

struct SettingRow;

// Called on EVERY draw. Returning nullptr means "no value": the drawing leaves the right column empty.
using SettingsValueFn = const char* (*)(Core&);

// Called by the shell after the frame reports ToggleValue / CycleValue / ActionRow.
using SettingsHandlerFn = void (*)(Core&, const SettingRow&);

// S516/EST-476: a row may be DIMMED from live state (the "Add heard nodes to the radio" row while saving is
// Off). Called on every draw like a value getter; nullptr/false means the normal look.
using SettingsFlagFn = bool (*)(Core&);

// A getter that also sees the ROW (S514/EST-446): one channel per row needs to know which slot to read,
// and the row's own `id` is the owner's slot number. Kept separate so every EST-448 getter stays as it was.
using SettingsValueRowFn = const char* (*)(Core&, const SettingRow&);

// A tiny wrapper so `nullptr` can never pick this constructor by accident (S514 self-sweep: the plain
// overload made every `{..., nullptr, handler}` row ambiguous).
struct RowValueFn {
  SettingsValueRowFn fn = nullptr;
  constexpr explicit RowValueFn(SettingsValueRowFn f) : fn(f) {}
};

struct SettingRow {
  SettingKind kind = SettingKind::Info;
  uint16_t id = 0;                 // the OWNER's row id; the frame never interprets it
  const char* label = "";
  const char* value = "";          // static text, used when read_value is nullptr
  SettingsValueFn read_value = nullptr;
  SettingsHandlerFn on_enter = nullptr;
  // A stored value is always readable; an ACTION may need the owning app running. When true and the
  // module is stopped, the frame refuses with NeedsRunning instead of calling the handler.
  bool needs_running = false;
  // Per-row getter (wins over read_value and value). At the END so every existing braced initialiser in
  // main.cpp keeps its meaning.
  SettingsValueRowFn read_row_value = nullptr;
  // S516/EST-476: an optional dim HINT drawn after the label (empty string = none) and the live dim state. Both
  // are at the very end, so every existing initialiser keeps its meaning.
  SettingsValueFn read_hint = nullptr;
  SettingsFlagFn  read_dim = nullptr;

  constexpr SettingRow() = default;
  constexpr SettingRow(SettingKind k, uint16_t i, const char* l, const char* v = "",
                       SettingsValueFn rv = nullptr, SettingsHandlerFn he = nullptr,
                       bool nr = false, SettingsValueFn rh = nullptr, SettingsFlagFn rd = nullptr)
      : kind(k), id(i), label(l), value(v), read_value(rv), on_enter(he), needs_running(nr),
        read_hint(rh), read_dim(rd) {}

  constexpr SettingRow(SettingKind k, uint16_t i, const char* l, const char* v, RowValueFn rv,
                       SettingsHandlerFn he = nullptr, bool nr = false, SettingsValueFn rh = nullptr,
                       SettingsFlagFn rd = nullptr)
      : kind(k), id(i), label(l), value(v), read_value(nullptr), on_enter(he), needs_running(nr),
        read_row_value(rv.fn), read_hint(rh), read_dim(rd) {}
};

// One section: an uppercase header and the rows under it. A section with zero rows still draws its header;
// an owner with zero sections shows the honest one-line state below instead.
struct SettingsSection {
  const char* header = "";
  const SettingRow* rows = nullptr;
  int row_count = 0;

  constexpr SettingsSection() = default;
  constexpr SettingsSection(const char* h, const SettingRow* r = nullptr, int n = 0)
      : header(h), rows(r), row_count(n) {}
};

// A module's settings, as data. `count` is an out-parameter. The returned array must outlive the draw
// (static storage in every owner) and may be rebuilt on the next call; nullptr is a valid answer.
using SettingsHook = const SettingsSection* (*)(Core&, int* count);

// The one dim line a module with no settings data shows (OPS-TOC and Mesh-Torry today: both LATER).
constexpr const char* kSettingsPlaceholder = "Settings come with the app";

// The value the panel must display for a row. The getter wins; the static value is the fallback. Kept here
// so the drawing code and the tests cannot disagree about which one is authoritative.
inline const char* settingsRowValue(Core& core, const SettingRow& row) {
  if (row.read_row_value != nullptr) {
    const char* v = row.read_row_value(core, row);
    return v != nullptr ? v : "";
  }
  if (row.read_value != nullptr) {
    const char* v = row.read_value(core);
    return v != nullptr ? v : "";
  }
  return row.value != nullptr ? row.value : "";
}

}  // namespace tm
