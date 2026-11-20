// SPDX-License-Identifier: MIT
#include "map_page.h"

#include <stdio.h>
#include <string.h>

#include "chrome.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "launcher_page.h"
#include "map_link.h"
#include "mc_link.h"      // StoreLock: the node table is written by the radio task
#include "map_nodes.h"    // S515/EST-466: projection, culling, the label greedy
#include "map_view.h"
#include "nodes_page.h"   // drawTextClipped, nodeTypeColour
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"

namespace om {
namespace ui {
namespace {

// Dark grey for the sea/void: the tiles are opaque, so this only shows where the pack has no data (outside
// Slovenia's bounds) and where a tile has not been decoded yet.
constexpr uint16_t kMapVoid = rgb565(0x1A1A18);

// Place one decoded tile into the framebuffer, clipped to the map area. Direct writes: the panel is blitted once
// after the whole page is composed, so there is no flicker to hide.
void blitTile(Screen& s, const uint16_t* px, int tw, int th, int dx, int dy) {
    if (px == nullptr || tw <= 0 || th <= 0) return;
    uint16_t* fb = static_cast<uint16_t*>(s.fb());
    if (fb == nullptr) return;
    const int stride = s.width();
    for (int ty = 0; ty < th; ty++) {
        const int y = dy + ty;
        if (y < kMapAreaY || y >= kMapAreaY + kMapAreaH) continue;
        const int x0 = dx < kMapAreaX ? kMapAreaX : dx;
        const int x1 = dx + tw;
        const int xe = x1 > kMapAreaX + kMapAreaW ? kMapAreaX + kMapAreaW : x1;
        if (xe <= x0) continue;
        uint16_t*       dst = fb + (size_t)y * (size_t)stride + (size_t)x0;
        const uint16_t* src = px + (size_t)ty * (size_t)tw + (size_t)(x0 - dx);
        memcpy(dst, src, sizeof(uint16_t) * (size_t)(xe - x0));
    }
}

// The callback form map_link wants: it holds the cache lock across the copy, so a tile cannot be evicted while it
// is being read, and the page never holds a pointer to somebody else's buffer.
void blitCallback(const uint16_t* px, int tw, int th, int dx, int dy, void* ctx) {
    blitTile(*static_cast<Screen*>(ctx), px, tw, th, dx, dy);
}

// S514: a placeholder is a REGION of a decoded tile scaled into a destination rectangle. NEAREST NEIGHBOUR on
// purpose: one source pixel per destination pixel, so a 2x upscale repeats each pixel and a 1/2 downscale takes
// the top-left of each 2x2 block — no per-channel arithmetic while the real tile is still decoding.
void blitTileScaled(const uint16_t* px, int tw, int th, int sx, int sy, int sw, int sh, int dx, int dy, int dw,
                    int dh, void* ctx) {
    if (px == nullptr || tw <= 0 || th <= 0 || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    Screen& s = *static_cast<Screen*>(ctx);
    uint16_t* fb = static_cast<uint16_t*>(s.fb());
    if (fb == nullptr) return;
    if (sx < 0 || sy < 0 || sx + sw > tw || sy + sh > th) return;
    if (dw > om::map::kTilePx || dh > om::map::kTilePx) return;   // the src_x table below is one tile wide

    const int y0 = dy < kMapAreaY ? kMapAreaY : dy;
    const int y1 = dy + dh > kMapAreaY + kMapAreaH ? kMapAreaY + kMapAreaH : dy + dh;
    const int x0 = dx < kMapAreaX ? kMapAreaX : dx;
    const int x1 = dx + dw > kMapAreaX + kMapAreaW ? kMapAreaX + kMapAreaW : dx + dw;
    if (x1 <= x0 || y1 <= y0) return;

    // One division per SOURCE column, not per pixel.
    int src_x[om::map::kTilePx];
    const int cols = dw <= om::map::kTilePx ? dw : om::map::kTilePx;
    for (int i = 0; i < cols; i++) src_x[i] = sx + (i * sw) / dw;

    const int stride = s.width();
    for (int y = y0; y < y1; y++) {
        const int row = y - dy;
        const int syy = sy + (row * sh) / dh;
        if (syy >= th) continue;
        const uint16_t* src = px + (size_t)syy * (size_t)tw;
        uint16_t*       dst = fb + (size_t)y * (size_t)stride;
        for (int x = x0; x < x1; x++) dst[x] = src[src_x[x - dx]];
    }
}

// Draw the scaled stand-ins for one Pending/Empty target tile. Zooming out the four children fill their
// quadrants (a missing child leaves its quadrant as it is); otherwise the parent fills the whole square.
bool drawPlaceholders(Screen& s, uint8_t z, uint32_t tx, uint32_t ty, int dx, int dy, om::map::ZoomDir dir) {
    om::map::Placeholder ph[om::map::kPlaceholderMax];
    const int n = (dir == om::map::ZoomDir::Out)
                      ? om::map::childPlaceholder(z, tx, ty, ph, om::map::kPlaceholderMax)
                      : om::map::parentPlaceholder(z, tx, ty, ph);
    bool any = false;
    for (int i = 0; i < n; i++) {
        if (om::maplink::drawTileScaled(ph[i].z, ph[i].x, ph[i].y, ph[i].sx, ph[i].sy, ph[i].sw, ph[i].sh,
                                        dx + ph[i].dx, dy + ph[i].dy, ph[i].dw, ph[i].dh, &blitTileScaled, &s)) {
            any = true;
        }
    }
    return any;
}

// The zoom of the previous compose, so the page knows which stand-in to ask for. -1 = no previous draw.
int g_prev_z = -1;

// ---------------------------------------------------------------------------------- S515/EST-466: the nodes
// A marker is a disc of about 8 px (radius 4) in the Nodes list's own type colour, over a disc one pixel larger in
// the near-black of the page, so the dot reads on a pale road AND on dark forest without a blend. A favourite gets
// a gold ring; the node the node menu centred on gets a bigger, thicker gold ring until the view moves.
constexpr int kMarkerR       = 4;
constexpr int kFavRingR      = 6;
constexpr int kHighlightRingR = 10;
constexpr int kHighlightRingW = 2;

// The label plate's ink: the same near-black the void tiles use, so a name over a bright map reads as a plate and
// not as another place name printed by the map itself.
constexpr uint16_t kLabelPlate = kMapVoid;

// How many labels may be on screen at once, and how many candidates are ranked for them. A bound, not a policy:
// the greedy stops as soon as the plates start to collide, and 24 names already cover most of the map area.
constexpr int kLabelCap = 32;

// The rate limit on the one cost line. A pan repaints on every key press, so a line per compose would be a line
// per 100 ms; once every 3 s is enough to read the cost without the log drowning in itself.
constexpr uint32_t kMarkerLogMs = 3000;

om::map::LabelCandidate g_label_cand[kLabelCap];
om::map::MapLabel       g_labels[kLabelCap];

// The label width, from the face the plate is drawn with. Passed as a callback so the collision test is the same
// function on the host (a fake font) and on the glass.
int measureLabel(void* ctx, const char* utf8) {
    const Font* f = static_cast<const Font*>(ctx);
    return (f != nullptr && f->ready()) ? f->textWidth(utf8) : 0;
}

// Every positioned node in the view gets a marker (radio AND archive: one table), then the names that fit.
// Called AFTER the tile loop, so a marker is drawn over a real tile, over a stand-in and over the void alike.
void drawMapNodes(Screen& s, const MapView& v) {
    if (v.store == nullptr || v.store->c == nullptr) return;

    const int64_t t0  = esp_timer_get_time();
    const Font*   lf  = fontFor(Face::Hint);
    int           markers = 0;
    int           labels  = 0;

    for (int i = 0; i < (int)v.store->count; ++i) {
        const mc::Contact& c = v.store->c[i];
        if (!om::map::positionDrawable(c.has_position, c.lat_e6, c.lon_e6)) continue;
        int x = 0, y = 0;
        if (!om::map::projectForView(v.z, v.wx, v.wy, kMapAreaW, kMapAreaH, c.lat_e6, c.lon_e6, &x, &y)) continue;

        ++markers;
        const int px = kMapAreaX + x;
        const int py = kMapAreaY + y;
        s.disc(px, py, kMarkerR + 1, (uint16_t)kBg);   // the 1 px dark outline
        s.disc(px, py, kMarkerR, nodeTypeColour(c.type));
        if (c.favourite) s.ring(px, py, kFavRingR, 1, kAcc);
        if (v.highlight_pub != nullptr && memcmp(c.pub, v.highlight_pub, 32) == 0)
            s.ring(px, py, kHighlightRingR, kHighlightRingW, kAcc);
    }

    if (v.z >= om::map::kLabelMinZoom) {
        const int nc = om::map::collectLabelCandidates(*v.store, v.z, v.wx, v.wy, kMapAreaW, kMapAreaH, g_label_cand,
                                                       kLabelCap);
        labels = om::map::placeLabels(*v.store, g_label_cand, nc, kMapAreaW, kMapAreaH, &measureLabel,
                                      const_cast<Font*>(lf), g_labels, kLabelCap);
        for (int i = 0; i < labels; ++i) {
            const om::map::LabelRect& b = g_labels[i].box;
            s.fillRect(kMapAreaX + b.x0, kMapAreaY + b.y0, b.x1 - b.x0, b.y1 - b.y0, kLabelPlate);
            drawText(s, Face::Hint, kMapAreaX + b.x0 + om::map::kLabelPadX,
                     textBaseline(s, Face::Hint, kMapAreaY + b.y0, b.y1 - b.y0), g_labels[i].text, kInk);
        }
    }

    const long us = (long)(esp_timer_get_time() - t0);

    // ONE line per compose that drew markers, rate limited. `fresh` also logs the first draw after a view with no
    // markers, so a pan onto a town says so at once instead of up to 3 s later.
    static uint32_t s_log_ms      = 0;
    static int      s_log_markers = 0;
    const uint32_t  now_ms        = (uint32_t)(esp_timer_get_time() / 1000);
    if (markers > 0) {
        const bool fresh = (s_log_markers == 0);
        if (fresh || (uint32_t)(now_ms - s_log_ms) >= kMarkerLogMs) {
            ESP_LOGI("tm/map", "map: %d nodes in view, %d labels, %ld us", markers, labels, us);
            s_log_ms      = now_ms;
            s_log_markers = markers;
        }
    } else {
        s_log_markers = 0;
    }
}

const char* attribution() {
    // Required by the tile source's terms (kept on the card as attribution.txt too). Short on purpose: the panel
    // is 800 px wide and this must stay readable, not complete.
    return "Esri, HERE, Garmin, OSM";
}

}  // namespace

void drawMap(Screen& s, const MapView& v) {
    s.clear((uint16_t)kBg);
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    // S516/EST-482: the page-head band is GONE (Filip: it only said "Map" and "z16" and took 44 px). The
    // canvas starts at the status bar's bottom; kMapAreaY/kMapAreaH in map_page.h are the one place that says so.

    // The map area, filled with the void colour first: a tile that is not decoded yet (or that the pack does not
    // have) leaves a visible hole rather than yesterday's pixels or the previous page.
    s.fillRect(kMapAreaX, kMapAreaY, kMapAreaW, kMapAreaH, kMapVoid);

    if (v.problem != nullptr && v.problem[0] != '\0') {
        drawText(s, Face::Body, kTmPad + 4, kMapAreaY + 28, v.problem, kDim);
    } else {
        const int64_t               t0 = esp_timer_get_time();
        const om::map::ViewTiles    vt = om::map::viewTilesFor(v.z, v.wx, v.wy, kMapAreaW, kMapAreaH);
        // Which way this compose is moving: the stand-in for a Pending tile is the parent when zooming IN and the
        // children when zooming OUT. First draw and a pan use the parent (one source, cheaper).
        const om::map::ZoomDir dir = (g_prev_z < 0 || v.z == g_prev_z)
                                         ? om::map::ZoomDir::Same
                                         : (v.z > g_prev_z ? om::map::ZoomDir::In : om::map::ZoomDir::Out);
        g_prev_z = v.z;

        // Ask the worker for whatever this view is still missing, centre-out. Idempotent: on a compose where
        // nothing is missing it queues nothing, so calling it on every repaint costs three small loops.
        const int queued = om::maplink::showView((uint8_t)v.z, vt);

        // Draw the tiles that ARE decoded. Decoding never happens on this task (S506): a zoom used to freeze the
        // panel for 1.3 s because twelve tiles were decoded inside this loop.
        int drawn = 0;
        int placeheld = 0;
        for (int row = 0; row < vt.rows; row++) {
            for (int col = 0; col < vt.cols; col++) {
                const uint32_t tx = vt.x0 + (uint32_t)col;
                const uint32_t ty = vt.y0 + (uint32_t)row;
                const int      dx = kMapAreaX + col * om::map::kTilePx - vt.shift_x;
                const int      dy = kMapAreaY + row * om::map::kTilePx - vt.shift_y;
                if (om::maplink::drawTile((uint8_t)v.z, tx, ty, dx, dy, &blitCallback, &s)) {
                    ++drawn;
                } else if (drawPlaceholders(s, (uint8_t)v.z, tx, ty, dx, dy, dir)) {
                    // S514: the real tile is still Pending/Empty, so a scaled stand-in from the cache covers the
                    // square instead of the void colour. Cache state is untouched; the next repaint after the tile
                    // lands draws the real one over it.
                    ++placeheld;
                }
            }
        }

        const long composed_us = (long)(esp_timer_get_time() - t0);
        // What the compose cost and what is still coming: a frozen screen used to hide these numbers, and a pan
        // should read "all drawn, nothing queued" while a zoom fills in over a few frames.
        ESP_LOGI("tm/map",
                 "view z%d %dx%d tiles: %d drawn, %d stand-in, %d of %d visible, %d queued, composed in %ld ms",
                 v.z, vt.cols, vt.rows, drawn, placeheld, drawn + placeheld, vt.count, queued, composed_us / 1000);
        if (queued < 0) {
            // The plan did not fit: say so on the page, because a silently clipped edge looks like a pack bug.
            drawText(s, Face::Hint, kMapAreaX + kTmPad, kMapAreaY + 4, "view too large for the tile queue", kWhite);
        }

        // S515/EST-466: the nodes, over whatever the tiles left behind (a decoded tile, a stand-in, the void).
        // S515 (Claude, review): under the store lock, like the Nodes page: the radio task inserts rows into this
        // table (advertInArchive) at any time. Only the marker pass holds it (well under a millisecond measured),
        // never the tile compose, so RX is not held up by a slow frame.
        {
            om::mc::StoreLock l;
            drawMapNodes(s, v);
        }

        // Crosshair at the centre of the view: where the app thinks "here" is. No GPS on this board yet, so it
        // marks the view, and the position source is step 6b.
        const int cx = kMapAreaX + kMapAreaW / 2;
        const int cy = kMapAreaY + kMapAreaH / 2;
        s.fillRect(cx - 12, cy, 24, 1, kAcc);
        s.fillRect(cx, cy - 12, 1, 24, kAcc);
        s.frame(cx - 3, cy - 3, 6, 6, 1, kAcc);
    }

    // S516/EST-482: the zoom level, on a small house plate in the map's TOP-RIGHT corner. It is drawn AFTER
    // the tiles and the node layer, so a name plate that happens to land there cannot hide it; the only other
    // overlays are the centre crosshair (middle) and the attribution (bottom right), so there is no collision.
    {
        char       ztext[8];
        snprintf(ztext, sizeof(ztext), "z%d", v.z);
        const Font* zf    = fontFor(Face::Hint);
        const int   ztw   = (zf != nullptr && zf->ready()) ? zf->textWidth(ztext) : 18;
        const int   zpw   = ztw + 16;
        const int   zph   = 20;
        const int   zx    = kMapAreaX + kMapAreaW - 8 - zpw;
        const int   zy    = kMapAreaY + 8;
        drawRoundedRect(s, zx, zy, zpw, zph, 4, kPanel2);
        s.frame(zx, zy, zpw, zph, 1, kLine);
        drawTextCentred(s, Face::Hint, zx + zpw / 2, textBaseline(s, Face::Hint, zy, zph), ztext, kInk);
    }

    // Attribution, bottom right of the map area: the source's terms require it to be visible.
    drawTextRight(s, Face::Hint, kMapAreaX + kMapAreaW - 6, kMapAreaY + kMapAreaH - 6, attribution(), kWhite);

    // S516/EST-482: the page keys live on the Launcher only, so this bar is the three map keys alone.
    static const HintItem hints[] = {{"dpad", "pan"}, {"+/-", "zoom"}, {"esc", "back"}};
    drawHintBar(s, tm::Page::Map, hints, 3);
}

}  // namespace ui
}  // namespace om
