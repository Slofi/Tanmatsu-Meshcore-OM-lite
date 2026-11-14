// SPDX-License-Identifier: MIT
#include "launcher_page.h"

#include <stdio.h>
#include <string.h>

#include "chrome.h"
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"   // the L2 palette: one source of truth, never copied

namespace om {
namespace ui {
// -------------------------------------------------------------------------------------------- text
int textBaseline(Screen& s, Face face, int top, int line_h) {
    (void)s;
    const Font* f = fontFor(face);
    if (!f || !f->ready()) return top + line_h - 4;
    const int asc = f->ascent();
    return top + (line_h - (asc + f->descent())) / 2 + asc;
}

void drawText(Screen& s, Face face, int x, int baseline, const char* utf8, uint16_t colour) {
    const Font* f = fontFor(face);
    if (!f || !f->ready() || !utf8) return;
    f->draw(s.fb(), s.width(), s.height(), x, baseline, utf8, colour);
}

void drawTextRight(Screen& s, Face face, int x_right, int baseline, const char* utf8, uint16_t colour) {
    const Font* f = fontFor(face);
    if (!f || !f->ready() || !utf8) return;
    f->draw(s.fb(), s.width(), s.height(), x_right - f->textWidth(utf8), baseline, utf8, colour);
}

void drawTextCentred(Screen& s, Face face, int cx, int baseline, const char* utf8, uint16_t colour) {
    const Font* f = fontFor(face);
    if (!f || !f->ready() || !utf8) return;
    f->draw(s.fb(), s.width(), s.height(), cx - f->textWidth(utf8) / 2, baseline, utf8, colour);
}

// -------------------------------------------------------------------------------------------- frame
void drawLauncher(Screen& s, const LauncherView& v) {
    const int W = kTmW, H = kTmH;
    s.clear((uint16_t)kBg);

    // ---- status bar (the L2's: clock left, radio/wifi/battery/gear right) — shared with the other pages
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    // ---- page title row
    const int head_top = kTmStatusH;
    drawText(s, Face::Title, kTmPad, textBaseline(s, Face::Title, head_top, kTmHeadH), "Launcher", kInk);
    drawTextRight(s, Face::Meta, W - kTmPad, textBaseline(s, Face::Meta, head_top, kTmHeadH), v.right_head,
                  kDimmer);
    s.hline(kTmPad, W - kTmPad, head_top + kTmHeadH, kLine);

    // ---- left pane: the modules
    const int list_x0 = kTmPad;
    const int list_x1 = kTmPad + kTmListW;
    const int work_top = head_top + kTmHeadH;
    const int work_bot = H - kTmHintH;
    char      count[32];
    snprintf(count, sizeof(count), "%d · %d running", v.row_count, 1);
    drawText(s, Face::Meta, list_x0, textBaseline(s, Face::Meta, work_top, 26) - 2, "MODULES", kDimmer);
    drawTextRight(s, Face::Hint, list_x1 - kTmRingInset, textBaseline(s, Face::Meta, work_top, 26) - 2, count,
                  kDimmer);

    int y = work_top + 30;
    for (int i = 0; i < v.row_count && y + kTmRowH <= work_bot - 8; i++) {
        const ModuleRow& r  = v.rows[i];
        const bool       f  = (i == v.focused);
        const uint16_t   ink = r.available ? kInk : kDimmer;
        if (f) {
            // the L2's selected band; the 2 px gold ring marks WHICH AREA has the focus (Filip, S499:
            // Enter takes it to the actions, Esc brings it back), so with the actions focused the band
            // stays but the ring goes plain.
            const int right = list_x1 - kTmRingInset;
            s.fillRect(list_x0, y, right - list_x0, kTmRowH - 2, kSelBg);
            if (!v.focus_actions)
                s.frame(list_x0, y, right - list_x0, kTmRowH - 2, kTmRingW, kAcc);
            else
                s.frame(list_x0, y, right - list_x0, kTmRowH - 2, 1, kLine);
        }
        // status dot: green when running, dim when later
        const int dot_cx = list_x0 + 14, dot_cy = y + kTmRowH / 2 - 1;
        if (r.available)
            s.disc(dot_cx, dot_cy, 4, r.running ? (uint16_t)kGreen : (uint16_t)kDim);
        else {
            s.ring(dot_cx, dot_cy, 4, 1, kDimmer);
        }
        drawText(s, Face::Row, list_x0 + 30, textBaseline(s, Face::Row, y + 6, 28), r.name,
                 f ? (uint16_t)kAccInk : ink);
        drawText(s, Face::Meta, list_x0 + 30, textBaseline(s, Face::Meta, y + 34, 22), r.sub, kDim);
        if (r.badge && r.badge[0]) {
            const Font* hf = fontFor(Face::Hint);
            const int   tw = hf && hf->ready() ? hf->textWidth(r.badge) : 0;
            const int   pw = tw + 14, ph = 18;
            const int   bx = list_x0 + kTmListW - kTmRingInset - pw - 8, by2 = y + (kTmRowH - ph) / 2 - 1;
            s.fillRect(bx, by2, pw, ph, r.running ? rgb565(0x14301F) : rgb565(0x242321));
            s.frame(bx, by2, pw, ph, 1, r.running ? (uint16_t)kGreen : (uint16_t)rgb565(0x3a3936));
            drawTextCentred(s, Face::Hint, bx + pw / 2, textBaseline(s, Face::Hint, by2, ph), r.badge,
                            r.running ? (uint16_t)kGreen : kDimmer);
        }
        if (i + 1 < v.row_count) s.hline(list_x0, list_x1 - kTmRingInset, y + kTmRowH - 1, kRowLine);
        y += kTmRowH;
    }

    // ---- the note under the list (the mock's honest line about one app at a time)
    drawText(s, Face::Body, list_x0, y + 26, "The stock launcher starts one app at a time.", kDimmer);
    drawText(s, Face::Body, list_x0, y + 48, "Here the modules start and stop inside this", kDimmer);
    drawText(s, Face::Body, list_x0, y + 70, "app; the core keeps the radio.", kDimmer);

    // ---- pane divider + right pane: the focused module
    s.vline(list_x1 + 8, work_top, work_bot - 8, kLine);
    const int rx = list_x1 + kTmPad + 14;
    if (v.detail) {
        const ModuleDetail& d = *v.detail;
        s.fillRect(rx - kTmPad + 4, work_top + 1, W - (rx - kTmPad + 4), work_bot - work_top - 2, kScreen);
        drawText(s, Face::Title, rx, textBaseline(s, Face::Title, work_top, 44), d.title, kInk);
        if (d.meta)
            drawText(s, Face::Meta, rx, textBaseline(s, Face::Meta, work_top + 36, 24), d.meta, kDim);
        int kv_y = work_top + 72;
        for (int i = 0; i < 4 && d.kv[i][0]; i++) {
            drawText(s, Face::Kv, rx, textBaseline(s, Face::Kv, kv_y, 26), d.kv[i][0], kDimmer);
            if (d.kv[i][1])
                drawText(s, Face::Body, rx + 150, textBaseline(s, Face::Body, kv_y, 26), d.kv[i][1], kInk);
            kv_y += 28;
        }
        // the two buttons (gold "Open", plain "Stop"), then the note
        const int by = kv_y + 18;
        const int open_w = 120, stop_w = 96, bh = 46;
        const bool open_hot = v.focus_actions && v.action_index == 0;
        const bool stop_hot = v.focus_actions && v.action_index == 1;
        s.fillRect(rx, by, open_w, bh, kSelBg2);
        s.frame(rx, by, open_w, bh, open_hot ? kTmRingW : 1, open_hot ? kAcc : kLine);
        drawTextCentred(s, Face::Row, rx + open_w / 2, textBaseline(s, Face::Row, by, bh), "Open",
                        open_hot ? kAccInk : kInk);
        s.fillRect(rx + open_w + 16, by, stop_w, bh, kPanel);
        s.frame(rx + open_w + 16, by, stop_w, bh, stop_hot ? kTmRingW : 1, stop_hot ? kAcc : kLine);
        drawTextCentred(s, Face::Row, rx + open_w + 16 + stop_w / 2, textBaseline(s, Face::Row, by, bh), "Stop",
                        stop_hot ? kAccInk : kInk);
        if (d.note)
            drawText(s, Face::Body, rx, by + bh + 34, d.note, kDim);
    }

    // ---- hint bar: this page's actions left, the five symbol keys right (chrome.cpp: one bar, all pages)
    static const HintItem hints_list[] = {
        {"up/dn", "move"}, {"ret", "actions"}, {"s", "stop"}, {"esc", "quit"}};
    static const HintItem hints_act[] = {
        {"l/r", "pick"}, {"ret", "do it"}, {"", ""}, {"esc", "back"}};
    drawHintBar(s, tm::Page::Launcher, v.focus_actions ? hints_act : hints_list, 4);
}

}  // namespace ui
}  // namespace om
