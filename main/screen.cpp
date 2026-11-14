// SPDX-License-Identifier: MIT
#include "screen.h"

#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_log.h"
#include "tm_theme.h"

extern "C" {
#include "bsp/display.h"
}

namespace om {
namespace ui {
namespace {

const char* TAG = "tm/screen";

inline uint16_t blend565(uint16_t dst, uint16_t fg, uint8_t a) {
    if (a == 0) return dst;
    if (a == 255) return fg;
    const uint32_t ia = 255 - a;
    const uint32_t r  = (((fg >> 11) & 0x1F) * a + ((dst >> 11) & 0x1F) * ia) / 255;
    const uint32_t g  = (((fg >> 5) & 0x3F) * a + ((dst >> 5) & 0x3F) * ia) / 255;
    const uint32_t b  = ((fg & 0x1F) * a + (dst & 0x1F) * ia) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

}  // namespace

bool Screen::begin() {
    w_ = kTmW;
    h_ = kTmH;
    if (fb_ == nullptr) {
        fb_ = (uint16_t*)heap_caps_malloc((size_t)w_ * h_ * sizeof(uint16_t),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (fb_ == nullptr) {   // PSRAM absent: fall back to internal (tight but honest)
            ESP_LOGW(TAG, "no PSRAM for the framebuffer, trying internal RAM");
            fb_ = (uint16_t*)heap_caps_malloc((size_t)w_ * h_ * sizeof(uint16_t), MALLOC_CAP_8BIT);
        }
    }
    if (band_ == nullptr) {
        band_ = (uint16_t*)heap_caps_malloc((size_t)kPanelW * kBlitBandH * sizeof(uint16_t),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (band_ == nullptr)
            band_ = (uint16_t*)heap_caps_malloc((size_t)kPanelW * kBlitBandH * sizeof(uint16_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (fb_ == nullptr || band_ == nullptr) {
        ESP_LOGE(TAG, "framebuffer allocation failed (fb=%p band=%p)", fb_, band_);
        return false;
    }
    if (!cf_shown_) {
        // One-shot: what the panel actually wants, so a wrong assumption is visible in the first log.
        size_t                   hh = 0, vv = 0;
        bsp_display_color_format_t cf = (bsp_display_color_format_t)0;
        bsp_display_endianness_t   en = (bsp_display_endianness_t)0;
        esp_err_t rc = bsp_display_get_parameters(&hh, &vv, &cf, &en);
        ESP_LOGI(TAG, "display parameters: rc=%d %ux%u format=%d endianness=%d", (int)rc, (unsigned)hh,
                 (unsigned)vv, (int)cf, (int)en);
        cf_shown_ = true;
    }
    clear(0x0000);
    ESP_LOGI(TAG, "landscape framebuffer %dx%d in %s", w_, h_,
             esp_ptr_external_ram(fb_) ? "PSRAM" : "internal RAM");
    return true;
}

void Screen::px(int x, int y, uint16_t c) {
    if ((unsigned)x >= (unsigned)w_ || (unsigned)y >= (unsigned)h_) return;
    fb_[(size_t)y * w_ + x] = c;
}

void Screen::clear(uint16_t c) {
    if (!fb_) return;
    for (size_t i = 0; i < (size_t)w_ * h_; i++) fb_[i] = c;
}

void Screen::fillRect(int x, int y, int w, int h, uint16_t c) {
    if (!fb_ || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > w_ ? w_ : x + w, y1 = y + h > h_ ? h_ : y + h;
    if (x1 <= x0 || y1 <= y0) return;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t* row = fb_ + (size_t)yy * w_;
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

void Screen::hline(int x0, int x1, int y, uint16_t c) { fillRect(x0, y, x1 - x0, 1, c); }

void Screen::vline(int x, int y0, int y1, uint16_t c) { fillRect(x, y0, 1, y1 - y0, c); }

void Screen::frame(int x, int y, int w, int h, int t, uint16_t c) {
    if (t <= 0) return;
    fillRect(x, y, w, t, c);                  // top
    fillRect(x, y + h - t, w, t, c);          // bottom
    fillRect(x, y + t, t, h - 2 * t, c);      // left
    fillRect(x + w - t, y + t, t, h - 2 * t, c);  // right
}

void Screen::disc(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++) {
        const int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        hline(cx - dx, cx + dx + 1, cy + dy, c);
    }
}

void Screen::ring(int cx, int cy, int r, int t, uint16_t c) {
    const int r_out = r, r_in = r - t;
    for (int dy = -r_out; dy <= r_out; dy++) {
        for (int dx = -r_out; dx <= r_out; dx++) {
            // Coverage of an ANNULUS: 1 between r_in and r_out, 0 outside AND 0 inside the hole.
            // S499: the first version multiplied by (d - r_in) and treated a negative value as "inside",
            // which filled the hole -> the circle key rendered as a solid disc ("looks like a sun").
            const float d   = sqrtf((float)(dx * dx + dy * dy));
            float       out = (float)r_out - d + 0.5f;   // 1 well inside the outer edge, 0 outside
            float       in  = d - (float)r_in + 0.5f;    // 1 well outside the hole, 0 inside it
            if (out > 1.0f) out = 1.0f;
            if (out < 0.0f) out = 0.0f;
            if (in > 1.0f) in = 1.0f;
            if (in < 0.0f) in = 0.0f;
            const float cov = out * in;
            if (cov <= 0.0f) continue;
            const int x = cx + dx, y = cy + dy;
            if ((unsigned)x >= (unsigned)w_ || (unsigned)y >= (unsigned)h_) continue;
            uint16_t& p = fb_[(size_t)y * w_ + x];
            p           = blend565(p, c, (uint8_t)(cov * 255.0f));
        }
    }
}

void Screen::polyline(const int* xs, const int* ys, int n, int t, uint16_t c) {
    for (int i = 0; i < n; i++) {
        const int j  = (i + 1) % n;
        const int x0 = xs[i], y0 = ys[i], x1 = xs[j], y1 = ys[j];
        const int steps = (abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0)) * 2 + 1;
        for (int s = 0; s <= steps; s++) {
            const int x = x0 + (x1 - x0) * s / steps;
            const int y = y0 + (y1 - y0) * s / steps;
            fillRect(x - t / 2, y - t / 2, t, t, c);
        }
    }
}

void Screen::maskBlend(int x, int y, int w, int h, const uint8_t* cov, uint16_t c) {
    for (int yy = 0; yy < h; yy++) {
        const int py = y + yy;
        if ((unsigned)py >= (unsigned)h_) continue;
        uint16_t* row = fb_ + (size_t)py * w_;
        const uint8_t* line = cov + (size_t)yy * w;
        for (int xx = 0; xx < w; xx++) {
            const int px_ = x + xx;
            if ((unsigned)px_ >= (unsigned)w_ || !line[xx]) continue;
            row[px_] = blend565(row[px_], c, line[xx]);
        }
    }
}

bool Screen::present() {
    if (!fb_ || !band_) return false;
    bool ok = true;
    for (int py = 0; py < kPanelH; py += kBlitBandH) {
        const int rows = (py + kBlitBandH > kPanelH) ? (kPanelH - py) : kBlitBandH;
        // landscape (lx, ly) -> panel row py = lx, panel col px = (kPanelW-1) - ly
        for (int pxx = 0; pxx < kPanelW; pxx++) {
            const int          ly   = (kPanelW - 1) - pxx;
            const uint16_t*    src  = fb_ + (size_t)ly * w_ + py;
            uint16_t*          dst  = band_ + pxx;
            for (int i = 0; i < rows; i++) dst[(size_t)i * kPanelW] = src[i];
        }
        bsp_display_blit(0, py, kPanelW, py + rows, band_);
    }
    return ok;
}

}  // namespace ui
}  // namespace om
