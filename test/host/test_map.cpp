// SPDX-License-Identifier: MIT
// Host tests for the map pack reader's rules (S503, step 6): the 24-byte record, the binary search over a
// sorted index, the zoom -> pack mapping and the slippy-map coordinate maths.
//
// The search runs through the same record-reader callback the device uses (there it reads from the card, here
// from an array), so what is tested is the code that runs, not a copy of it.
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "map_pack.h"
#include "map_view.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                       \
      return;                                                             \
    }                                                                     \
  } while (0)

#define CHECK_EQ(got, want)                                                          \
  do {                                                                               \
    ++g_checks;                                                                      \
    const long g_ = static_cast<long>(got);                                          \
    const long w_ = static_cast<long>(want);                                         \
    if (g_ != w_) {                                                                  \
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,        \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

// ---- an index in memory, read through the same callback the device uses
struct FakeIdx {
    uint8_t* bytes;
    uint32_t count;
    uint32_t fail_at;        // simulate a short read on one record (9999 = never)
};

uint8_t* g_buf = nullptr;

FakeIdx g_idx;

bool readRec(void* ctx, uint32_t i, om::map::Entry* out) {
    FakeIdx* f = static_cast<FakeIdx*>(ctx);
    if (i == f->fail_at) return false;
    if (i >= f->count) return false;
    return om::map::decodeEntry(f->bytes + (size_t)i * om::map::kIdxRecordBytes, out);
}

void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
void put64(uint8_t* p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

// Build a sorted index of `count` tiles at zoom z: x runs slowest, y fastest, exactly as the packer writes.
void buildIndex(uint32_t count, uint8_t z, uint32_t y_per_x) {
    g_buf = new uint8_t[(size_t)count * om::map::kIdxRecordBytes];
    uint64_t off = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t* rec = g_buf + (size_t)i * om::map::kIdxRecordBytes;
        memset(rec, 0, om::map::kIdxRecordBytes);
        rec[0] = z;
        put32(rec + 4, 1000 + i / y_per_x);
        put32(rec + 8, 2000 + i % y_per_x);
        put64(rec + 12, off);
        put32(rec + 20, 5000 + i);
        off += 5000 + i;
    }
    g_idx.bytes        = g_buf;
    g_idx.count        = count;
    g_idx.fail_at = 9999;
}

void test_decode_entry() {
    uint8_t rec[om::map::kIdxRecordBytes];
    memset(rec, 0, sizeof(rec));
    rec[0] = 16;
    put32(rec + 4, 35789);
    put32(rec + 8, 23466);
    put64(rec + 12, 2809209108ull);
    put32(rec + 20, 5390);
    om::map::Entry e;
    CHECK(om::map::decodeEntry(rec, &e));
    CHECK_EQ(e.z, 16);
    CHECK_EQ(e.x, 35789);
    CHECK_EQ(e.y, 23466);
    CHECK_EQ((long)(e.offset), 2809209108l);
    CHECK_EQ(e.length, 5390);

    // A zero-length tile is not a tile, and a wild zoom is not a record: both refused rather than seeking.
    put32(rec + 20, 0);
    CHECK(!om::map::decodeEntry(rec, &e));
    put32(rec + 20, 100);
    rec[0] = 25;
    CHECK(!om::map::decodeEntry(rec, &e));
    CHECK(!om::map::decodeEntry(nullptr, &e));
}

void test_zoom_to_pack() {
    CHECK(strcmp(om::map::packDirForZoom(9), "slo") == 0);
    CHECK(strcmp(om::map::packDirForZoom(15), "slo") == 0);
    CHECK(strcmp(om::map::packDirForZoom(16), "slo16") == 0);
    CHECK(strcmp(om::map::packDirForZoom(17), "slo17") == 0);
    CHECK(strcmp(om::map::packDirForZoom(18), "slo17") == 0);
    CHECK(om::map::packDirForZoom(8) == nullptr);
    CHECK(om::map::packDirForZoom(19) == nullptr);
}

void test_binary_search() {
    buildIndex(1000, 16, 25);   // 40 x-columns of 25 tiles
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 16, 1000, 2000), 0);      // first
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 16, 1039, 2024), 999);    // last
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 16, 1020, 2010), 510);    // 20*25 + 10

    // Not in the pack: a y past the column, an x past the end, the wrong zoom.
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 16, 1020, 2099), -1);
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 16, 1040, 2000), -1);
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 15, 1000, 2000), -1);

    // Empty index, and a reader that dies: both say "no tile", never a seek.
    CHECK_EQ(om::map::findTile(0, readRec, &g_idx, 16, 1000, 2000), -1);
    g_idx.fail_at = 499;   // the search's first midpoint for these 1000 records
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 16, 1039, 2024), -1);
    g_idx.fail_at = 9999;

    // One record only (both ends of the guard).
    buildIndex(1, 17, 1);
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 17, 1000, 2000), 0);
    CHECK_EQ(om::map::findTile(g_idx.count, readRec, &g_idx, 17, 1001, 2000), -1);
    delete[] g_buf;
    g_buf = nullptr;
}

void test_coordinates() {
    // central Slovenia, z16: 46.15 N, 14.99 E. Values computed independently from the slippy-map formula.
    CHECK_EQ(om::map::tileXForLon(14.99, 16), 35496);
    CHECK_EQ(om::map::tileYForLat(46.15, 16), 23275);   // cross-checked in Python, same formula, same answer
    CHECK_EQ(om::map::tileXForLon(14.99, 9), 277);
    // Round trip: the centre of a tile maps back to a coordinate inside it.
    const double lon = om::map::lonForTileX(35496, 16);
    const double lat = om::map::latForTileY(23275, 16);
    CHECK(fabs(lon - 14.9881) < 0.01);
    CHECK(fabs(lat - 46.1513) < 0.01);   // the centre of tile y=23275
    CHECK_EQ(om::map::tileXForLon(lon, 16), 35496);
    CHECK_EQ(om::map::tileYForLat(lat, 16), 23275);
    // Out-of-range input clamps instead of wrapping.
    CHECK_EQ(om::map::tileXForLon(-400.0, 16), 0);
    CHECK_EQ(om::map::tileYForLat(89.999, 5), 0);
}


void test_view_tiles() {
    // A view starting on a tile boundary: exactly ceil(800/256)=4 columns, ceil(480/256)=2 rows (no extra tile).
    om::map::ViewTiles v = om::map::viewTilesFor(16, 0, 0, 800, 480);
    CHECK_EQ(v.x0, 0);
    CHECK_EQ(v.y0, 0);
    CHECK_EQ(v.cols, 4);
    CHECK_EQ(v.rows, 2);
    CHECK_EQ(v.shift_x, 0);
    CHECK_EQ(v.count, 8);

    // Shifted 100 px into the tiles: one more row is needed, and the shift is what the blit subtracts.
    v = om::map::viewTilesFor(16, 100, 100, 800, 480);
    CHECK_EQ(v.cols, 4);
    CHECK_EQ(v.rows, 3);
    CHECK_EQ(v.shift_x, 100);
    CHECK_EQ(v.shift_y, 100);
    CHECK_EQ(v.count, 12);

    // A world pixel that is not on a tile boundary: floor, not truncate (this is where a negative pan would break).
    v = om::map::viewTilesFor(16, 256 * 7 + 255, 256 * 9, 800, 480);
    CHECK_EQ(v.x0, 7);
    CHECK_EQ(v.y0, 9);
    CHECK_EQ(v.shift_x, 255);

    // At the far corner of the map the view is CLIPPED, not extended into tiles that do not exist.
    v = om::map::viewTilesFor(9, 511 * 256 + 200, 511 * 256 + 200, 800, 480);
    CHECK_EQ(v.x0, 511);
    CHECK_EQ(v.y0, 511);
    CHECK_EQ(v.cols, 1);
    CHECK_EQ(v.rows, 1);
}

void test_pan_and_zoom() {
    int64_t wx = 1000, wy = 2000;
    om::map::panBy(16, &wx, &wy, -500, -500, 800, 480);
    CHECK_EQ(wx, 500);                    // a normal step
    CHECK_EQ(wy, 1500);
    om::map::panBy(16, &wx, &wy, -99999, -99999, 800, 480);
    CHECK_EQ(wx, 0);                      // clamped at the top-left corner, never negative
    CHECK_EQ(wy, 0);
    const int64_t size = (int64_t)om::map::kTilePx << 16;
    om::map::panBy(16, &wx, &wy, size, size, 800, 480);
    CHECK_EQ(wx, size - 800);             // and at the far edge, so the screen never leaves the map
    CHECK_EQ(wy, size - 480);

    // Zoom keeps the CENTRE: 16 -> 15 halves it, 15 -> 16 doubles it back to where it was.
    int64_t zx = 256 * 100 + 128, zy = 256 * 200 + 128;   // even centre offsets, so the round trip is exact
    const int64_t c0x = zx + 400, c0y = zy + 240;
    CHECK_EQ(om::map::zoomTo(16, 15, &zx, &zy, 800, 480), 15);
    CHECK_EQ(zx + 400, c0x / 2);
    CHECK_EQ(zy + 240, c0y / 2);
    om::map::zoomTo(15, 16, &zx, &zy, 800, 480);
    CHECK_EQ(zx, 256 * 100 + 128);
    CHECK_EQ(zy, 256 * 200 + 128);

    // The zoom range is the pack's: z9..z17, and the clamp is enforced here rather than by the keys.
    CHECK_EQ(om::map::zoomTo(9, 3, &zx, &zy, 800, 480), om::map::kMinZoom);
    CHECK_EQ(om::map::zoomTo(17, 25, &zx, &zy, 800, 480), om::map::kMaxZoom);
}

// S506: the cache grew a state machine, because decoding moved to another task. Three rules matter and each of
// them fails silently on the glass: a pending slot must not be drawn, must not be evicted, and a tile the pack
// does not have must not be asked for again and again.
void test_tile_cache() {
    om::map::TileCache c;
    c.reset();
    CHECK_EQ(c.find(16, 1, 1), -1);                       // nothing cached yet
    CHECK_EQ((int)c.stateOf(16, 1, 1), (int)om::map::SlotState::Empty);

    for (uint32_t i = 0; i < (uint32_t)om::map::kCacheSlots; i++) {
        const int slot = c.take(16, i, 0);
        CHECK(slot >= 0 && slot < om::map::kCacheSlots);
        // Pending is not a hit: this is what the drawing task sees while the decoder works.
        CHECK_EQ(c.find(16, i, 0), -1);
        CHECK_EQ((int)c.stateOf(16, i, 0), (int)om::map::SlotState::Pending);
        c.commit(slot, true);
    }
    CHECK_EQ(c.find(16, 0, 0), 0);                        // all twenty are held
    CHECK_EQ(c.find(16, 7, 0), 7);

    // Touch slot 0 so slot 1 is now the least recently used, then take one more tile.
    CHECK_EQ(c.find(16, 0, 0), 0);
    const int s20 = c.take(16, 20, 0);
    c.commit(s20, true);
    CHECK_EQ(c.find(16, 1, 0), -1);                       // evicted: least recently used
    CHECK_EQ(c.find(16, 0, 0), 0);                        // kept: it was touched
    CHECK_EQ(c.find(16, 20, 0) >= 0, true);

    // A different zoom is a different key, and the victim is whatever is least recently used AT THAT MOMENT:
    // after touching 16/0/0 twice and inserting 16/20/0, the stale one is 16/2/0 (never touched since insertion).
    const int s17 = c.take(17, 0, 0);
    c.commit(s17, true);
    CHECK_EQ(c.find(16, 2, 0), -1);                       // the true LRU
    CHECK_EQ(c.find(16, 0, 0) >= 0, true);                // touched, so kept
    CHECK_EQ(c.find(17, 0, 0) >= 0, true);                // the new key is in

    c.reset();
    CHECK_EQ(c.find(17, 0, 0), -1);
}

void test_cache_pending_is_untouchable() {
    om::map::TileCache c;
    c.reset();

    // One tile being decoded in the oldest slot, the rest full and newer: an eviction must pass it by.
    const int pending = c.take(16, 100, 100);              // slot 0, the oldest use stamp
    CHECK_EQ((int)c.stateOf(16, 100, 100), (int)om::map::SlotState::Pending);
    for (uint32_t i = 0; i < (uint32_t)om::map::kCacheSlots - 1; i++) {
        c.commit(c.take(16, 200 + i, 0), true);
    }
    CHECK_EQ((int)c.stateOf(16, 100, 100), (int)om::map::SlotState::Pending);   // still reserved
    const int other = c.take(16, 300, 0);
    CHECK(other >= 0);
    CHECK(other != pending);                              // the victim is a ready tile, never the pending one
    CHECK_EQ((int)c.stateOf(16, 100, 100), (int)om::map::SlotState::Pending);
    c.commit(other, true);

    // Every slot pending at once: say -1 rather than hand out a buffer that is being written.
    om::map::TileCache full;
    full.reset();
    for (int i = 0; i < om::map::kCacheSlots; i++) CHECK(full.take(16, (uint32_t)i, 9) >= 0);
    CHECK_EQ(full.take(16, 999, 9), -1);

    // A failed decode frees the slot and is remembered, so a hole is not retried for ever.
    om::map::TileCache f;
    f.reset();
    const int s = f.take(16, 5, 5);
    CHECK_EQ(f.isFailed(16, 5, 5), false);
    f.commit(s, false);
    CHECK_EQ((int)f.stateOf(16, 5, 5), (int)om::map::SlotState::Empty);
    CHECK_EQ(f.find(16, 5, 5), -1);
    CHECK_EQ(f.isFailed(16, 5, 5), true);
    CHECK_EQ(f.isFailed(16, 5, 6), false);                // only that exact tile
    f.reset();
    CHECK_EQ(f.isFailed(16, 5, 5), false);                // a new pack forgets the holes
}

void test_plan_tiles_order() {
    // A 4x3 view (the panel's 800x386 at some offsets) starting at tile (10, 20). Nothing cached.
    om::map::ViewTiles vt;
    vt.x0    = 10;
    vt.y0    = 20;
    vt.cols  = 4;
    vt.rows  = 3;
    vt.count = 12;

    om::map::TileCache c;
    c.reset();
    om::map::TileRequest req[om::map::kMaxRequests];
    const int            n = om::map::planTiles(vt, 16, c, req, om::map::kMaxRequests);
    CHECK_EQ(n, 12);

    // Centre-out, ties in row-major order: the 4x3 grid has no single middle tile, so the two tiles nearest the
    // middle come first (cols 1-2, row 1), then the same two columns in the rows above and below, then the side
    // columns, and the four corners last. Distance = (2*col-(cols-1))^2 + (2*row-(rows-1))^2: d=1, 5, 9, 13.
    static const uint32_t wx[12] = {11, 12, 11, 12, 11, 12, 10, 13, 10, 13, 10, 13};
    static const uint32_t wy[12] = {21, 21, 20, 20, 22, 22, 21, 21, 20, 20, 22, 22};
    for (int i = 0; i < 12; i++) {
        CHECK_EQ(req[i].z, 16);
        CHECK_EQ(req[i].x, wx[i]);
        CHECK_EQ(req[i].y, wy[i]);
    }

    // A tile that is ready is not requested; a tile being decoded is not requested twice; a tile the pack does
    // not have is not requested at all. Each of those removes exactly one entry from the plan.
    c.commit(c.take(16, 11, 21), true);    // ready
    const int s2 = c.take(16, 12, 21);     // pending, never committed
    c.markFailed(16, 10, 20);
    CHECK_EQ((int)c.stateOf(16, 12, 21), (int)om::map::SlotState::Pending);
    CHECK(s2 >= 0);
    const int n2 = om::map::planTiles(vt, 16, c, req, om::map::kMaxRequests);
    CHECK_EQ(n2, 9);
    for (int i = 0; i < n2; i++) {
        CHECK(!(req[i].x == 11 && req[i].y == 21));
        CHECK(!(req[i].x == 12 && req[i].y == 21));
        CHECK(!(req[i].x == 10 && req[i].y == 20));
    }
    CHECK_EQ(req[0].x, 11);               // the next-nearest tile (col 1, row 0) still comes first
    CHECK_EQ(req[0].y, 20);

    // A cap too small for the view is refused, not silently truncated (which would leave the edges blank).
    CHECK_EQ(om::map::planTiles(vt, 16, c, req, 11), -1);
    CHECK_EQ(om::map::planTiles(vt, 16, c, req, 12), 9);
    CHECK_EQ(om::map::planTiles(vt, 16, c, nullptr, 12), -1);

    // A 15-tile view (5x3, the real panel) still fits the queue: the middle of the middle row is first.
    om::map::ViewTiles big;
    big.x0    = 100;
    big.y0    = 200;
    big.cols  = 5;
    big.rows  = 3;
    big.count = 15;
    om::map::TileCache empty;
    empty.reset();
    CHECK_EQ(om::map::planTiles(big, 16, empty, req, om::map::kMaxRequests), 15);
    CHECK_EQ(req[0].x, 102);
    CHECK_EQ(req[0].y, 201);
}

}  // namespace


int main() {
    test_decode_entry();
    test_zoom_to_pack();
    test_binary_search();
    test_coordinates();
    test_view_tiles();
    test_pan_and_zoom();
    test_tile_cache();
    test_cache_pending_is_untouchable();
    test_plan_tiles_order();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
