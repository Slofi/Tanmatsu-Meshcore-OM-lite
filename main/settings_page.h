// SPDX-License-Identifier: MIT
// Settings (EST-445, per-app tabs in S514/EST-448): the drawing. The DECISIONS are in core/settings.h
// (host-tested); this file only paints the frame it describes: status bar, page head, the owner tabs, the
// sections the current tab returned, the gold focus ring, the scroll thumb, the one-line no-settings state,
// the Name sub-page and the ONE shared confirm sheet.
//
// A view carries only what the panel needs. Each tab's sections come from its owner (the core for General,
// Module::settings for an app); a row's value comes from its getter, called here on every draw, so nothing
// on the panel is a stale copy. Nothing here reads NVS, the radio or the clock by itself.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "font.h"
#include "screen.h"
#include "settings.h"
#include "tm_theme.h"

namespace om {
namespace ui {

// Frame metrics, mirrored by tools/preview_tanmatsu.py.
constexpr int kSetTabH   = 30;   // the L2's tab row, at 800 px
constexpr int kSetBodyTop = kTmStatusH + kTmHeadH + kSetTabH;   // 26 + 44 + 30 = 100
constexpr int kSetBodyH   = kTmH - kTmHintH - kSetBodyTop;      // 480 - 24 - 100 = 356

// S514/EST-451: a LIST sub-page has no tab row. Its body starts one pixel below the page head's line and
// runs to the hint bar; BOTH the renderer and the key arithmetic use these, so the scroll's window and the
// drawn window can never differ by a pixel again (the picker used to pass the TAB body + tab row = 386).
constexpr int kSetSubBodyTop = tm::settingsListBodyTop(kTmStatusH, kTmHeadH);           // 71
constexpr int kSetSubBodyH   = tm::settingsListBodyHeight(kTmH, kTmHintH, kTmStatusH, kTmHeadH);  // 385

struct SettingsView {
  // Chrome (the caller fills these from the app's one sources).
  char clock[8] = "--:--";
  int  battery_pct = -1;
  bool charging = false;

  // The owner tabs, exactly as core/settings.h derived them from the registry.
  const tm::SettingsTabDef* tabs = nullptr;
  int tab_count = 0;
  int tab = tm::kSettingsGeneralTab;

  // The current tab's sections. `sections == nullptr` or `section_count == 0` is the honest one-line
  // state: "Settings come with the app" (a module with no settings data yet).
  const tm::SettingsSection* sections = nullptr;
  int section_count = 0;

  // The getter needs the core, and only the core: the panel never reaches a store by itself.
  tm::Core* core = nullptr;
  bool tab_running = false;

  // S514/EST-451: the Volume row is focused, so the hint bar carries the '+' / '-' keys. The shell sets
  // this from the focused row's id; the panel only chooses the hints.
  bool volume_row = false;

  int focus = -1;    // flat element index (section header or row), as core/settings.h numbers them
  int scroll = 0;    // px

  // The Name sub-page: no tab row; the page head names it and the body is the shared text field.
  bool sub_page = false;
  const char* sub_title = "Name";
  const char* draft = "";
  size_t draft_len = 0;
  // The shared text field's own rules, per page (S514/EST-446): what may be typed, and the counter's
  // maximum. Defaults keep EST-445's node-name editor exactly as it was.
  const char* sub_hint = "Allowed: letters, digits, space; no [ ] \\ : , ? *";
  size_t      sub_max  = tm::kNodeNameMaxBytes;

  // The ONE shared confirm sheet (EST-422): the row behind it still shows the OLD value until Apply.
  bool confirm = false;
  const char* confirm_title = "";
  const char* confirm_text = "";
  const char* confirm_apply = "Apply";
  bool confirm_danger = false;
};

// Draws a whole Settings frame into `s` (does not present it).
void drawSettings(Screen& s, const SettingsView& v);

// The shared sheet over the current frame. Public because the Name page raises it and the frame must be
// drawn first (the row keeps its old value behind it).
void drawConfirmSheet(Screen& s, const SettingsView& v);

}  // namespace ui
}  // namespace om
