// SPDX-License-Identifier: MIT
// The landscape framebuffer and the panel blit.
//
// The panel is natively 480x800 PORTRAIT and takes `bsp_display_blit(x0, y0, x1, y1, pixels)` in PANEL
// coordinates. The app draws 800x480 landscape (all layout math in tm_theme.h is landscape), so the last
// step of every frame rotates: the mapping below is the one proven on this board by WadaMesh
// (`variants/tanmatsu/main/main.cpp`, "the ROT_270 formula was 180 deg off"):
//
//     panel_y = landscape_x            panel_x = (kPanelW - 1) - landscape_y
//
// A single full-frame blit came back partial on real hardware, so the panel is written in horizontal
// bands of `kTmBlitBandH` rows through a staging buffer (`kBlitBandH`), exactly like the proven flush path.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

class Screen {
  public:
    // Allocates the 800x480 RGB565 landscape framebuffer (PSRAM) and the band staging buffer.
    bool begin();
    bool ready() const { return fb_ != nullptr; }

    int  width() const { return w_; }
    int  height() const { return h_; }

    void clear(uint16_t c);
    void px(int x, int y, uint16_t c);
    void fillRect(int x, int y, int w, int h, uint16_t c);
    void hline(int x0, int x1, int y, uint16_t c);
    void vline(int x, int y0, int y1, uint16_t c);
    void frame(int x, int y, int w, int h, int thickness, uint16_t c);
    void disc(int cx, int cy, int r, uint16_t c);
    // Anti-aliased arc-free ring: coverage sampled 2x2 per pixel.
    void ring(int cx, int cy, int r, int thickness, uint16_t c);
    // Polygon outline (closed), 1-2 px: used for the triangle/diamond key sigils.
    void polyline(const int* xs, const int* ys, int n, int thickness, uint16_t c);
    // Alpha-blit a solid colour with an 8-bit coverage mask (w*h), for sigils and rounded plates.
    void maskBlend(int x, int y, int w, int h, const uint8_t* coverage, uint16_t c);

    void* fb() { return fb_; }

    // Rotate into the panel and push it in bands. Returns false if the BSP refused a band.
    bool present();

  private:
    uint16_t* fb_       = nullptr;   // landscape, stride w_
    uint16_t* band_     = nullptr;   // portrait staging, 480 x kTmBlitBandH
    int       w_        = 0;
    int       h_        = 0;
    bool      cf_shown_ = false;
};

}  // namespace ui
}  // namespace om
