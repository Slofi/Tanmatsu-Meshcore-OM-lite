// SPDX-License-Identifier: MIT
// The Map page's link: decode-get a tile by (z, x, y), with a small cache so a pan reuses what is already decoded.
// The rules (which tiles a view needs, panning, zooming, the cache policy) live in core/map_view.* and are tested
// on the host; this file owns the card handles, the PSRAM buffers and the decoder.
#pragma once

#include <stdint.h>

namespace om {
namespace maplink {

// Once the card is up. Safe to call again after the card is remounted.
void begin();
bool ready();

// Forget the pack handles and the cache (card gone, or a new pack set).
void forget();

// A decoded 256x256 RGB565 tile, or nullptr when it is not in the pack (a hole at the edge of the data) or the
// card is not there. The pointer stays valid until the cache evicts that slot, which cannot happen while the same
// tile is being drawn, so callers draw immediately.
const uint16_t* tile(uint8_t z, uint32_t x, uint32_t y, int* out_w, int* out_h);

// Hits and misses since the last call, so one page compose can report what it actually cost.
void takeStats(uint32_t* hits, uint32_t* misses);

// One-shot boot check: read and decode one tile per pack and log the numbers. Kept from slice 1, because a boot
// line is the cheapest way to know the card and the packs are alive.
void probe(double lat, double lon);

}  // namespace maplink
}  // namespace om
