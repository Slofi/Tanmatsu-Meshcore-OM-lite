// SPDX-License-Identifier: MIT
// Settings, part 1 — the drawing. See settings_page.h and core/settings.h for the split.
#include "settings_page.h"

#include <stdio.h>
#include <string.h>

#include "chrome.h"
#include "launcher_page.h"   // textBaseline / drawText*: one home for baseline maths
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"   // the L2 palette: one source of truth

namespace om {
namespace ui {
namespace {

void drawSpaced(Screen& s, Face face, int x, int baseline, const char* text, uint16_t colour,
                int gap) {
    const Font* f = fontFor(face);
    if (!f || !f->ready() || text == nullptr) return;
    int pen = x;
    for (const char* p = text; *p; ++p) {
        char one[2] = {*p, '\0'};
        drawText(s, face, pen, baseline, one, colour);
        pen += f->textWidth(one) + gap;
    }
}

// A chevron drawn as two strokes, not a glyph: the generated faces carry Latin-1 + Latin Extended-A,
// so U+203A is not in them and a missing glyph would read as "no sub-page".
void drawChevron(Screen& s, int x, int cy, uint16_t colour) {
    const int xs[3] = {x, x + 5, x};
    const int ys[3] = {cy - 4, cy, cy + 4};
    s.polyline(xs, ys, 3, 2, colour);
}

void drawScrollThumb(Screen& s, int body_top, int body_h, int total, int scroll) {
    if (total <= body_h || body_h <= 0) return;
    const int x0 = kTmW - 5;
    s.fillRect(x0, body_top, 3, body_h, kLine);
    int thumb = body_h * body_h / total;
    if (thumb < 20) thumb = 20;
    if (thumb > body_h) thumb = body_h;
    const int max_scroll = total - body_h;
    const int top = body_top + (body_h - thumb) * (scroll < 0 ? 0 : (scroll > max_scroll ? max_scroll : scroll)) /
                                   (max_scroll > 0 ? max_scroll : 1);
    s.fillRect(x0, top, 3, thumb, kAcc);
}

}  // namespace

// One body: a section list with the gold focus ring, the row lines and the gold scroll thumb. Used by a
// TAB's sections and by a LIST sub-page (the channel / scope / add-by-key pages, S514/EST-446), so the two
// can never drift in row height, focus look or scrolling.
void drawSettingsRows(Screen& s, const tm::SettingsSection* secs, int count, int focus, int scroll,
                      int body_top, int body_h, tm::Core* core) {
    const int W = kTmW, pad = kTmPad;
    const int body_bot = body_top + body_h;
    const int total    = tm::settingsContentHeight(secs, count);
    if (secs == nullptr || count <= 0) {
        // A module with no settings data yet: one honest dim line, never an empty page.
        drawText(s, Face::Body, pad + 6, textBaseline(s, Face::Body, body_top + 10, tm::kSettingsRowH),
                 tm::kSettingsPlaceholder, kDim);
        return;
    }
    int flat = 0;
    for (int si = 0; si < count; ++si) {
        const tm::SettingsSection& sec = secs[si];
        const int header_top = body_top + tm::settingsRowTop(secs, count, flat) - scroll;
        const int header_bot = header_top + tm::kSettingsSectionH;
        if (header_top >= body_top && header_bot <= body_bot) {
            drawSpaced(s, Face::Meta, pad, textBaseline(s, Face::Meta, header_top, tm::kSettingsSectionH),
                       sec.header != nullptr ? sec.header : "", kDim, 1);
        }
        ++flat;
        const int rows = sec.rows != nullptr && sec.row_count > 0 ? sec.row_count : 0;
        for (int j = 0; j < rows; ++j, ++flat) {
            const tm::SettingRow& row = sec.rows[j];
            const int rh   = tm::settingsRowHeight(row);
            const int top  = body_top + tm::settingsRowTop(secs, count, flat) - scroll;
            const int bot  = top + rh;
            if (bot <= body_top || top >= body_bot) continue;
            if (top < body_top || bot > body_bot) continue;   // no text on the chrome

            const bool focused = (flat == focus);
            const int right_edge = W - pad;
            if (focused) {
                s.fillRect(pad, top, right_edge - pad, rh, kSelBg);
                s.frame(pad, top, right_edge - pad, rh, 2, kAcc);
            } else {
                s.hline(pad, right_edge, bot - 1, kRowLine);
            }

            // S516/EST-476: a row can be dimmed by live state (the Add-heard row while saving is Off) and can
            // carry a short dim hint after its label. Both are optional; a row with neither draws exactly as
            // before.
            const bool row_dim = (core != nullptr && row.read_dim != nullptr) ? row.read_dim(*core) : false;
            const uint16_t label_col =
                focused ? (uint16_t)(row_dim ? kDim : kAccInk) : (uint16_t)(row_dim ? kDimmer : kInk);
            const uint16_t value_col =
                focused ? (uint16_t)(row_dim ? kDimmer : kAcc) : (uint16_t)(row_dim ? kDimmer : kDim);
            const char* label = row.label != nullptr ? row.label : "";
            drawText(s, Face::Body, pad + 6, textBaseline(s, Face::Body, top, rh), label, label_col);
            const char* hint = (core != nullptr && row.read_hint != nullptr) ? row.read_hint(*core) : nullptr;
            if (hint != nullptr && hint[0] != '\0') {
                const Font* lf = fontFor(Face::Body);
                const int   label_w = (lf != nullptr && lf->ready()) ? lf->textWidth(label) : 0;
                drawText(s, Face::Meta, pad + 6 + label_w + 12, textBaseline(s, Face::Meta, top, rh), hint,
                         kDimmer);
            }

            const char* value = (core != nullptr) ? tm::settingsRowValue(*core, row)
                                                  : (row.value != nullptr ? row.value : "");
            const bool chevron = (row.kind == tm::SettingKind::SubPage);
            const int value_right = right_edge - (chevron ? 22 : 6);
            if (right_edge - 5 > 0) {
                drawTextRight(s, Face::Meta, value_right, textBaseline(s, Face::Meta, top, rh), value, value_col);
            }
            if (chevron) {
                drawChevron(s, right_edge - 13, top + rh / 2, focused ? (uint16_t)kAcc : (uint16_t)kDimmer);
            }
        }
    }
    drawScrollThumb(s, body_top, body_h, total, scroll);
}

void drawSettings(Screen& s, const SettingsView& v) {
    s.clear((uint16_t)kBg);
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    const int W = kTmW;
    const int pad = kTmPad;

    // ---- page head: the frame's name, and the tab/sub-page it is on (the L2's right-hand slot).
    char right[40];
    if (v.sub_page) {
        if (v.sections != nullptr && v.section_count > 0)
            snprintf(right, sizeof(right), "%s", v.sub_title != nullptr ? v.sub_title : "Settings");
        else
            snprintf(right, sizeof(right), "%s · 1-31 chars", v.sub_title != nullptr ? v.sub_title : "Name");
    } else {
        const int tabs = v.tab_count > 0 ? v.tab_count : 1;
        snprintf(right, sizeof(right), "%s · tab %d of %d",
                 tm::settingsTabLabel(v.tabs, tabs, v.tab), tm::settingsTabIndex(v.tab, tabs) + 1, tabs);
    }
    const int head_bot = drawPageHead(s, "Settings", right);

    if (v.sub_page) {
        // No tab row on a sub-page (the L2's setwrap). A LIST sub-page draws the same rows as a tab; the
        // text sub-page draws the shared field, typed on the physical keyboard like the Chat composer.
        const int body_top = kSetSubBodyTop;
        if (v.sections != nullptr && v.section_count > 0) {
            const int body_h = kSetSubBodyH;
            drawSettingsRows(s, v.sections, v.section_count, v.focus, v.scroll, body_top, body_h, v.core);
        } else {
        const int field_h  = 52;
        const int fx = pad, fw = W - 2 * pad;
        const int fy = body_top + 28;
        s.fillRect(fx, fy, fw, field_h, kFieldBg);
        s.frame(fx, fy, fw, field_h, 2, kAcc);

        const int bl = textBaseline(s, Face::Row, fy, field_h);
        drawText(s, Face::Row, fx + 14, bl, v.draft != nullptr ? v.draft : "", kInk);
        // A caret after the draft: the composer's own "there is room for more" cue.
        const Font* rf = fontFor(Face::Row);
        const int   tx = fx + 14 + (rf && rf->ready() ? rf->textWidth(v.draft != nullptr ? v.draft : "") : 0);
        s.vline(tx + 2, fy + 14, fy + field_h - 14, kAcc);

        char count[32];
        snprintf(count, sizeof(count), "%u / %u", (unsigned)v.draft_len, (unsigned)v.sub_max);
        drawText(s, Face::Meta, fx + 4, textBaseline(s, Face::Meta, fy + field_h + 10, 24), count, kDim);
        drawText(s, Face::Meta, fx + 4, textBaseline(s, Face::Meta, fy + field_h + 38, 24),
                 v.sub_hint != nullptr ? v.sub_hint : "", kDimmer);
        }
    } else {
        // ---- the owner tabs, equal widths, the current one gold with an underline. The list is the
        // registry's (core/settings.h::settingsTabDefs), never a second hard-coded row.
        const int tabs = v.tab_count > 0 ? v.tab_count : 1;
        const int tab_top = head_bot + 1;
        for (int i = 0; i < tabs; ++i) {
            const int x0 = W * i / tabs;
            const int x1 = W * (i + 1) / tabs;
            const bool on = (tm::settingsTabIndex(v.tab, tabs) == i);
            const char* name = tm::settingsTabLabel(v.tabs, tabs, i);
            const Font* f = fontFor(Face::Meta);
            const int tw = (f && f->ready()) ? f->textWidth(name) : 40;
            drawText(s, Face::Meta, (x0 + x1) / 2 - tw / 2,
                     textBaseline(s, Face::Meta, tab_top, kSetTabH), name, on ? (uint16_t)kAccInk : kDimmer);
            if (on) s.fillRect(x0 + 18, tab_top + kSetTabH - 3, x1 - x0 - 36, 2, kAcc);
        }
        s.hline(0, W, tab_top + kSetTabH, kLine);

        // ---- the current tab's SECTIONS, drawn by the one body renderer a list sub-page also uses.
        const int body_top = kSetBodyTop;
        drawSettingsRows(s, v.sections, v.section_count, v.focus, v.scroll, body_top, kSetBodyH, v.core);
    }

    // ---- the hint bar: this page's own keys; the six page keys live in the cluster on the right.
    // S516/EST-478: the diamond is SETTINGS in that cluster (the old `, settings` chip is gone), and inside
    // a sub-page it returns to the Settings root.
    if (v.sub_page) {
        static const HintItem hints[] = {{"type", "write"}, {"Enter", "apply"}, {"esc", "back"}};
        drawHintBar(s, tm::Page::Settings, hints, 3);
    } else if (v.volume_row) {
        // S514/EST-451: the Volume row adds '+' / '-'. The six-key page cluster leaves even less room than
        // the old five, so on this row the bar carries only the three keys that matter here.
        static const HintItem hints[] = {{"+/-", "volume"}, {"Enter", "change"}, {"esc", "back"}};
        drawHintBar(s, tm::Page::Settings, hints, 3);
    } else {
        static const HintItem hints[] = {{"Enter", "open/change"}, {"esc", "back"}, {"l/r", "tab"},
                                         {"up/dn", "row"}};
        drawHintBar(s, tm::Page::Settings, hints, 4);
    }
}

void drawConfirmSheet(Screen& s, const SettingsView& v) {
    if (!v.confirm) return;
    const int W = kTmW, H = kTmH;

    // A dither scrim (every other line darkened) because Screen has no alpha blend: it makes the sheet
    // modal without inventing a second look. The page behind it still shows the OLD value, as EST-422
    // requires; nothing here paints the pending value into a row.
    for (int y = 0; y < H; y += 2) s.fillRect(0, y, W, 1, kBg);

    const int x0 = 80, y0 = 148, x1 = W - 80, y1 = 340;
    s.fillRect(x0, y0, x1 - x0, y1 - y0, kPanel);
    s.frame(x0, y0, x1 - x0, y1 - y0, 2, kAcc);
    s.hline(x0 + 1, x1 - 1, y0 + 46, kLine);

    drawText(s, Face::Title, x0 + 22, textBaseline(s, Face::Title, y0, 46),
             v.confirm_title != nullptr ? v.confirm_title : "Confirm", kInk);

    // The question, word-wrapped with the real body font. At most three lines: the sheet's body is fixed.
    const Font* f = fontFor(Face::Body);
    const int maxw = x1 - x0 - 44;
    char line[80];
    int  ly = y0 + 66;
    const char* p = v.confirm_text != nullptr ? v.confirm_text : "";
    while (*p && ly < y1 - 62) {
        size_t n = 0;
        size_t last_space = 0;
        line[0] = '\0';
        while (p[n] && n < sizeof(line) - 1) {
            line[n] = p[n];
            line[n + 1] = '\0';
            const int wpx = (f && f->ready()) ? f->textWidth(line) : (int)n * 7;
            if (wpx > maxw && n > 0) {
                if (last_space > 0) {
                    line[last_space] = '\0';
                    n = last_space;
                } else {
                    line[n] = '\0';
                }
                break;
            }
            if (p[n] == ' ') last_space = n;
            ++n;
        }
        drawText(s, Face::Body, x0 + 22, textBaseline(s, Face::Body, ly, 24), line, kInk);
        ly += 24;
        p += n;
        while (*p == ' ') ++p;
    }

    // Cancel on the LEFT, Apply on the right: the L2's EST-422 rule, and the only filled plate on the
    // screen is the applying key. S515/EST-467: the sheet OPENS on Cancel — the gold 2 px ring shows where
    // Enter will land, so a key pressed out of habit cannot delete anything.
    const int bw = 148, bh = 42;
    const int by = y1 - bh - 16;
    const int cx = x1 - 22 - 2 * bw - 12;
    s.fillRect(cx, by, bw, bh, kPanel2);
    s.frame(cx, by, bw, bh, 1, kLine);
    if (v.confirm_cancel) s.frame(cx - 2, by - 2, bw + 4, bh + 4, 2, kAcc);
    drawText(s, Face::Body, cx + 16, textBaseline(s, Face::Body, by, bh), "Cancel", kInk);
    drawKeyCap(s, cx + bw - 46, by + 12, 34, 18, "esc", kDim);

    const int ax = x1 - 22 - bw;
    s.fillRect(ax, by, bw, bh, v.confirm_danger ? (uint16_t)0x5a1a1a : (uint16_t)kSelBg2);
    s.frame(ax, by, bw, bh, 1, v.confirm_danger ? (uint16_t)0xF800 : (uint16_t)kAcc);
    if (!v.confirm_cancel) s.frame(ax - 2, by - 2, bw + 4, bh + 4, 2, kAcc);
    drawText(s, Face::Body, ax + 16, textBaseline(s, Face::Body, by, bh),
             v.confirm_apply != nullptr ? v.confirm_apply : "Apply", kAccInk);
    drawKeyCap(s, ax + bw - 56, by + 12, 44, 18, "Enter", v.confirm_danger ? (uint16_t)0xF800 : kAcc);
}

}  // namespace ui
}  // namespace om
