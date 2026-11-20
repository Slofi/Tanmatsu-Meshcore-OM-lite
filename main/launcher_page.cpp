// SPDX-License-Identifier: MIT
#include "launcher_page.h"

#include <stdint.h>

#include "chrome.h"
#include "menu_cards.h"   // the menu focus rules (S516/EST-482: three cards only)
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

namespace {

// The state chip's word and colour: the one place a card's two booleans become the LATER / STOPPED /
// RUNNING the panel shows, so the chip can never say RUNNING beside a stopped module.
const char* chipText(const MenuCard& c) {
    if (!c.available) return "LATER";
    return c.running ? "RUNNING" : "STOPPED";
}

uint16_t chipColour(const MenuCard& c) {
    if (!c.available) return kDimmer;
    return c.running ? (uint16_t)kGreen : (uint16_t)kDim;
}

}  // namespace

// -------------------------------------------------------------------------------------------- frame
void drawLauncher(Screen& s, const LauncherView& v) {
    const int W = kTmW;
    s.clear((uint16_t)kBg);

    // ---- status bar (the L2's: clock left, radio/wifi/battery/gear right) — shared with the other pages
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    // ---- page head: the app's name, the core line on the right. A live notice owns that right slot for
    // its three seconds (the EST-428 pattern), so the core line returns by itself.
    const char* right = (v.notice != nullptr && v.notice[0] != '\0') ? v.notice : v.right_head;
    const int   head_bot = drawPageHead(s, "Tanmatsu", right);

    // ---- three equal cards from the work box; the odd leftover pixel stays in a gap
    int count = v.card_count;
    if (count < 0) count = 0;
    if (count > 3) count = 3;
    int focused = v.focused;
    if (focused < 0) focused = 0;
    if (focused > count - 1) focused = count - 1;

    // S516/EST-482: the Settings bar under the cards is GONE (Filip: "no need for it, that's what an F-key is
    // for"). The cards + their one dim line are re-centred in the work box so the freed 72 px does not leave a
    // hole at the bottom; kTmMenuTop stays the minimum under the page head.
    const int body_top = head_bot;
    const int body_bot = kTmH - kTmHintH;
    const int group_h  = kTmMenuCardH + kTmMenuOwnsGap + 24;
    int       top      = body_top + (body_bot - body_top - group_h) / 2;
    if (top < body_top + kTmMenuTop) top = body_top + kTmMenuTop;
    if (top + group_h > body_bot) top = body_bot - group_h;

    const int cw  = (W - 2 * kTmPad - (count > 1 ? (count - 1) * kTmMenuGap : 0)) / (count > 0 ? count : 1);

    for (int i = 0; i < count; i++) {
        const MenuCard& c = v.cards[i];
        const int       x = kTmPad + i * (cw + kTmMenuGap);
        const bool      f = (i == focused);
        // resting card: kPanel plate + 1 px kLine; focused: kSelBg plate + the 2 px gold ring (the Chat cards'
        // exact recipe, from chrome.cpp so the two pages share one implementation)
        drawCardPlate(s, x, top, cw, kTmMenuCardH, kTmMenuRadius, f ? (int)kTmRingW : 1,
                      f ? (uint16_t)kAcc : (uint16_t)kLine, f ? (uint16_t)kSelBg : (uint16_t)kPanel);

        const int ix = x + 16;
        drawText(s, Face::Title, ix, textBaseline(s, Face::Title, top + 10, 34), c.name,
                 f ? (uint16_t)kAccInk : (uint16_t)kInk);
        drawText(s, Face::Body, ix, textBaseline(s, Face::Body, top + 50, 24), c.sub, kDim);

        // state chip: the existing chip style (Panel2 plate, 1 px kLine, mono caps), green + line for running
        const char* chip = chipText(c);
        const Font* hf   = fontFor(Face::Hint);
        const int   tw   = (hf && hf->ready()) ? hf->textWidth(chip) : 0;
        const int   cw2  = tw + 14;
        s.fillRect(ix, top + 84, cw2, 22, rgb565(0x242321));
        s.frame(ix, top + 84, cw2, 22, 1, rgb565(0x3a3936));
        drawTextCentred(s, Face::Hint, ix + cw2 / 2, textBaseline(s, Face::Hint, top + 84, 22), chip,
                        chipColour(c));

        if (c.available && c.running) {
            // the two small lines the old detail pane carried: the module's memory and how long it has run
            if (c.memory) drawText(s, Face::Meta, ix, textBaseline(s, Face::Meta, top + 122, 24), c.memory, kDim);
            if (c.uptime) drawText(s, Face::Meta, ix, textBaseline(s, Face::Meta, top + 144, 24), c.uptime, kDim);
        } else if (!c.available) {
            drawText(s, Face::Body, ix, textBaseline(s, Face::Body, top + 126, 24), "coming later", kDim);
        } else {
            drawText(s, Face::Body, ix, textBaseline(s, Face::Body, top + 126, 24), "stopped \xc2\xb7 ret starts it",
                     kDim);
        }
    }

    // ---- one short dim line about the FOCUSED card: what it owns, in the mesh's own numbers. Settings is the
    // diamond symbol key now, so this line sits directly under the cards.
    if (count > 0 && focused >= 0 && focused < count && v.cards[focused].owns) {
        drawText(s, Face::Body, kTmPad,
                 textBaseline(s, Face::Body, top + kTmMenuCardH + kTmMenuOwnsGap, 24), v.cards[focused].owns,
                 kDim);
    }

    // ---- hint bar: this page's actions left, the six page keys right (X is Launcher, S516/EST-478).
    // The panel's generated faces are the L2 subset (ASCII + Latin-1/Extended-A, tools/mkfonts.py), so the
    // key chips say "l/r" and "Enter" exactly like every other page's hint bar; the preview draws ←→ / Enter in
    // desktop DejaVu. Settings is now the diamond in the page-key cluster, not a left-hand chip.
    static const HintItem hints[] = {{"l/r", "select"}, {"Enter", "open"}, {"esc", "quit"}};
    drawHintBar(s, tm::Page::Launcher, hints, 3);
}

}  // namespace ui
}  // namespace om
