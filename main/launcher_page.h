// SPDX-License-Identifier: MIT
// The inner Launcher page: the approved 800x480 look (shots/2026-11-13-launcher-home.png).
//
// Behaviour is the CD launcher's (Filip: "look at the CD (Cyberdeck) launcher"): a table of modules with
// a clear running/stopped state and a Stop that frees the module's memory without a restart; the stock
// launcher starts one app at a time, and *this* app is the one that keeps more of them alive inside.
// The drawing style is the L2 OM Lite's (same palette from firmware/src/ui/theme.h, same row/band/ring
// metrics scaled to 800x480 — tm_theme.h).
#pragma once

#include <stdint.h>

#include "font.h"
#include "screen.h"

namespace om {
namespace ui {

// One row of the module list. `running`/`available` come from the core's registry in main.cpp.
struct ModuleRow {
    const char* name;
    const char* sub;      // "mesh companion · 4 pages" / "game · Wi-Fi · later"
    const char* badge;    // "RUNNING" / "LATER" (empty = none)
    bool        running;
    bool        available;  // false -> the row is dimmed and cannot be opened
};

// What the detail pane shows for the focused module.
struct ModuleDetail {
    const char* title;
    const char* meta;       // "started 09:41 · up 1 h 12 m"
    const char* kv[4][2];   // label / value pairs, nullptr-terminated
    const char* note;
};

// Focused index and the screen contents the shell wants drawn.
struct LauncherView {
    const ModuleRow* rows = nullptr;
    int              row_count = 0;
    int              focused = 0;
    // Focus is TWO-AREA (Filip, S499): the list on the left and the actions on the right. Enter on a row
    // moves the focus to the actions; Esc there comes back. Inside the actions, Left/Right pick a button.
    bool             focus_actions = false;
    int              action_index = 0;   // 0 = Open, 1 = Stop
    const ModuleDetail* detail = nullptr;
    const char*      right_head = "";    // status bar right text: "core ready · radio free"
    // Battery / clock for the status bar (empty string = hide).
    char             clock[8] = "--:--";
    char             battery[8] = "";
    int              battery_pct = -1;
    bool             charging = false;   // draw the bolt
};

// Draws a whole frame into `s` (does not present it).
void drawLauncher(Screen& s, const LauncherView& v);

// Text helpers that also exist for the other pages later (one place for baseline math).
int  textBaseline(Screen& s, Face face, int top, int line_h);
void drawText(Screen& s, Face face, int x, int baseline, const char* utf8, uint16_t colour);
void drawTextRight(Screen& s, Face face, int x_right, int baseline, const char* utf8, uint16_t colour);
void drawTextCentred(Screen& s, Face face, int cx, int baseline, const char* utf8, uint16_t colour);

}  // namespace ui
}  // namespace om
