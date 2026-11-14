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

uint32_t tileXForLon(double lon, int z) {
    const double n = (double)(1u << z);
    double       v = (lon + 180.0) / 360.0 * n;
    if (v < 0) v = 0;
    if (v >= n) v = n - 1;
    return (uint32_t)v;
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
