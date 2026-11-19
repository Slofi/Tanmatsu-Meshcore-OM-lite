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
    uint32_t rec_reads;      // S514: how many one-record reads the search issued
    uint32_t block_reads;    // S514: how many whole-block reads it issued
};

uint8_t* g_buf = nullptr;

FakeIdx g_idx;

bool readRec(void* ctx, uint32_t i, om::map::Entry* out) {
    FakeIdx* f = static_cast<FakeIdx*>(ctx);
    ++f->rec_reads;
    if (i == f->fail_at) return false;
    if (i >= f->count) return false;
    return om::map::decodeEntry(f->bytes + (size_t)i * om::map::kIdxRecordBytes, out);
}

// S514: the block search's one-read-per-block callback.
uint32_t readBlock(void* ctx, uint32_t first, om::map::Entry* out, uint32_t cap) {
    FakeIdx* f = static_cast<FakeIdx*>(ctx);
    ++f->block_reads;
    if (first >= f->count || out == nullptr || cap == 0) return 0;
    uint32_t n = f->count - first;
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; i++) {
        if (!om::map::decodeEntry(f->bytes + (size_t)(first + i) * om::map::kIdxRecordBytes, &out[i])) return 0;
    }
    return n;
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
    g_idx.rec_reads   = 0;
    g_idx.block_reads = 0;
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

void test_world_pixel() {
    // S513 (Claude): Ljubljana, Presernov trg, 46.0514 N 14.5060 E. Expected values computed independently in
    // Python with the same slippy formula (math.log/tan/cos), not copied from this code's output.
    CHECK_EQ((long long)om::map::worldPxXForLon(14.5060, 16), 9064636LL);
    CHECK_EQ((long long)om::map::worldPxYForLat(46.0514, 16), 5965242LL);
    CHECK_EQ((long long)om::map::worldPxXForLon(14.5060, 9), 70817LL);
    CHECK_EQ((long long)om::map::worldPxYForLat(46.0514, 9), 46603LL);
    // A world pixel lies inside the tile the tile functions name (the two cannot drift apart).
    CHECK_EQ((long long)(om::map::worldPxXForLon(14.5060, 16) / 256), (long long)om::map::tileXForLon(14.5060, 16));
    CHECK_EQ((long long)(om::map::worldPxYForLat(46.0514, 16) / 256), (long long)om::map::tileYForLat(46.0514, 16));
    // Out of range clamps.
    CHECK_EQ((long long)om::map::worldPxXForLon(-400.0, 3), 0LL);
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

// S514/EST-443 item 2: the block search must give the SAME answer as findTile, and once its block-key cache is warm
// it must cost <= 2 card reads per tile for a view (the ticket's number). The callbacks count reads exactly as the
// device does: one for a block key, one for a whole block.
void test_block_search() {
    const uint32_t count = 5000;   // 200 columns of 25 tiles, 30 blocks (last one partial)
    buildIndex(count, 16, 25);

    const uint32_t blocks = om::map::blockCount(count);
    CHECK_EQ(blocks, (count + om::map::kBlockRecords - 1) / om::map::kBlockRecords);
    CHECK_EQ(om::map::blockCount(0), 0);
    CHECK_EQ(om::map::blockCount(1), 1);
    CHECK_EQ(om::map::blockCount(om::map::kBlockRecords), 1);
    CHECK_EQ(om::map::blockCount(om::map::kBlockRecords + 1), 2);

    om::map::Entry* key    = new om::map::Entry[blocks];
    uint8_t*        key_ok = new uint8_t[blocks]();
    om::map::Entry* lru    = new om::map::Entry[om::map::kBlockLruSlots * om::map::kBlockRecords];
    uint32_t*       lru_id = new uint32_t[om::map::kBlockLruSlots]();
    uint32_t*       lru_n  = new uint32_t[om::map::kBlockLruSlots]();
    uint8_t*        lru_ok = new uint8_t[om::map::kBlockLruSlots]();
    om::map::BlockCache bc;
    bc.key         = key;
    bc.key_valid   = key_ok;
    bc.key_cap     = blocks;
    bc.block       = lru;
    bc.block_id    = lru_id;
    bc.block_got   = lru_n;
    bc.block_valid = lru_ok;
    bc.lru_slots   = om::map::kBlockLruSlots;
    bc.next        = 0;

    // Every record, and keys that are not in the index: the two searches must never disagree.
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t x = 1000 + i / 25;
        const uint32_t y = 2000 + i % 25;
        CHECK_EQ(om::map::findTile(count, readRec, &g_idx, 16, x, y), i);
        CHECK_EQ(om::map::findTileBlocks(count, readRec, readBlock, &g_idx, bc, 16, x, y), i);
    }
    CHECK_EQ(om::map::findTileBlocks(count, readRec, readBlock, &g_idx, bc, 16, 1000, 2099), -1);
    CHECK_EQ(om::map::findTileBlocks(count, readRec, readBlock, &g_idx, bc, 16, 1199, 2025), -1);
    CHECK_EQ(om::map::findTileBlocks(count, readRec, readBlock, &g_idx, bc, 15, 1000, 2000), -1);

    // The last block is partial: its last record is still found, and one past it is not.
    CHECK_EQ(om::map::findTileBlocks(count, readRec, readBlock, &g_idx, bc, 16, 1199, 2024), (int)(count - 1));

    // A 12-tile view (5 columns x 3 rows), the shape a zoom fills. Plain binary search first, for the baseline.
    g_idx.rec_reads = g_idx.block_reads = 0;
    int got_plain = 0;
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 5; col++) {
            got_plain += om::map::findTile(count, readRec, &g_idx, 16, 1010 + col, 2010 + row) >= 0 ? 1 : 0;
        }
    }
    const uint32_t plain_reads = g_idx.rec_reads + g_idx.block_reads;

    // Cold block search: forget every cached key and block, so this is the first fill after opening the pack.
    memset(key_ok, 0, blocks);
    memset(lru_ok, 0, om::map::kBlockLruSlots);
    bc.next = 0;
    g_idx.rec_reads = g_idx.block_reads = 0;
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 5; col++) {
            CHECK(om::map::findTileBlocks(count, readRec, readBlock, &g_idx, bc, 16, 1010 + col, 2010 + row) >= 0);
        }
    }
    const uint32_t cold_reads = g_idx.rec_reads + g_idx.block_reads;

    // Warm: the same view again. The ticket's budget: <= 2 card reads per tile.
    g_idx.rec_reads = g_idx.block_reads = 0;
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 5; col++) {
            CHECK(om::map::findTileBlocks(count, readRec, readBlock, &g_idx, bc, 16, 1010 + col, 2010 + row) >= 0);
        }
    }
    const uint32_t warm_reads = g_idx.rec_reads + g_idx.block_reads;

    CHECK_EQ(got_plain, 15);
    printf("block search: 15-tile view, plain %u reads, cold %u, warm %u (%.1f/tile)\n", plain_reads, cold_reads,
           warm_reads, (double)warm_reads / 15.0);
    CHECK(plain_reads > 0);
    CHECK(cold_reads < plain_reads);          // the block tree is already an improvement on the first fill
    CHECK(warm_reads <= 2 * 15);              // and <= 2 reads per tile once warm (15 tiles here)

    // A cache-less call is still correct (falls back to record-by-record inside one block).
    om::map::BlockCache empty;
    g_idx.fail_at = 9999;
    CHECK_EQ(om::map::findTileBlocks(count, readRec, nullptr, &g_idx, empty, 16, 1020, 2010), 510);
    CHECK_EQ(om::map::findTileBlocks(count, readRec, nullptr, &g_idx, empty, 16, 1199, 2024), (int)(count - 1));

    // A failed block-key read is a failure, not a false hit. Forget the cached keys first: a hit would not read.
    memset(key_ok, 0, blocks);
    g_idx.fail_at = 0;
    CHECK_EQ(om::map::findTileBlocks(count, readRec, readBlock, &g_idx, bc, 16, 1010, 2010), -1);
    g_idx.fail_at = 9999;

    delete[] key;
    delete[] key_ok;
    delete[] lru;
    delete[] lru_id;
    delete[] lru_n;
    delete[] lru_ok;
    delete[] g_buf;
    g_buf = nullptr;
}

// S514/EST-443: the stand-in geometry. It is pure and it has two ways to be wrong on the glass: the wrong quarter
// of the parent (a tile that jumps sideways at a zoom) and children past the world edge at z9/z17.
void test_placeholders() {
    om::map::Placeholder p[om::map::kPlaceholderMax];

    // Zooming IN: tile (z16, 2/3) comes from the BOTTOM-RIGHT quarter of parent (z15, 1/1).
    CHECK_EQ(om::map::parentPlaceholder(16, 2, 3, p), 1);
    CHECK_EQ(p[0].z, 15);
    CHECK_EQ(p[0].x, 1);
    CHECK_EQ(p[0].y, 1);
    CHECK_EQ(p[0].sx, 0);
    CHECK_EQ(p[0].sy, 128);
    CHECK_EQ(p[0].sw, 128);
    CHECK_EQ(p[0].sh, 128);
    CHECK_EQ(p[0].dx, 0);
    CHECK_EQ(p[0].dy, 0);
    CHECK_EQ(p[0].dw, 256);   // upscaled 2x into the whole target tile
    CHECK_EQ(p[0].dh, 256);

    // The other diagonal: (3,2) is the TOP-RIGHT quarter.
    CHECK_EQ(om::map::parentPlaceholder(16, 3, 2, p), 1);
    CHECK_EQ(p[0].z, 15);
    CHECK_EQ(p[0].x, 1);
    CHECK_EQ(p[0].y, 1);
    CHECK_EQ(p[0].sx, 128);
    CHECK_EQ(p[0].sy, 0);

    // z9 is the lowest pack zoom (no z8 records), and a null out pointer is refused, not written.
    CHECK_EQ(om::map::parentPlaceholder(9, 0, 0, p), 0);
    CHECK_EQ(om::map::parentPlaceholder(16, 0, 0, nullptr), 0);

    // Zooming OUT: tile (z16, 2/3) is the four z17 children, row-major, each whole tile into its 1/2 quadrant.
    CHECK_EQ(om::map::childPlaceholder(16, 2, 3, p, om::map::kPlaceholderMax), 4);
    static const uint32_t cx[4] = {4, 5, 4, 5};
    static const uint32_t cy[4] = {6, 6, 7, 7};
    static const int      qx[4] = {0, 128, 0, 128};
    static const int      qy[4] = {0, 0, 128, 128};
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(p[i].z, 17);
        CHECK_EQ(p[i].x, cx[i]);
        CHECK_EQ(p[i].y, cy[i]);
        CHECK_EQ(p[i].sx, 0);
        CHECK_EQ(p[i].sy, 0);
        CHECK_EQ(p[i].sw, 256);   // the whole child, downscaled 1/2
        CHECK_EQ(p[i].sh, 256);
        CHECK_EQ(p[i].dx, qx[i]);
        CHECK_EQ(p[i].dy, qy[i]);
        CHECK_EQ(p[i].dw, 128);
        CHECK_EQ(p[i].dh, 128);
    }

    // z17 is the highest zoom (no z18 records); a small cap returns a prefix, not a write past the array.
    CHECK_EQ(om::map::childPlaceholder(17, 0, 0, p, om::map::kPlaceholderMax), 0);
    CHECK_EQ(om::map::childPlaceholder(16, 2, 3, p, 2), 2);
    CHECK_EQ(om::map::childPlaceholder(16, 2, 3, p, 0), 0);
    CHECK_EQ(om::map::childPlaceholder(16, 2, 3, nullptr, 4), 0);

    // The world edge: the last tile of z9 has the last tile of z10 as its bottom-right child, never 1024.
    CHECK_EQ(om::map::childPlaceholder(9, 511, 511, p, om::map::kPlaceholderMax), 4);
    CHECK_EQ(p[3].x, 1023u);
    CHECK_EQ(p[3].y, 1023u);
}

// The placeholder path uses peek(), not find(): a stand-in must not touch the LRU and evict a tile the fill
// still needs. This is the arm against a "helpful" find() call in drawTileScaled().
void test_peek_does_not_touch_lru() {
    om::map::TileCache c;
    c.reset();
    for (uint32_t i = 0; i < (uint32_t)om::map::kCacheSlots; i++) c.commit(c.take(16, i, 0), true);

    CHECK_EQ(c.peek(16, 0, 0), 0);
    CHECK_EQ(c.peek(16, 5, 0), 5);
    CHECK_EQ(c.peek(16, 99, 0), -1);

    // Slot 0 is still the least recently used, because peek() did not stamp it; take() must choose it.
    const int victim = c.take(16, 99, 0);
    CHECK_EQ(victim, 0);
    c.commit(victim, true);
    CHECK_EQ(c.peek(16, 0, 0), -1);
    CHECK_EQ(c.peek(16, 1, 0), 1);   // untouched, still there

    // A Pending slot is not peekable (its pixels are being written), a Ready one is.
    const int pending = c.take(17, 3, 3);
    CHECK_EQ(c.peek(17, 3, 3), -1);
    c.commit(pending, true);
    CHECK(c.peek(17, 3, 3) >= 0);
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

// S513: the z17 pack is 3,590,722,268 bytes, i.e. past the 2 GiB mark where a SIGNED 32-bit offset turns negative.
// The device reads that pack through FatFs (FSIZE_t = a 32-bit UNSIGNED DWORD). This is the arm against the old
// behaviour: an offset above 2^31-1 must survive Entry -> read-call as an unsigned 32-bit value, with no trip
// through `long`, and a range that cannot exist on FAT32 must be refused instead of wrapped.
void test_read_range_above_2gib() {
    // The exact record the device failed on: Maribor z17, 71232/46338, 2493062600 bytes into the pack.
    const uint32_t want_offset = 2493062600u;
    const uint32_t want_len    = 26'000u;

    uint8_t rec[om::map::kIdxRecordBytes] = {0};
    rec[0] = 17;
    for (int i = 0; i < 4; i++) {
        rec[4 + i]  = (uint8_t)((71232u >> (8 * i)) & 0xFF);
        rec[8 + i]  = (uint8_t)((46338u >> (8 * i)) & 0xFF);
        rec[12 + i] = (uint8_t)((want_offset >> (8 * i)) & 0xFF);
        rec[16 + i] = 0;   // high half of the u64 offset: this pack is under 4 GiB, so it must be zero
        rec[20 + i] = (uint8_t)((want_len >> (8 * i)) & 0xFF);
    }

    om::map::Entry e;
    CHECK(om::map::decodeEntry(rec, &e));
    CHECK_EQ(e.offset, 2493062600ull);

    om::map::ReadRange r;
    CHECK(om::map::readRangeFor(e, &r));
    CHECK_EQ(r.offset, want_offset);      // the value the read call is given
    CHECK_EQ(r.length, want_len);
    // It is an UNSIGNED 32-bit value. The device's `long` is 32 bits, so the old path turned this same number
    // negative; the host's `long` is wider, so the cast below is to int32_t — the width the P4 really has.
    CHECK((int32_t)r.offset < 0);   // what the old `(long)e.offset` produced on the P4: 2493062600 -> -1801904696
    CHECK_EQ((int32_t)r.offset, -1801904696);
    CHECK_EQ((uint32_t)r.offset, want_offset);

    // A record whose range ends past 4 GiB - 1 cannot be on FAT32: refused, with no wrapped offset left behind.
    om::map::Entry bad;
    bad.z      = 17;
    bad.offset = 0xFFFFFFFFull;
    bad.length = 2;
    CHECK(!om::map::readRangeFor(bad, &r));
    bad.offset = 0xFFFFFFF0ull;
    bad.length = 0x20;
    CHECK(!om::map::readRangeFor(bad, &r));
    bad.offset = 0xFFFFFFF0ull;   // the last byte at 4 GiB - 1 is still one byte too far by the strict +length rule
    bad.length = 0x10;
    CHECK(!om::map::readRangeFor(bad, &r));
    // A legitimate last tile just under the limit is accepted, with its offset intact.
    bad.offset = 0xFFFF0000ull;
    bad.length = 0x1000;
    CHECK(om::map::readRangeFor(bad, &r));
    CHECK_EQ(r.offset, 4294901760u);
    // A zero-length record is not a read, and a null out-pointer is refused rather than written.
    bad.offset = 1;
    bad.length = 0;
    CHECK(!om::map::readRangeFor(bad, &r));
    CHECK(!om::map::readRangeFor(e, nullptr));
}

}  // namespace


int main() {
    test_decode_entry();
    test_zoom_to_pack();
    test_binary_search();
    test_coordinates();
    test_world_pixel();
    test_view_tiles();
    test_pan_and_zoom();
    test_tile_cache();
    test_cache_pending_is_untouchable();
    test_block_search();
    test_placeholders();
    test_peek_does_not_touch_lru();
    test_plan_tiles_order();
    test_read_range_above_2gib();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
