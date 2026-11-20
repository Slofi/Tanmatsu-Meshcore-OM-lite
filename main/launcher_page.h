// SPDX-License-Identifier: MIT
// The start menu page: three large app cards (S513/EST-432), the app's landing screen.
//
// Behaviour is the CD launcher's (Filip: "look at the CD (Cyberdeck) launcher"): the modules with a clear
// running/stopped/later state and a Stop that frees the module's memory without a restart. The drawing
// style is now the Chat list's rounded cards (S513/EST-430) — same palette (firmware/src/ui/theme.h), same
// focus outline and plate colours — three of them side by side, one per app.
//
// This page replaced the old list + detail pane. It also retires Filip's S499 two-area key rule on purpose
// (EST-432): there is no second area any more, so Enter opens the focused app directly (see core/menu_cards.h
// for the decisions; the shell in main.cpp only acts on them).
#pragma once

#include <stdint.h>

#include "font.h"
#include "screen.h"

namespace om {
namespace ui {

// The start menu's own metrics: the same 16 px gutter and 6 px radius as the Chat cards, so the two card
// pages cannot look like two different designs. Three equal columns from the work box (x 18 … 782):
// (764 - 2*16) / 3 = 244 px each; the odd pixel stays in the gap.
constexpr int kTmMenuGap      = 16;
constexpr int kTmMenuCardH    = 232;
constexpr int kTmMenuTop      = 22;   // the cards' minimum distance under the page head's line
constexpr int kTmMenuRadius   = 6;
constexpr int kTmMenuOwnsGap  = 28;   // the one dim line about the focused card sits this far under the cards

// One large app card. `running`/`available` come from the core's registry in main.cpp; the chip text is
// derived here so a card cannot say RUNNING while its booleans say otherwise.
struct MenuCard {
    const char* name;      // "OM Lite" / "OPS-TOC" / "Mesh-Torry"
    const char* sub;       // one dim line of what it is
    bool        running;
    bool        available;  // false -> LATER: listed, never startable
    const char* memory;     // running card only: the old detail pane's memory line
    const char* uptime;     // running card only: the old detail pane's uptime line
    const char* owns;       // one short dim line about this card when it is focused
};

struct LauncherView {
    const MenuCard* cards = nullptr;
    int             card_count = 0;
    int             focused = 0;      // 0 = OM Lite; clamped by the drawer
    const char*     right_head = "";  // status bar right text: "core ready · radio free"
    // S513/EST-428's transient-line pattern: while this is set it owns the head's right slot (the 3-second
    // "OPS-TOC: coming later" notice). main.cpp only passes it while it is live.
    const char*     notice = nullptr;
    // Battery / clock for the status bar (empty string = hide).
    char            clock[8] = "--:--";
    char            battery[8] = "";
    int             battery_pct = -1;
    bool            charging = false;   // draw the bolt
};

// Draws a whole frame into `s` (does not present it).
void drawLauncher(Screen& s, const LauncherView& v);

// Text helpers that also exist for the other pages (one place for baseline math).
int  textBaseline(Screen& s, Face face, int top, int line_h);
void drawText(Screen& s, Face face, int x, int baseline, const char* utf8, uint16_t colour);
void drawTextRight(Screen& s, Face face, int x_right, int baseline, const char* utf8, uint16_t colour);
void drawTextCentred(Screen& s, Face face, int cx, int baseline, const char* utf8, uint16_t colour);

}  // namespace ui
}  // namespace om
