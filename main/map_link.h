// SPDX-License-Identifier: MIT
// The Map page's link: get a tile by (z, x, y), cached, and decode what is missing.
// The rules (which tiles a view needs, panning, zooming, the cache policy, what to fetch and in which order) live
// in core/map_view.* and are tested on the host; this file owns the card handles, the PSRAM buffers, the two
// decoders and the worker task that runs them.
//
// ⚠️ S506: decoding is NOT done on the drawing task any more. A zoom makes twelve tiles miss at once, and twelve
// decodes inside one compose froze the panel for 1.3 s (measured S503). Now drawTile() only hands out tiles that
// are already decoded, showView() tells a worker task what is still missing, and the page repaints as tiles land.
#pragma once

#include <stdint.h>

#include "map_view.h"

namespace om {
namespace maplink {

// Once the card is up. Safe to call again after the card is remounted (the worker is started once).
void begin();
bool ready();

// ---------------------------------------------------------------------------------- the page (never decodes here)
// Copy one decoded tile into the caller's panel through `blit`. Returns false when the tile is not decoded yet or
// the pack does not have it: the caller leaves the void colour there, and the worker brings it if it exists.
// The cache lock is held for the length of the blit, so the buffer cannot be evicted out from under the copy.
using TileBlit = void (*)(const uint16_t* px, int tw, int th, int dx, int dy, void* ctx);
bool drawTile(uint8_t z, uint32_t x, uint32_t y, int dx, int dy, TileBlit blit, void* ctx);

// S514: a placeholder blit. Draw a REGION of a decoded tile into a destination rectangle, NEAREST NEIGHBOUR,
// without touching the cache's LRU: a stand-in must never evict a tile the fill still needs. Returns false when
// the source tile is not Ready or every argument is empty.
using TileBlitScaled = void (*)(const uint16_t* px, int tw, int th, int sx, int sy, int sw, int sh, int dx, int dy,
                                int dw, int dh, void* ctx);
bool drawTileScaled(uint8_t z, uint32_t x, uint32_t y, int sx, int sy, int sw, int sh, int dx, int dy, int dw,
                    int dh, TileBlitScaled blit, void* ctx);

// Hand the worker the tiles this view is missing, centre-out. Cheap and idempotent, so the page calls it on every
// compose. Returns how many tiles were queued, or -1 when the view needs more than the queue holds.
int showView(uint8_t z, const om::map::ViewTiles& vt);

// The worker still has tiles to fetch. While this is true the page keeps its input latency short.
bool busy();
// One or more tiles finished (decoded, or found missing) since the last call: the page should repaint.
bool takeLanded();

// ---------------------------------------------------------------------------------- boot checks (synchronous)
// One-shot: read and decode one tile per pack and log the numbers. The cheapest proof the card and packs are alive.
void probe(double lat, double lon);

// Read one tile and decode it with BOTH decoders, then compare the pixels. The hardware codec is what makes the
// map fast, so this is how the boot log proves it agrees with the software one, colour order included.
void verifyDecoders(double lat, double lon);

}  // namespace maplink
}  // namespace om
