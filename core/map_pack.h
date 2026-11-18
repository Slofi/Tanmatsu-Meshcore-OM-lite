// SPDX-License-Identifier: MIT
// The map pack reader's RULES: what a tiles.idx record is, how to find a tile in it, which pack serves which
// zoom, and which tile covers a coordinate.
//
// The format is the L2's (tools/tiles_export.py --pack): `tiles.pack` is the tile bytes concatenated in z,x,y
// order and `tiles.idx` is one 24-byte record per tile: z u8, 3 pad, x u32, y u32, offset u64, length u32, all
// little-endian. Because the index is sorted, a tile is found by binary search instead of a scan.
//
// Everything here is arithmetic and I/O-free on purpose: the device reads records through a callback (from a
// file it keeps open), the host tests read them out of an array. That keeps the search testable without a card.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace map {

constexpr size_t kIdxRecordBytes = 24;

struct Entry {
    uint8_t  z = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint64_t offset = 0;
    uint32_t length = 0;
};

// Parse one 24-byte record. Returns false when the bytes are not a plausible record (a truncated read, a
// zero-length tile, or a zoom far outside anything the packer writes).
bool decodeEntry(const uint8_t* rec, Entry* out);

// Read record `i` of an index. `ctx` is the caller's (a FILE*, an array); returns false on a short read.
using RecordReader = bool (*)(void* ctx, uint32_t i, Entry* out);

// How a tile read is issued: offset and length in the width the device's read call takes them — FatFs's FSIZE_t,
// a 32-bit UNSIGNED DWORD while FF_FS_EXFAT is 0, so it reaches 4 GiB - 1.
//
// ⚠️ S513, measured on the P4: picolibc's off_t is a SIGNED 32-bit long, so fseek() cannot pass an offset above
// 2^31-1. The slo17 pack is 3,590,722,268 bytes and its index is sorted by column, so every z17 tile east of about
// 15.35 E (~38.6 % of the records) failed in fseek and landed in the failed ring as a silent hole: Maribor, Ptuj,
// Murska Sobota, Krsko drew as void. That pack is read through FatFs now, and this function is the rule that keeps
// the offset unsigned the whole way: an Entry (uint64) becomes the two uint32 values a read call takes, and a
// record that cannot exist on FAT32 (offset + length past 4 GiB - 1) is REFUSED, not wrapped into a small one.
struct ReadRange {
    uint32_t offset = 0;
    uint32_t length = 0;
};
bool readRangeFor(const Entry& e, ReadRange* out);

// The pack layout the L2 already uses (firmware/src/map.cpp): z9-15 in "slo", z16 in "slo16", z17 in "slo17".
// Returns nullptr for a zoom no pack covers.
const char* packDirForZoom(int z);

// Binary search for (z, x, y). Returns the record index, or -1 when the tile is not in the pack. The reader is
// called O(log2 count) times, which is what makes a 900,000-tile index usable straight from the card.
int findTile(uint32_t count, RecordReader read, void* ctx, uint8_t z, uint32_t x, uint32_t y);

// Slippy-map tile coordinates for a coordinate, in degrees (lon positive east, lat positive north).
uint32_t tileXForLon(double lon, int z);
uint32_t tileYForLat(double lat, int z);
// The inverse, for the centre of tile (x, y): the coordinate the tile sits on.
double lonForTileX(uint32_t x, int z);
double latForTileY(uint32_t y, int z);

}  // namespace map
}  // namespace om
