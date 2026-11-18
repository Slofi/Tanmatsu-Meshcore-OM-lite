// SPDX-License-Identifier: MIT
// The parts every page draws the same way: the status bar, the key-hint bar, the five symbol sigils, and the
// page-head row. They were inside launcher_page.cpp until the second and third page needed them (S502, step 4);
// the numbers are the approved ones, moved, not changed.
//
// One home matters here: the symbol key bar is how Filip knows which page he is on, so it must not be possible
// for the Chat page to draw a differently-spaced bar from the Launcher's.
#pragma once

#include <stdint.h>

#include "pages.h"
#include "screen.h"

namespace om {
namespace ui {

// One chip in the hint bar: the key as printed on it, and what it does here.
struct HintItem {
    const char* key;     // "up/dn", "ret", "esc"
    const char* label;   // "move", "open", "back"
};

// Clock left; battery, wifi and gear sigils right. battery_pct < 0 hides the battery.
void drawStatusBar(Screen& s, const char* clock, int battery_pct, bool charging);

// Page-head band: title left, right-hand text right, and the line under it. Returns the y of the line.
int drawPageHead(Screen& s, const char* title, const char* right_text);

// The bottom bar: this page's actions on the left, the five symbol keys on the right with `active` in gold.
// S513/EST-435: it also draws the app's ONE notice, just above itself, when one is live — see noticeSet().
void drawHintBar(Screen& s, tm::Page active, const HintItem* hints, int hint_count);

// --------------------------------------------------------------------------------- the one notice (S513/EST-435)
// Short-lived texts ("Advert already sent, wait 4 s", later "New node: …") appear on EVERY page, so they cannot
// belong to a page's view: they belong to the chrome. One slot for the whole app, the newest wins, and it is
// drawn by drawHintBar() above the hint bar — every page already calls that, so no page can forget it and no page
// needs a clock of its own.
//
// ⚠️ The WRITER may be the radio task (a notice decided while a packet is handled) and the READER is always the
// main task, so the slot is published under a critical section — the `time_source` pattern, not a shared buffer.
// Set from the task that decides; drawn only by the main task.
void noticeSet(const char* text, uint32_t ttl_ms);
void noticeClear();

// The live text, or nullptr. Returns a copy taken under the lock, so the string cannot change while it is drawn.
const char* noticeText();

// S513 (Claude): does the screen need a repaint for the notice? True once after a notice was set or cleared, and
// once when a live notice runs out. The radio task sets "Advert sent" / "New node" with no key press behind it,
// so without this the strip only appeared if something else happened to repaint within its 3 s (Filip: "a sends
// an ADV, but still no pop-up"). Main task only; call it every loop pass.
bool noticeNeedsRepaint();

// The five symbol keys as vectors (BRIEF § 5). kind: 0 triangle, 1 square, 2 circle, 3 cloud, 4 diamond.
void drawSymbolSigil(Screen& s, int kind, int cx, int cy, int r, uint16_t c, bool active);

// The rounded card style of S513/EST-430: the Chat list's cards and now the start menu's three app cards.
// `drawRoundedRect` is a per-row span fill with an integer corner (no float, no allocation); `drawCardPlate`
// draws the outline as the outer plate and the fill inside it, so the ring follows the corners exactly
// (thickness 1 = a resting card, 2 = the gold focus outline). One implementation, so the two card pages
// cannot drift apart.
void drawRoundedRect(Screen& s, int x, int y, int w, int h, int r, uint16_t c);
void drawCardPlate(Screen& s, int x, int y, int w, int h, int radius, int thickness, uint16_t outline,
                   uint16_t fill);

}  // namespace ui
}  // namespace om
