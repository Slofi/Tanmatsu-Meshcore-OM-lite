// SPDX-License-Identifier: MIT
#include "map_nodes.h"

#include <stdio.h>   // snprintf: the label text

#include "map_pack.h"    // worldPxXForLon / worldPxYForLat: the pack's own slippy maths, reused not copied
#include "map_view.h"    // kMinZoom / kMaxZoom: the zooms the map itself has

namespace om {
namespace map {

bool projectForView(int z, int64_t wx, int64_t wy, int view_w, int view_h, int32_t lat_e6, int32_t lon_e6,
                    int* out_x, int* out_y) {
    if (out_x == nullptr || out_y == nullptr) return false;
    if (z < kMinZoom || z > kMaxZoom) return false;
    if (view_w <= 0 || view_h <= 0) return false;
    if (!positionDrawable(true, lat_e6, lon_e6)) return false;

    // Degrees from micro-degrees. The double conversion happens once, here, so the page never carries a float.
    const double lat = (double)lat_e6 / 1000000.0;
    const double lon = (double)lon_e6 / 1000000.0;

    const int64_t px = worldPxXForLon(lon, z);
    const int64_t py = worldPxYForLat(lat, z);
    const int64_t sx = px - wx;
    const int64_t sy = py - wy;

    // The CULLING rule: the point must be in the view. Half-open, so a node exactly on the right/bottom edge is
    // out (that pixel is the next view's first) and one exactly on the left/top edge is in.
    if (sx < 0 || sy < 0 || sx >= (int64_t)view_w || sy >= (int64_t)view_h) return false;

    *out_x = (int)sx;
    *out_y = (int)sy;
    return true;
}

// ---------------------------------------------------------------------------------------------- labels

namespace {

// The greedy's order: favourite first, then most recently heard, then the lower row index. Written as ONE
// comparator so collectLabelCandidates and any future caller cannot disagree about "first".
bool candidateBetter(const mc::Contact& a, int ai, const mc::Contact& b, int bi) {
    if (a.favourite != b.favourite) return a.favourite;
    if (a.heard_at != b.heard_at) return a.heard_at > b.heard_at;
    return ai < bi;
}

}  // namespace

const char* mapLabelText(char* out, size_t cap, const char* name) {
    if (out == nullptr || cap == 0) return "";
    if (name == nullptr || name[0] == '\0') {
        snprintf(out, cap, "%s", "(no name)");
        return out;
    }
    // Walk the string in UTF-8 steps for at most kLabelMaxChars characters.
    size_t i     = 0;   // bytes consumed from `name`
    int    chars = 0;
    while (name[i] != '\0' && chars < kLabelMaxChars) {
        const unsigned char c    = (unsigned char)name[i];
        size_t              step = 1;
        if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        bool complete = true;
        for (size_t k = 1; k < step; ++k) {
            if (name[i + k] == '\0') {
                complete = false;
                break;
            }
        }
        if (!complete) break;   // a malformed tail: stop BEFORE it rather than copy half a character
        i += step;
        ++chars;
    }
    if (name[i] == '\0') snprintf(out, cap, "%.*s", (int)i, name);
    else snprintf(out, cap, "%.*s\xe2\x80\xa6", (int)i, name);
    return out;
}

int collectLabelCandidates(const mc::ContactStore& s, int z, int64_t wx, int64_t wy, int view_w, int view_h,
                           LabelCandidate* out, int cap) {
    if (out == nullptr || cap <= 0 || s.c == nullptr) return 0;

    int n = 0;
    for (int i = 0; i < (int)s.count; ++i) {
        const mc::Contact& c = s.c[i];
        int                x = 0, y = 0;
        if (!projectForView(z, wx, wy, view_w, view_h, c.lat_e6, c.lon_e6, &x, &y)) continue;
        if (!positionDrawable(c.has_position, c.lat_e6, c.lon_e6)) continue;

        // Where does it belong in the (sorted) array? Same rules as candidateBetter.
        if (n >= cap && !candidateBetter(c, i, s.c[out[cap - 1].node], out[cap - 1].node)) {
            continue;   // worse than everything we can show: drop it, the marker is still drawn
        }
        int at = (n < cap) ? n : cap - 1;
        while (at > 0 && candidateBetter(c, i, s.c[out[at - 1].node], out[at - 1].node)) {
            if (at < cap) out[at] = out[at - 1];
            --at;
        }
        out[at].node = i;
        out[at].x    = x;
        out[at].y    = y;
        if (n < cap) ++n;
    }
    return n;
}

int placeLabels(const mc::ContactStore& s, const LabelCandidate* cand, int cand_count, int view_w, int view_h,
                LabelMeasure measure, void* ctx, MapLabel* out, int cap) {
    if (cand == nullptr || out == nullptr || cap <= 0) return 0;

    int placed = 0;
    for (int i = 0; i < cand_count && placed < cap; ++i) {
        const LabelCandidate& k = cand[i];
        if (k.node < 0 || k.node >= (int)s.count) continue;

        MapLabel lab;
        lab.node = k.node;
        mapLabelText(lab.text, sizeof(lab.text), s.c[k.node].name);

        const int text_w = (measure != nullptr) ? measure(ctx, lab.text) : 0;
        if (text_w <= 0) continue;   // nothing to draw: never a zero-width plate

        lab.box.x0 = k.x + kLabelGap;
        lab.box.y0 = k.y - kLabelH / 2;
        lab.box.x1 = lab.box.x0 + text_w + 2 * kLabelPadX;
        lab.box.y1 = lab.box.y0 + kLabelH;

        // A plate that would leave the view is not offered: there is no clipping rectangle on this page, and a name
        // cut by the edge reads as a drawing fault rather than as "the node is a little further right".
        if (lab.box.x0 < 0 || lab.box.y0 < 0) continue;
        if (lab.box.x1 > view_w || lab.box.y1 > view_h) continue;

        bool clash = false;
        for (int p = 0; p < placed; ++p) {
            if (lab.box.overlaps(out[p].box)) {
                clash = true;
                break;
            }
        }
        if (clash) continue;   // the marker stays; only the name is skipped

        out[placed++] = lab;
    }
    return placed;
}

// ---------------------------------------------------------------------------------------------- the menu's Map

bool centreOnNode(int32_t lat_e6, int32_t lon_e6, int zoom, int view_w, int view_h, int* out_z, int64_t* out_wx,
                  int64_t* out_wy) {
    if (out_z == nullptr || out_wx == nullptr || out_wy == nullptr) return false;
    if (view_w <= 0 || view_h <= 0) return false;
    if (!positionDrawable(true, lat_e6, lon_e6)) return false;

    // The zooms the map itself has; 15 is inside them and lives in the "slo" pack (z9-15).
    if (zoom < kMinZoom) zoom = kMinZoom;
    if (zoom > kMaxZoom) zoom = kMaxZoom;

    const double lat = (double)lat_e6 / 1000000.0;
    const double lon = (double)lon_e6 / 1000000.0;

    *out_z  = zoom;
    *out_wx = worldPxXForLon(lon, zoom) - view_w / 2;
    *out_wy = worldPxYForLat(lat, zoom) - view_h / 2;
    clampWorldPixel(zoom, out_wx, out_wy, view_w, view_h);
    return true;
}

}  // namespace map
}  // namespace om
