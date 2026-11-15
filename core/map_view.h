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
// A fixed number of decoded 256x256 RGB565 tiles (128 KiB each: 2.5 MB of PSRAM at 20 slots), least-recently-used
// evicted. The pixels live with the caller; this only decides WHICH slot holds what.
// A view is up to 5 x 3 = 15 tiles (800x386 starting mid-tile), so a cache smaller than that thrashes: every
// redraw evicts tiles it is about to ask for again and the screen is decoded from scratch on every key press.
// 20 slots, which buys headroom for the column a pan is about to reveal.
constexpr int kCacheSlots = 20;

// A slot is one of three things (S506). Decoding moved off the drawing task, so a slot can be *being written*:
//   Empty   — free; whatever its pixels hold is stale and must not be drawn.
//   Pending — reserved by the decoder, which is writing that buffer right now. It is NOT a hit (the pixels are
//             garbage until the decode finishes) and it must NEVER be evicted, because the decoder holds a raw
//             pointer into that buffer and an eviction would hand the same memory to a second tile.
//   Ready   — decoded; drawable, and evictable.
enum class SlotState : uint8_t { Empty = 0, Pending = 1, Ready = 2 };

struct CacheKey {
    SlotState state = SlotState::Empty;
    uint8_t   z = 0;
    uint32_t  x = 0;
    uint32_t  y = 0;
};

// Tiles the pack does not have (holes at the edge of the data) or that failed to decode are remembered, because
// otherwise the worker asks for them again on every repaint: a permanent hole would become a permanent retry.
constexpr int kFailedSlots = 24;

struct FailedKey {
    bool     used = false;
    uint8_t  z = 0;
    uint32_t x = 0;
    uint32_t y = 0;
};

struct TileCache {
    CacheKey  key[kCacheSlots];
    uint32_t  use[kCacheSlots] = {0};   // higher = more recently used
    uint32_t  tick = 0;
    FailedKey failed[kFailedSlots];
    int       failed_next = 0;

    // Slot holding this tile, or -1. READY tiles only, and it marks the slot as just used. This is the DRAWING
    // path: handing out a pending slot would draw a half-decoded tile.
    int find(uint8_t z, uint32_t x, uint32_t y);

    // Read-only query, no LRU effect: what state is this tile in? Empty when it is in no slot.
    SlotState stateOf(uint8_t z, uint32_t x, uint32_t y) const;

    // Reserve a slot to decode into: an empty one if there is, else the least recently used READY one. Returns
    // -1 when every slot is pending, which the caller must treat as "try again in a moment", never as a miss.
    // The slot comes back Pending.
    int take(uint8_t z, uint32_t x, uint32_t y);

    // The decode finished. `ok` -> the slot becomes drawable; a failure sends the tile to the failed ring and
    // frees the slot, so its stale pixels are never drawn.
    void commit(int slot, bool ok);

    // The pack has no such tile: remember it, so it is not asked for again.
    void markFailed(uint8_t z, uint32_t x, uint32_t y);
    bool isFailed(uint8_t z, uint32_t x, uint32_t y) const;

    // Clear (e.g. when the pack changes or the card goes away).
    void reset();
};

// ----------------------------------------------------------------------------------------------- what to fetch
// One tile for the worker to decode.
struct TileRequest {
    uint8_t  z = 0;
    uint32_t x = 0;
    uint32_t y = 0;
};

// The most tiles one view can ask for. 5 x 3 fills 800x386; this is headroom for a bigger panel.
constexpr int kMaxRequests = 40;

// The tiles this view is still missing, in the order a person notices them: CENTRE-OUT, so what is under the
// middle of the screen is decoded first and the edges fill in while the eye is still there. Tiles that are
// already Ready, already Pending (the worker is on them) or known missing are not requested.
// Returns the count, or -1 when the view needs more than `cap` requests: a caller bug, said out loud rather
// than silently dropping the edges of the map.
int planTiles(const ViewTiles& vt, uint8_t z, const TileCache& cache, TileRequest* out, int cap);

}  // namespace map
}  // namespace om
