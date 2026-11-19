// SPDX-License-Identifier: MIT
// The Map page: the offline Esri Topo tiles from the card, pan and zoom, and the attribution the licence needs.
//
// The page owns no logic it can be wrong about: which tiles a view needs comes from core/map_view.* (host-tested),
// the pixels come from maplink::tile() (cached, decoded from the pack). This file only places them on the panel.
#pragma once

#include <stdint.h>

#include "mc_store.h"
#include "screen.h"

namespace om {
namespace ui {

struct MapView {
    int64_t wx = 0;   // world pixel at the view's top-left
    int64_t wy = 0;
    int     z = 16;

    // Chrome
    const char* clock = "--:--";
    int         battery_pct = -1;
    bool        charging = false;

    // Why the map cannot be drawn, when it cannot: stated on screen instead of an empty page.
    const char* problem = "";

    // S515/EST-466: the nodes ON the map. ONE table holds the radio's nodes and the SD archive's alike, so this is
    // the same store the Nodes page reads and both stores are drawn. Null = no marker layer at all.
    const mc::ContactStore* store = nullptr;
    // The node the node menu's Map item centred on: a gold ring around its marker until the user pans or zooms
    // (main.cpp owns that state, so a key press anywhere clears it the same way).
    const uint8_t* highlight_pub = nullptr;
};

void drawMap(Screen& s, const MapView& v);

// The map area on the panel, below the chrome. Shared with the key handler so panning and drawing agree.
constexpr int kMapAreaX = 0;
constexpr int kMapAreaY = 26 + 44;          // under the status bar and the page head
constexpr int kMapAreaW = 800;
constexpr int kMapAreaH = 480 - 26 - 44 - 24;   // down to the hint bar

}  // namespace ui
}  // namespace om
