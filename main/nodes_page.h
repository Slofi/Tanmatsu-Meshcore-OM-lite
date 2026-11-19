// SPDX-License-Identifier: MIT
// The Nodes page: who we have heard, in the L2's two-pane shape at 800x480 (shots/2026-11-13-nodes.png).
//
// What it shows is only what we can prove: an advert we verified (name, key, type, timestamp, and the radio's
// own RSSI/SNR). The action row from the preview (DM, Trace, Favourite) is NOT drawn yet — reply paths and
// favourites are the next slice, and a button that does nothing would be a lie on a device this honest about
// its state.
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

    // chrome
    const char* clock = "--:--";
    int         battery_pct = -1;
    bool        charging = false;
};

void drawNodes(Screen& s, const NodesView& v);

// UTF-8 aware truncation to a pixel width, with an ellipsis — shared with Chat so long names and messages end
// the same way on both pages. The right-aligned form exists for the chat's own messages (Filip, S502: mine on
// the right, everyone else's on the left, so it is clear who said what).
void drawTextClipped(Screen& s, Face face, int x, int baseline, int max_w, const char* utf8, uint16_t colour);
void drawTextRightClipped(Screen& s, Face face, int x_right, int baseline, int max_w, const char* utf8,
                          uint16_t colour);

}  // namespace ui
}  // namespace om
