// SPDX-License-Identifier: MIT
// The Nodes page: who we have heard, in the L2's two-pane shape at 800x480 (shots/2026-11-13-nodes.png).
//
// What it shows is only what we can prove: an advert we verified (name, key, type, timestamp, and the radio's
// own RSSI/SNR). S514/EST-455 adds the per-node NODE MENU (Enter on a row): only DM, Info, Favourite and Remove,
// because only those work on the Tanmatsu today. The menu's rules are core/node_menu.h (host-tested); this file
// only draws the sheet and the small gold favourite dot.
#pragma once

#include <stdint.h>

#include "font.h"
#include "mc_store.h"
#include "screen.h"

namespace om {
namespace ui {

struct NodesView {
    const mc::ContactStore* store = nullptr;
    const int*              order = nullptr;   // contactsOrder(), FILTERED by `query` (S514/EST-449)
    int                     order_count = 0;   // N in "N of M": rows after the search filter
    int                     focused = 0;       // index INTO order
    int                     in_last_hour = 0;
    uint32_t                now = 0;

    // S514/EST-449: the search bar is the list's top row. `query` is the live filter text (null/empty =
    // all nodes) and `search_focused` means the BAR owns the keys, so no row is drawn as selected.
    const char* query = nullptr;
    bool        search_focused = false;

    // S514/EST-455: the node menu of the FOCUSED row. `menu_focus` is a row index inside core/node_menu.h's
    // list; main.cpp owns it and the side effects, this view only says whether the sheet is up.
    bool menu_open  = false;
    int  menu_focus = 0;

    // chrome
    const char* clock = "--:--";
    int         battery_pct = -1;
    bool        charging = false;
};

void drawNodes(Screen& s, const NodesView& v);

// The node menu sheet (S514/EST-455): a bottom sheet in the confirm sheet's house style, drawn OVER the page.
// The rows come from core/node_menu.h so what is offered and what is drawn cannot disagree. `focus` is the
// focused row; the favourite row's value is "on"/"off" from the contact itself.
void drawNodeMenu(Screen& s, const mc::Contact& c, int focus, uint32_t now);

// UTF-8 aware truncation to a pixel width, with an ellipsis — shared with Chat so long names and messages end
// the same way on both pages. The right-aligned form exists for the chat's own messages (Filip, S502: mine on
// the right, everyone else's on the left, so it is clear who said what).
void drawTextClipped(Screen& s, Face face, int x, int baseline, int max_w, const char* utf8, uint16_t colour);
void drawTextRightClipped(Screen& s, Face face, int x_right, int baseline, int max_w, const char* utf8,
                          uint16_t colour);

}  // namespace ui
}  // namespace om
