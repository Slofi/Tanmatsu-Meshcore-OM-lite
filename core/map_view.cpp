// SPDX-License-Identifier: MIT
#include "map_view.h"

namespace om {
namespace map {

namespace {

int64_t worldSize(int z) { return (int64_t)kTilePx << z; }

// Floor division and modulo for the negative cases panning can produce.
int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0))) --q;
    return q;
}
int floorMod(int64_t a, int64_t b) {
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return (int)r;
}

}  // namespace

ViewTiles viewTilesFor(int z, int64_t wx, int64_t wy, int w, int h) {
    ViewTiles v;
    if (z < 0) z = 0;
    const int64_t maxTile = ((int64_t)1 << z) - 1;

    const int64_t tx0   = floorDiv(wx, kTilePx);
    const int64_t ty0   = floorDiv(wy, kTilePx);
    const int     sx    = floorMod(wx, kTilePx);
    const int     sy    = floorMod(wy, kTilePx);

    // From the shift, the screen needs (shift + w) pixels of tiles, rounded up. A view that starts exactly on a
    // tile boundary needs w/256 tiles, not w/256 + 1.
    v.cols = (sx + w + kTilePx - 1) / kTilePx;
    v.rows = (sy + h + kTilePx - 1) / kTilePx;
    v.shift_x = sx;
    v.shift_y = sy;

    // Negative tile numbers cannot exist; the caller clamps the world pixel, this is the belt and braces.
    if (tx0 < 0) {
        v.x0 = 0;
    } else if (tx0 > maxTile) {
        v.x0 = (uint32_t)maxTile;
    } else {
        v.x0 = (uint32_t)tx0;
    }
    if (ty0 < 0) {
        v.y0 = 0;
    } else if (ty0 > maxTile) {
        v.y0 = (uint32_t)maxTile;
    } else {
        v.y0 = (uint32_t)ty0;
    }

    // Do not ask for tiles past the last column/row of the map: the view is clipped, not padded with garbage.
    if ((int64_t)v.x0 + v.cols - 1 > maxTile) v.cols = (int)(maxTile - (int64_t)v.x0 + 1);
    if ((int64_t)v.y0 + v.rows - 1 > maxTile) v.rows = (int)(maxTile - (int64_t)v.y0 + 1);
    if (v.cols < 1) v.cols = 1;
    if (v.rows < 1) v.rows = 1;
    v.count = v.cols * v.rows;
    return v;
}

void clampWorldPixel(int z, int64_t* wx, int64_t* wy, int w, int h) {
    const int64_t size = worldSize(z);
    int64_t       maxx = size - w;
    int64_t       maxy = size - h;
    if (maxx < 0) maxx = 0;   // a view wider than the map: pin to the top left rather than oscillate
    if (maxy < 0) maxy = 0;
    if (*wx < 0) *wx = 0;
    if (*wy < 0) *wy = 0;
    if (*wx > maxx) *wx = maxx;
    if (*wy > maxy) *wy = maxy;
}

void panBy(int z, int64_t* wx, int64_t* wy, int dx, int dy, int w, int h) {
    *wx += dx;
    *wy += dy;
    clampWorldPixel(z, wx, wy, w, h);
}

int zoomTo(int from_z, int to_z, int64_t* wx, int64_t* wy, int w, int h) {
    if (to_z < kMinZoom) to_z = kMinZoom;
    if (to_z > kMaxZoom) to_z = kMaxZoom;
    if (to_z == from_z) return from_z;
    // Keep the CENTRE of the screen on the same place: centre_world(from) / 2^from == centre_world(to) / 2^to.
    const int64_t cx = *wx + w / 2;
    const int64_t cy = *wy + h / 2;
    const int     dz = to_z - from_z;
    int64_t       ncx = cx;
    int64_t       ncy = cy;
    if (dz > 0) {
        ncx = cx << dz;
        ncy = cy << dz;
    } else {
        ncx = cx >> (-dz);
        ncy = cy >> (-dz);
    }
    *wx = ncx - w / 2;
    *wy = ncy - h / 2;
    clampWorldPixel(to_z, wx, wy, w, h);
    return to_z;
}

// ----------------------------------------------------------------------------------------------- tile cache

int TileCache::find(uint8_t z, uint32_t x, uint32_t y) {
    for (int i = 0; i < kCacheSlots; i++) {
        if (key[i].valid && key[i].z == z && key[i].x == x && key[i].y == y) {
            use[i] = ++tick;
            return i;
        }
    }
    return -1;
}

int TileCache::take(uint8_t z, uint32_t x, uint32_t y) {
    int victim = -1;
    for (int i = 0; i < kCacheSlots; i++) {
        if (!key[i].valid) {
            victim = i;
            break;
        }
        if (victim < 0 || use[i] < use[victim]) victim = i;
    }
    if (victim < 0) victim = 0;
    key[victim].valid = true;
    key[victim].z     = z;
    key[victim].x     = x;
    key[victim].y     = y;
    use[victim]       = ++tick;
    return victim;
}

void TileCache::reset() {
    for (int i = 0; i < kCacheSlots; i++) {
        key[i] = CacheKey{};
        use[i] = 0;
    }
    tick = 0;
}

}  // namespace map
}  // namespace om
