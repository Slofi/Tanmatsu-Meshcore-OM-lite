// SPDX-License-Identifier: MIT
// The Map page's arithmetic: which tiles fill the screen, how far to shift them, how panning and zooming move,
// and a small cache of already-decoded tiles so a pan does not re-decode the world.
//
// Nothing here touches a screen or a card. The page asks "these tiles", decodes what is missing, blits, and the
// host tests pin the answers (an off-by-one here shows up as a one-pixel seam or a missing row on the glass, which
// is slow to find by eye).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace map {

// The map is addressed in "world pixels" at a zoom: 256 * 2^z square. Panning is an integer pixel inside that.
constexpr int kTilePx = 256;
constexpr int kMinZoom = 9;
constexpr int kMaxZoom = 17;

struct ViewTiles {
    uint32_t x0 = 0;   // first tile column (absolute)
    uint32_t y0 = 0;   // first tile row (absolute)
    int      cols = 0; // how many columns the screen needs
    int      rows = 0;
    int      shift_x = 0;   // how far the first tile is shifted left, 0..255
    int      shift_y = 0;
    int      count = 0;     // cols * rows, for the caller's loop
};

// Tiles needed to fill w x h from world pixel (wx, wy) at zoom z. The caller clamps wx/wy first
// (clampWorldPixel below), because a view past the edge of the map is a normal state while panning.
ViewTiles viewTilesFor(int z, int64_t wx, int64_t wy, int w, int h);

// One pan step: move the view by (dx, dy) pixels at zoom z, staying inside the world.
void panBy(int z, int64_t* wx, int64_t* wy, int dx, int dy, int w, int h);

// Zoom while keeping the centre of the screen on the same place. Returns the new zoom; wx/wy are rewritten for it.
int zoomTo(int from_z, int to_z, int64_t* wx, int64_t* wy, int w, int h);

// Keep a world pixel inside the map at zoom z.
void clampWorldPixel(int z, int64_t* wx, int64_t* wy, int w, int h);

// ----------------------------------------------------------------------------------------------- tile cache
// A fixed number of decoded 256x256 RGB565 tiles (1 MB in PSRAM at 8 slots), least-recently-used evicted. The
// pixels live with the caller; this only decides WHICH slot holds what.
// A view is up to 5 x 3 = 15 tiles (800x386 starting mid-tile), so a cache smaller than that thrashes: every
// redraw evicts tiles it is about to ask for again and the screen is decoded from scratch on every key press.
// 20 slots = 2.5 MB of PSRAM, which buys headroom for the column a pan is about to reveal.
constexpr int kCacheSlots = 20;

struct CacheKey {
    bool     valid = false;
    uint8_t  z = 0;
    uint32_t x = 0;
    uint32_t y = 0;
};

struct TileCache {
    CacheKey key[kCacheSlots];
    uint32_t use[kCacheSlots] = {0};   // higher = more recently used
    uint32_t tick = 0;

    // Slot holding this tile, or -1. Also marks it as just used.
    int find(uint8_t z, uint32_t x, uint32_t y);

    // Reserve a slot for a tile we are about to decode into: an empty one if there is, else the least recently
    // used. Marks it used and records the key.
    int take(uint8_t z, uint32_t x, uint32_t y);

    // Clear (e.g. when the pack changes or the card goes away).
    void reset();
};

}  // namespace map
}  // namespace om
