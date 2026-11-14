// SPDX-License-Identifier: MIT
#include "map_page.h"

#include <stdio.h>
#include <string.h>

#include "chrome.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "launcher_page.h"
#include "map_link.h"
#include "map_view.h"
#include "nodes_page.h"   // drawTextClipped
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"

namespace om {
namespace ui {
namespace {

// Light grey for the sea/void: the tiles are opaque, so this only shows where the pack has no data (outside
// Slovenia's bounds) and while a tile is missing.
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

const char* attribution() {
    // Required by the tile source's terms (kept on the card as attribution.txt too). Short on purpose: the panel
    // is 800 px wide and this must stay readable, not complete.
    return "Esri, HERE, Garmin, OSM";
}

}  // namespace

void drawMap(Screen& s, const MapView& v) {
    s.clear((uint16_t)kBg);
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    char head_right[32];
    snprintf(head_right, sizeof(head_right), "z%d", v.z);
    const int head_bot = drawPageHead(s, "Map", head_right);
    (void)head_bot;

    // The map area, filled with the void colour first: a tile that fails to load leaves a visible hole rather
    // than yesterday's pixels or the previous page.
    s.fillRect(kMapAreaX, kMapAreaY, kMapAreaW, kMapAreaH, kMapVoid);

    if (v.problem != nullptr && v.problem[0] != '\0') {
        drawText(s, Face::Body, kTmPad + 4, kMapAreaY + 28, v.problem, kDim);
    } else {
        (void)om::maplink::takeStats(nullptr, nullptr);   // clear the counters for this compose
        const int64_t               t0 = esp_timer_get_time();
        const om::map::ViewTiles vt = om::map::viewTilesFor(v.z, v.wx, v.wy, kMapAreaW, kMapAreaH);
        for (int row = 0; row < vt.rows; row++) {
            for (int col = 0; col < vt.cols; col++) {
                const uint32_t tx = vt.x0 + (uint32_t)col;
                const uint32_t ty = vt.y0 + (uint32_t)row;
                int            tw = 0, th = 0;
                const uint16_t* px = om::maplink::tile((uint8_t)v.z, tx, ty, &tw, &th);
                const int       dx = kMapAreaX + col * om::map::kTilePx - vt.shift_x;
                const int       dy = kMapAreaY + row * om::map::kTilePx - vt.shift_y;
                blitTile(s, px, tw, th, dx, dy);
            }
        }
        uint32_t hits = 0, misses = 0;
        om::maplink::takeStats(&hits, &misses);
        const long composed_us = (long)(esp_timer_get_time() - t0);
        // Evidence for the lag: a press that only moved the view should be nearly all hits.
        ESP_LOGI("tm/map", "view z%d %dx%d tiles: %u hit / %u miss, composed in %ld ms", v.z, vt.cols, vt.rows,
                 (unsigned)hits, (unsigned)misses, composed_us / 1000);

        // Crosshair at the centre of the view: where the app thinks "here" is. No GPS on this board yet, so it
        // marks the view, and the position source is step 6b.
        const int cx = kMapAreaX + kMapAreaW / 2;
        const int cy = kMapAreaY + kMapAreaH / 2;
        s.fillRect(cx - 12, cy, 24, 1, kAcc);
        s.fillRect(cx, cy - 12, 1, 24, kAcc);
        s.frame(cx - 3, cy - 3, 6, 6, 1, kAcc);
    }

    // Attribution, bottom right of the map area: the source's terms require it to be visible.
    drawTextRight(s, Face::Hint, kMapAreaX + kMapAreaW - 6, kMapAreaY + kMapAreaH - 6, attribution(), kWhite);

    static const HintItem hints[] = {{"dpad", "pan"}, {"+/-", "zoom"}, {"esc", "back"}};
    drawHintBar(s, tm::Page::Map, hints, 3);
}

}  // namespace ui
}  // namespace om
