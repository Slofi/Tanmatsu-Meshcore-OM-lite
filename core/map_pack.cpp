// SPDX-License-Identifier: MIT
#include "map_pack.h"

#include <math.h>
#include <string.h>

namespace om {
namespace map {

namespace {

uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint64_t get64(const uint8_t* p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

// (z, x, y) ordering, the order tiles_export.py writes in.
int compare(const Entry& a, uint8_t z, uint32_t x, uint32_t y) {
    if (a.z != z) return (a.z < z) ? -1 : 1;
    if (a.x != x) return (a.x < x) ? -1 : 1;
    if (a.y != y) return (a.y < y) ? -1 : 1;
    return 0;
}

}  // namespace

bool decodeEntry(const uint8_t* rec, Entry* out) {
    if (rec == nullptr || out == nullptr) return false;
    out->z      = rec[0];
    out->x      = get32(rec + 4);
    out->y      = get32(rec + 8);
    out->offset = get64(rec + 12);
    out->length = get32(rec + 20);
    // A zoom outside 0..24 or a zero-length tile means the record is not one: better a missing tile than a
    // seek into nonsense.
    if (out->z > 24) return false;
    if (out->length == 0) return false;
    return true;
}

const char* packDirForZoom(int z) {
    // The base pack starts at z9 (the packer's own minimum); below that there is no tile at all, so saying
    // "slo" would only turn a missing zoom into a failed search on the card.
    if (z >= 9 && z <= 15) return "slo";
    if (z == 16) return "slo16";
    if (z >= 17 && z <= 18) return "slo17";   // 18 is 17 magnified, like the L2 does it
    return nullptr;
}

bool readRangeFor(const Entry& e, ReadRange* out) {
    if (out == nullptr) return false;
    if (e.length == 0) return false;   // decodeEntry already refuses this; a read of nothing is not a request
    // FAT32's largest file byte is 4 GiB - 1, so a range that ends past it cannot be a real tile: refuse it here
    // rather than let it wrap to a small offset and read someone else's bytes.
    if (e.offset + (uint64_t)e.length > 0xFFFFFFFFull) return false;
    out->offset = (uint32_t)e.offset;   // the cast is safe: the check above bounds it by 4 GiB - 1
    out->length = e.length;
    return true;
}

int findTile(uint32_t count, RecordReader read, void* ctx, uint8_t z, uint32_t x, uint32_t y) {
    if (read == nullptr || count == 0) return -1;
    uint32_t lo = 0;
    uint32_t hi = count - 1;
    while (lo <= hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        Entry          e;
        if (!read(ctx, mid, &e)) return -1;   // a short read is a failure, not "not found"
        const int c = compare(e, z, x, y);
        if (c == 0) return (int)mid;
        if (c < 0) {
            lo = mid + 1;
        } else {
            if (mid == 0) break;   // guard the unsigned wrap
            hi = mid - 1;
        }
    }
    return -1;
}

// ----------------------------------------------------------------------------------------------- block search (S514)

uint32_t blockCount(uint32_t count) { return (count + kBlockRecords - 1) / kBlockRecords; }

namespace {

// The first record of block `b`, from the PSRAM cache when it has been seen before, else one card read that also
// fills the cache. A null/mis-sized cache is not an error: it just means the read is repeated next time.
bool blockKey(uint32_t b, uint32_t count, RecordReader read, void* ctx, BlockCache& cache, Entry* out) {
    if (b < cache.key_cap && cache.key != nullptr && cache.key_valid != nullptr && cache.key_valid[b]) {
        *out = cache.key[b];
        return true;
    }
    const uint32_t rec = b * kBlockRecords;
    if (rec >= count || !read(ctx, rec, out)) return false;
    if (b < cache.key_cap && cache.key != nullptr && cache.key_valid != nullptr) {
        cache.key[b]       = *out;
        cache.key_valid[b] = 1;
    }
    return true;
}

// Binary search inside [first, last] using the one-record reader (the no-cache fallback).
int findInRange(uint32_t first, uint32_t last, RecordReader read, void* ctx, uint8_t z, uint32_t x, uint32_t y) {
    uint32_t lo = first;
    uint32_t hi = last;
    while (lo <= hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        Entry          e;
        if (!read(ctx, mid, &e)) return -1;
        const int c = compare(e, z, x, y);
        if (c == 0) return (int)mid;
        if (c < 0) {
            lo = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }
    return -1;
}

// Binary search inside one cached block. `first` is the block's global record number.
int findInBlock(const Entry* recs, uint32_t n, uint32_t first, uint8_t z, uint32_t x, uint32_t y) {
    uint32_t lo = 0;
    uint32_t hi = n;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const int      c   = compare(recs[mid], z, x, y);
        if (c == 0) return (int)(first + mid);
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return -1;
}

}  // namespace

int findTileBlocks(uint32_t count, RecordReader read, BlockReader blockRead, void* ctx, BlockCache& cache, uint8_t z,
                   uint32_t x, uint32_t y) {
    if (read == nullptr || count == 0) return -1;
    const uint32_t blocks = blockCount(count);

    // 1. The last block whose first record is <= the target. The top of this tree is the same for every tile, so
    // this is where the PSRAM key cache turns the expensive part into zero card reads.
    uint32_t lo    = 0;
    uint32_t hi    = blocks - 1;
    uint32_t best  = 0;
    bool     found = false;
    while (lo <= hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        Entry          k;
        if (!blockKey(mid, count, read, ctx, cache, &k)) return -1;
        const int c = compare(k, z, x, y);
        if (c <= 0) {
            best  = mid;
            found = true;
            lo    = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }
    if (!found) return -1;   // smaller than the first record: not in the pack

    const uint32_t first = best * kBlockRecords;
    const uint32_t want  = (count - first) < kBlockRecords ? (count - first) : kBlockRecords;

    // 2. One whole-block read, from the LRU when the block is already there.
    if (blockRead != nullptr && cache.block != nullptr && cache.block_id != nullptr && cache.block_valid != nullptr &&
        cache.block_got != nullptr && cache.lru_slots > 0) {
        int slot = -1;
        for (int i = 0; i < cache.lru_slots; i++) {
            if (cache.block_valid[i] && cache.block_id[i] == best) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            slot       = cache.next;
            cache.next = (cache.next + 1) % cache.lru_slots;
            Entry* dst = cache.block + (size_t)slot * kBlockRecords;
            const uint32_t got = blockRead(ctx, first, dst, want);
            if (got == 0 || got > want) return -1;
            cache.block_valid[slot] = 1;
            cache.block_id[slot]    = best;
            cache.block_got[slot]   = got;
            return findInBlock(dst, got, first, z, x, y);
        }
        const uint32_t got = cache.block_got[slot] > want ? want : cache.block_got[slot];
        return findInBlock(cache.block + (size_t)slot * kBlockRecords, got, first, z, x, y);
    }

    // No block storage: still correct, just one record read per binary-search step inside the block.
    return findInRange(first, first + want - 1, read, ctx, z, x, y);
}

uint32_t tileXForLon(double lon, int z) {
    const double n = (double)(1u << z);
    double       v = (lon + 180.0) / 360.0 * n;
    if (v < 0) v = 0;
    if (v >= n) v = n - 1;
    return (uint32_t)v;
}

int64_t worldPxXForLon(double lon, int z) {
    const double n = (double)(1u << z) * 256.0;
    double       v = (lon + 180.0) / 360.0 * n;
    if (v < 0) v = 0;
    if (v >= n) v = n - 1;
    return (int64_t)v;
}

int64_t worldPxYForLat(double lat, int z) {
    const double n   = (double)(1u << z) * 256.0;
    const double rad = lat * 3.14159265358979323846 / 180.0;
    double       v   = (1.0 - log(tan(rad) + 1.0 / cos(rad)) / 3.14159265358979323846) / 2.0 * n;
    if (v < 0) v = 0;
    if (v >= n) v = n - 1;
    return (int64_t)v;
}

uint32_t tileYForLat(double lat, int z) {
    const double n     = (double)(1u << z);
    const double rad   = lat * 3.14159265358979323846 / 180.0;
    double       v     = (1.0 - log(tan(rad) + 1.0 / cos(rad)) / 3.14159265358979323846) / 2.0 * n;
    if (v < 0) v = 0;
    if (v >= n) v = n - 1;
    return (uint32_t)v;
}

double lonForTileX(uint32_t x, int z) {
    const double n = (double)(1u << z);
    return ((double)x + 0.5) / n * 360.0 - 180.0;
}

double latForTileY(uint32_t y, int z) {
    const double n   = (double)(1u << z);
    const double v   = 3.14159265358979323846 * (1.0 - 2.0 * ((double)y + 0.5) / n);
    return atan(sinh(v)) * 180.0 / 3.14159265358979323846;
}

}  // namespace map
}  // namespace om
