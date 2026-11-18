// SPDX-License-Identifier: MIT
#include "chrome.h"

#include "launcher_page.h"   // textBaseline / drawText* — the one place the baseline math lives
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"   // the L2 palette: one source of truth, never copied

namespace om {
namespace ui {

// The five physical symbol keys, drawn as vectors (BRIEF § 5; the preview tool's recipes, ported).
// Cloud is an OUTLINE like the other four (Filip, S499 "fix the cloud symbol").
void drawSymbolSigil(Screen& s, int kind, int cx, int cy, int r, uint16_t c, bool active) {
    switch (kind) {
        case 0: {  // triangle
            const int xs[3] = {cx, cx + r, cx - r};
            const int ys[3] = {cy - r, cy + r, cy + r};
            s.polyline(xs, ys, 3, 2, c);
            break;
        }
        case 1:  // square
            s.frame(cx - r, cy - r, 2 * r, 2 * r, 2, c);
            break;
        case 2:  // circle
            s.ring(cx, cy, r, 2, c);
            break;
        case 3: {  // cloud = the tri-circle key
            // Outer union minus the same union shrunk by the stroke: an outline with no internal arcs.
            const int w = 2 * r + 8;
            static uint8_t cov[64 * 64];
            if (w > 64) return;
            const float rr = r * 0.60f;
            const float ox[3] = {0, -r * 0.86f, r * 0.86f};
            const float oy[3] = {-r * 0.45f, r * 0.10f, r * 0.10f};
            auto inside = [&](float x, float y, float shrink) {
                for (int i = 0; i < 3; i++) {
                    const float dx = x - ox[i], dy = y - oy[i];
                    const float rr2 = (rr - shrink) * (rr - shrink);
                    if (dx * dx + dy * dy <= rr2) return true;
                }
                // the bar under the three circles
                const float bx0 = -r + shrink, bx1 = r - shrink;
                const float by0 = 0 + shrink * 0.5f, by1 = r * 0.75f - shrink * 0.5f;
                return x >= bx0 && x <= bx1 && y >= by0 && y <= by1;
            };
            for (int y = 0; y < w; y++) {
                for (int x = 0; x < w; x++) {
                    int hits = 0;
                    for (int sy = 0; sy < 2; sy++)
                        for (int sx = 0; sx < 2; sx++) {
                            const float fx = (float)x + 0.25f + 0.5f * sx - w * 0.5f;
                            const float fy = (float)y + 0.25f + 0.5f * sy - w * 0.5f;
                            if (inside(fx, fy, 0.0f) && !inside(fx, fy, 2.0f)) hits++;
                        }
                    cov[y * w + x] = (uint8_t)(hits * 255 / 4);
                }
            }
            s.maskBlend(cx - w / 2, cy - w / 2, w, w, cov, c);
            break;
        }
        case 4: {  // diamond
            const int xs[4] = {cx, cx + r, cx, cx - r};
            const int ys[4] = {cy - r, cy, cy + r, cy};
            s.polyline(xs, ys, 4, 2, c);
            break;
        }
        default: break;
    }
    (void)active;
}

void drawRoundedRect(Screen& s, int x, int y, int w, int h, int r, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) {
        s.fillRect(x, y, w, h, c);
        return;
    }
    for (int row = 0; row < h; row++) {
        int dy;
        if (row < r)
            dy = r - row;                 // top corners: distance from the corner circle's centre
        else if (row >= h - r)
            dy = row - (h - r - 1);
        else {
            s.fillRect(x, y + row, w, 1, c);
            continue;
        }
        int v = r * r - dy * dy;
        if (v < 0) v = 0;
        int root = 0;
        while ((root + 1) * (root + 1) <= v) ++root;
        const int inset = r - root;
        if (w - 2 * inset > 0) s.fillRect(x + inset, y + row, w - 2 * inset, 1, c);
    }
}

void drawCardPlate(Screen& s, int x, int y, int w, int h, int radius, int thickness, uint16_t outline,
                   uint16_t fill) {
    drawRoundedRect(s, x, y, w, h, radius, outline);
    if (thickness > 0 && h - 2 * thickness > 0)
        drawRoundedRect(s, x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness,
                        radius - thickness, fill);
}

void drawStatusBar(Screen& s, const char* clock, int battery_pct, bool charging) {
    const int W = kTmW;
    s.fillRect(0, 0, W, kTmStatusH, kBar);
    s.hline(0, W, kTmStatusH, kLine);
    drawText(s, Face::Hint, kTmPad, textBaseline(s, Face::Hint, 0, kTmStatusH), clock, kDim);

    int right = W - kTmPad;
    drawText(s, Face::Hint, right - 14, textBaseline(s, Face::Hint, 0, kTmStatusH), "o", kDim);  // gear
    right -= 26;
    if (battery_pct >= 0) {
        // a small battery: outline + fill + optional bolt
        const int bw = 22, bh = 11, by = (kTmStatusH - bh) / 2;
        s.frame(right - bw, by, bw, bh, 1, kDim);
        s.fillRect(right - bw + bh + 2, by + 3, 2, bh - 6, kDim);   // terminal
        const int inner_w = (bw - 4) * battery_pct / 100;
        s.fillRect(right - bw + 2, by + 2, inner_w, bh - 4,
                   battery_pct < 15 ? (uint16_t)0xF800 : (uint16_t)kGreen);
        if (charging) s.fillRect(right - bw + bw / 2 - 1, by + 2, 2, bh - 4, 0xFFFF);
        right -= bw + 10;
    }
    drawText(s, Face::Hint, right - 12, textBaseline(s, Face::Hint, 0, kTmStatusH), "w", kDimmer);
    right -= 22;
    drawText(s, Face::Hint, right - 12, textBaseline(s, Face::Hint, 0, kTmStatusH), "@", kDimmer);
}

int drawPageHead(Screen& s, const char* title, const char* right_text) {
    const int head_top = kTmStatusH;
    drawText(s, Face::Title, kTmPad, textBaseline(s, Face::Title, head_top, kTmHeadH), title, kInk);
    if (right_text && right_text[0])
        drawTextRight(s, Face::Meta, kTmW - kTmPad, textBaseline(s, Face::Meta, head_top, kTmHeadH),
                      right_text, kDimmer);
    s.hline(kTmPad, kTmW - kTmPad, head_top + kTmHeadH, kLine);
    return head_top + kTmHeadH;
}

void drawHintBar(Screen& s, tm::Page active, const HintItem* hints, int hint_count) {
    const int W = kTmW, H = kTmH;
    s.fillRect(0, H - kTmHintH, W, kTmHintH, kBar);
    s.hline(0, W, H - kTmHintH, kLine);
    const int hy = H - kTmHintH;
    const int cy = hy + kTmHintH / 2;
    const int bl = textBaseline(s, Face::Hint, hy, kTmHintH);

    int hx = kTmPad;
    for (int i = 0; i < hint_count && hints && i < 4; i++) {
        if (hints[i].key == nullptr || hints[i].key[0] == '\0') continue;
        const int kw = 30;
        s.fillRect(hx, cy - 9, kw, 18, rgb565(0x232220));
        s.frame(hx, cy - 9, kw, 18, 1, rgb565(0x3a3936));
        drawTextCentred(s, Face::Hint, hx + kw / 2, bl, hints[i].key, kDim);
        hx += kw + 6;
        drawText(s, Face::Hint, hx, bl, hints[i].label, kDimmer);
        const Font* f = fontFor(Face::Hint);
        hx += 14 + (f && f->ready() ? f->textWidth(hints[i].label) : 30) + 16;
    }

    static const char* page_names[5] = {"LAUNCHER", "NODES", "CHAT", "MAP", "RPTR"};
    const Font*        hf            = fontFor(Face::Hint);
    int                sx            = W - kTmPad;
    for (int i = 4; i >= 0; i--) {
        const bool on = (static_cast<int>(active) == i);
        const int  tw = (hf && hf->ready()) ? hf->textWidth(page_names[i]) : 40;
        sx -= tw;
        drawText(s, Face::Hint, sx, bl, page_names[i], on ? (uint16_t)kAccInk : kDimmer);
        sx -= 22;
        drawSymbolSigil(s, i, sx, cy, 7, on ? (uint16_t)kAcc : (uint16_t)kDim, on);
        sx -= 18;
    }
}

}  // namespace ui
}  // namespace om
