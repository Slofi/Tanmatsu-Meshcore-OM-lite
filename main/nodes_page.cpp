// SPDX-License-Identifier: MIT
#include "nodes_page.h"

#include <stdio.h>
#include <string.h>

#include "chrome.h"
#include "launcher_page.h"   // drawText* / textBaseline
#include "node_menu.h"       // S514/EST-455: the rows the node menu offers
#include "nodes_search.h"    // S514/EST-449: the "N of M" text (one home for the rule)
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"

namespace om {
namespace ui {
namespace {

constexpr int kNodesRowH = 48;   // two lines: name, then "direct" / "path 4". The preview's 7 rows at 800x480.
constexpr int kPaneGap   = 8;

// The L2's dot colours: client blue, repeater green, room amber, sensor dim. One switch, all pages.
uint16_t dotColour(uint8_t type) {
    switch (type) {
        case 1: return kClient;     // chat / companion
        case 2: return kGreen;      // repeater
        case 3: return kWarn;       // room
        default: return kDimmer;    // unknown / sensor
    }
}

const char* typeName(uint8_t type) {
    switch (type) {
        case 1: return "client";
        case 2: return "repeater";
        case 3: return "room";
        case 4: return "sensor";
        default: return "unknown";
    }
}

void keyPrefix(char* out, size_t n, const uint8_t pub[32]) {
    // "4b 91 2e \xe2\x80\xa6 c7": the first three bytes and the last one, which is what the L2 shows in its detail pane.
    snprintf(out, n, "%02x %02x %02x \xe2\x80\xa6 %02x", pub[0], pub[1], pub[2], pub[31]);
}

}  // namespace

namespace {

// The longest UTF-8 prefix of the string that fits a pixel width, with an ellipsis, written into buf.
// One place decides
// how a long name or a long message ends, so the left-aligned and the right-aligned draw cannot disagree.
const char* clipInto(char* buf, size_t n, const Font* f, const char* utf8, int max_w) {
    if (n == 0) return "";
    buf[0] = '\0';
    if (utf8 == nullptr || max_w <= 0 || f == nullptr || !f->ready()) return buf;
    if (f->textWidth(utf8) <= max_w) {
        snprintf(buf, n, "%s", utf8);
        return buf;
    }
    const int ell_w = f->textWidth("\xe2\x80\xa6");   // "\xe2\x80\xa6"
    size_t    keep = 0, i = 0;
    while (utf8[i] != '\0') {
        const unsigned char c = (unsigned char)utf8[i];
        size_t step = 1;
        if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        if (keep + step + 3 >= n) break;
        memcpy(&buf[keep], &utf8[i], step);
        buf[keep + step] = '\0';
        if (f->textWidth(buf) + ell_w > max_w) break;
        keep += step;
        i += step;
    }
    memcpy(&buf[keep], "\xe2\x80\xa6", 3);
    buf[keep + 3] = '\0';
    return buf;
}

}  // namespace

void drawTextClipped(Screen& s, Face face, int x, int baseline, int max_w, const char* utf8, uint16_t colour) {
    const Font* f = fontFor(face);
    if (!f || !f->ready() || utf8 == nullptr || max_w <= 0) return;
    char buf[256];
    f->draw(s.fb(), s.width(), s.height(), x, baseline, clipInto(buf, sizeof(buf), f, utf8, max_w), colour);
}

void drawTextRightClipped(Screen& s, Face face, int x_right, int baseline, int max_w, const char* utf8,
                          uint16_t colour) {
    const Font* f = fontFor(face);
    if (!f || !f->ready() || utf8 == nullptr || max_w <= 0) return;
    char        buf[256];
    const char* text = clipInto(buf, sizeof(buf), f, utf8, max_w);
    f->draw(s.fb(), s.width(), s.height(), x_right - f->textWidth(text), baseline, text, colour);
}

void drawNodes(Screen& s, const NodesView& v) {
    const int W = kTmW, H = kTmH;
    s.clear((uint16_t)kBg);
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    char head_right[64];
    snprintf(head_right, sizeof(head_right), "%d node%s heard \xc2\xb7 %d in the last hour",
             (int)(v.store ? v.store->count : 0), (v.store && v.store->count == 1) ? "" : "s", v.in_last_hour);
    const int head_bot = drawPageHead(s, "Nodes", head_right);

    const int list_x0 = kTmPad;
    const int list_x1 = kTmPad + kTmListW;
    const int work_top = head_bot;
    const int work_bot = H - kTmHintH;

    // ---- S514/EST-449: the search bar is the list's TOP ROW, always visible.
    // ONE field (Filip's L2 correction), the query on the left and "N of M" on the right, both clear of the
    // border. The generated faces carry Latin-1 + Latin Extended-A only and have no magnifier glyph, so the
    // bar draws none; the dim placeholder says what the field is.
    const int bar_x0 = list_x0;
    const int bar_x1 = list_x1 - kTmRingInset;
    const int bar_w  = bar_x1 - bar_x0;
    const int bar_y  = work_top + 4;
    const int bar_h  = 30;
    s.fillRect(bar_x0, bar_y, bar_w, bar_h, kFieldBg);
    s.frame(bar_x0, bar_y, bar_w, bar_h, v.search_focused ? (int)kTmRingW : 1,
            v.search_focused ? (uint16_t)kAcc : (uint16_t)kLine);

    const char* query     = (v.query != nullptr) ? v.query : "";
    const bool  has_query = query[0] != 0;
    drawTextClipped(s, Face::Body, bar_x0 + 12, textBaseline(s, Face::Body, bar_y, bar_h),
                    bar_w - 12 - 84, has_query ? query : "search nodes",
                    has_query ? (uint16_t)kInk : (uint16_t)kDimmer);
    char count[32];
    nodeSearchCountText(count, sizeof(count), v.order_count, (int)(v.store ? v.store->count : 0));
    drawTextRight(s, Face::Hint, bar_x1 - 12, textBaseline(s, Face::Hint, bar_y, bar_h), count, kDim);

    int       y       = bar_y + bar_h + 6;
    const int rows_top = y;
    const int visible = (work_bot - 8 - y) / kNodesRowH;

    // An empty FILTERED result says so in the list area; an empty contact table is the right pane's
    // "No nodes yet", so the two cannot be confused.
    if (v.order_count == 0 && v.store != nullptr && v.store->count > 0) {
        drawTextCentred(s, Face::Body, (list_x0 + list_x1) / 2, rows_top + (work_bot - 8 - rows_top) / 2,
                        "No matching nodes", kDim);
    }
    // Keep the focused row on screen without moving the list until it must: the same rule as the Launcher.
    int first = 0;
    if (v.focused >= visible) first = v.focused - visible + 1;
    if (first < 0) first = 0;

    for (int i = first; i < v.order_count && y + kNodesRowH <= work_bot - 8; i++) {
        const mc::Contact& c  = v.store->c[v.order[i]];
        const bool         f  = (i == v.focused) && !v.search_focused;
        if (f) {
            s.fillRect(list_x0, y, list_x1 - kTmRingInset - list_x0, kNodesRowH - 2, kSelBg);
            s.frame(list_x0, y, list_x1 - kTmRingInset - list_x0, kNodesRowH - 2, kTmRingW, kAcc);
        }
        s.disc(list_x0 + 12, y + kNodesRowH / 2 - 1, 4, dotColour(c.type));
        drawTextClipped(s, Face::Row, list_x0 + 28, textBaseline(s, Face::Row, y + 2, 28),
                        list_x1 - kTmRingInset - 28 - list_x0 - 80, c.name[0] ? c.name : "(no name)",
                        f ? (uint16_t)kAccInk : (uint16_t)kInk);
        // S514/EST-455: the favourite mark. The generated faces carry ASCII + Latin-1/Latin Extended-A only,
        // so there is no U+2605 star; a small gold dot is the mark that can actually be drawn.
        if (c.favourite) s.disc(list_x1 - kTmRingInset - 74, y + 16, 3, kAcc);
        // second line: how it reached us
        char sub[24];
        if (c.path_len == 0) snprintf(sub, sizeof(sub), "direct");
        else snprintf(sub, sizeof(sub), "path %u", (unsigned)c.path_len);
        drawText(s, Face::Meta, list_x0 + 28, textBaseline(s, Face::Meta, y + 26, 20), sub, kDim);
        // right edge: age, then the SNR the radio measured
        char age[8], snr[16];
        mc::formatAge(v.now, c.heard_at, age, sizeof(age));
        snprintf(snr, sizeof(snr), "%.1f dB", c.snr_x10 / 10.0f);
        drawTextRight(s, Face::Hint, list_x1 - kTmRingInset - 6, textBaseline(s, Face::Hint, y + 4, 20), age,
                      kDim);
        drawTextRight(s, Face::Hint, list_x1 - kTmRingInset - 6, textBaseline(s, Face::Hint, y + 24, 20), snr,
                      kDim);
        if (i + 1 < v.order_count) s.hline(list_x0, list_x1 - kTmRingInset, y + kNodesRowH - 1, kRowLine);
        y += kNodesRowH;
    }

    // The scroll indicator: only when there is something above or below, like the L2's gold thumb.
    if (v.order_count > visible && visible > 0) {
        const int track_top = rows_top, track_bot = work_bot - 8;
        const int track_h   = track_bot - track_top;
        const int thumb_h   = track_h * visible / (v.order_count > visible ? v.order_count : 1);
        const int max_first = v.order_count - visible;
        const int thumb_y   = track_top + (max_first > 0 ? (track_h - thumb_h) * first / max_first : 0);
        s.fillRect(list_x1 - 6, track_top, 3, track_h, kPanel);
        s.fillRect(list_x1 - 6, thumb_y, 3, thumb_h < kTmScrollMin ? kTmScrollMin : thumb_h, kAcc);
    }

    // ---- right pane: the focused node
    s.vline(list_x1 + kPaneGap, work_top, work_bot - 8, kLine);
    const int rx = list_x1 + kPaneGap + kTmPad;
    s.fillRect(rx - kTmPad + 4, work_top + 1, W - (rx - kTmPad + 4), work_bot - work_top - 2, kScreen);

    if (v.store == nullptr || v.store->count == 0) {
        drawText(s, Face::Title, rx, textBaseline(s, Face::Title, work_top, 44), "No nodes yet", kInk);
        drawText(s, Face::Body, rx, work_top + 80, "An advert from another node fills this page.", kDim);
        drawText(s, Face::Body, rx, work_top + 104, "Ours goes out at start and every 3 hours;", kDimmer);
        drawText(s, Face::Body, rx, work_top + 128, "\"a\" forces one now.", kDimmer);
    } else if (v.order_count == 0) {
        // The FILTER matched nothing. The list area already says "No matching nodes"; the detail pane stays
        // quiet instead of claiming the contact table is empty.
    } else {
        const mc::Contact& c = v.store->c[v.order[v.focused < v.order_count ? v.focused : 0]];
        drawTextClipped(s, Face::Title, rx, textBaseline(s, Face::Title, work_top, 44), W - rx - kTmPad, 
                        c.name[0] ? c.name : "(no name)", kInk);
        char key[32];
        keyPrefix(key, sizeof(key), c.pub);
        char meta[64];
        snprintf(meta, sizeof(meta), "%s \xc2\xb7 key %s%s", typeName(c.type), key, c.favourite ? " \xc2\xb7 favourite" : "");
        drawText(s, Face::Meta, rx, textBaseline(s, Face::Meta, work_top + 36, 24), meta, kDim);

        char age[8], hops[24], sig[40], heard[40], path_s[32];
        mc::formatAge(v.now, c.heard_at, age, sizeof(age));
        if (c.path_len == 0) snprintf(hops, sizeof(hops), "direct \xc2\xb7 0 hops");
        else snprintf(hops, sizeof(hops), "%u byte%s of path", (unsigned)c.path_len, c.path_len == 1 ? "" : "s");
        snprintf(sig, sizeof(sig), "%.1f dB / %d dBm", c.snr_x10 / 10.0f, (int)c.rssi);
        snprintf(heard, sizeof(heard), "%s ago", age);
        snprintf(path_s, sizeof(path_s), "%s", c.path_len == 0 ? "direct" : "via a repeater");

        static const char* labels[6] = {"TYPE", "HOPS", "SNR / RSSI", "LAST HEARD", "PUBLIC KEY", "PATH"};
        const char*        values[6] = {typeName(c.type), hops, sig, heard, key, path_s};
        int                kv_y      = work_top + 76;
        for (int i = 0; i < 6; i++) {
            drawText(s, Face::Kv, rx, textBaseline(s, Face::Kv, kv_y, 30), labels[i], kDimmer);
            drawTextClipped(s, Face::Body, rx + 190, textBaseline(s, Face::Body, kv_y, 30),
                            W - (rx + 190) - kTmPad, values[i], kInk);
            kv_y += 30;
        }
        // S514/EST-455: the menu is where the actions live now; 'f' is the shortcut that skips it.
        drawText(s, Face::Body, rx, kv_y + 26, "Enter: actions for this node.", kDimmer);
        drawText(s, Face::Body, rx, kv_y + 48, "f: favourite. DM from the menu or the Chat page.", kDimmer);
    }

    // S514/EST-455: the node menu, drawn over the page (a dither scrim + bottom sheet, the confirm sheet's
    // house style). It belongs to the FOCUSED row: a toggle re-sorts the list and moves the focus with the node.
    if (v.menu_open && v.order_count > 0 && v.store != nullptr) {
        const mc::Contact& c = v.store->c[v.order[v.focused >= 0 && v.focused < v.order_count ? v.focused : 0]];
        drawNodeMenu(s, c, v.menu_focus, v.now, v.menu_ping_pending, v.menu_trace_pending, v.trace);
    } else if (v.trace != nullptr) {
        // S514/EST-456: the trace result outlives the sheet that started it (the L2's TraceView is its own list).
        drawNodeMenu(s, v.store->c[v.order[v.focused >= 0 && v.focused < v.order_count ? v.focused : 0]],
                     v.menu_focus, v.now, false, false, v.trace);
    }

    // S513/EST-432: the advert key works on this page too, so it is a hint and not only the empty-list line.
    // S514/EST-449: `/` opens the search, and while the BAR has the focus it gets its own (shorter) hints.
    // S514/EST-455: with the menu up, the same three keys describe the sheet (Esc closes it).
    static const HintItem hints_list[] = {{"ret", "menu"}, {"f", "favourite"}, {",", "settings"},
                                          {"/", "search"}, {"a", "advert"}, {"esc", "back"}};
    static const HintItem hints_search[] = {{"type", "search"}, {"dn", "list"}, {"esc", "clear/back"}};
    static const HintItem hints_menu[]   = {{"up/dn", "move"}, {"ret", "choose"}, {"esc", "close"}};
    if (v.menu_open)
        drawHintBar(s, tm::Page::Nodes, hints_menu, 3);
    else if (v.search_focused)
        drawHintBar(s, tm::Page::Nodes, hints_search, 3);
    else
        drawHintBar(s, tm::Page::Nodes, hints_list, 6);
}

void drawNodeMenu(Screen& s, const mc::Contact& c, int focus, uint32_t now, bool ping_pending,
                  bool trace_pending, const ops::TraceView* trace) {
    const int W = kTmW, H = kTmH;

    NodeMenuContext ctx;
    ctx.advType      = c.type;
    ctx.hasKey       = !mc::contactKeyIncomplete(c.pub);
    ctx.favourite    = c.favourite;
    ctx.pathKnown    = mc::contactHasOutPath(c);
    ctx.pingPending  = ping_pending;
    ctx.tracePending = trace_pending;
    NodeMenuRow rows[kMaxNodeMenuRows];
    const int   row_count = nodeMenuRows(ctx, rows, kMaxNodeMenuRows);
    if (row_count <= 0) return;

    // S514/EST-456: the trace result replaces the menu's rows (the L2's TraceView): one line per hop with its SNR,
    // then the SNR at this node, then the total.
    if (trace != nullptr) {
        const int shown = trace->count + 2;
        for (int y = 0; y < H; y += 2) s.fillRect(0, y, W, 1, kBg);
        const int sheet_w = 560;
        const int row_h   = 42;
        const int head_h  = 64;
        const int pad     = 16;
        const int count   = shown > 12 ? 12 : shown;
        const int sheet_h = 2 * pad + head_h + count * row_h;
        const int x0      = (W - sheet_w) / 2;
        const int x1      = x0 + sheet_w;
        const int y0      = H - kTmHintH - 8 - sheet_h;
        s.fillRect(x0, y0, sheet_w, sheet_h, kPanel);
        s.frame(x0, y0, sheet_w, sheet_h, 2, kAcc);
        drawTextClipped(s, Face::Title, x0 + 20, textBaseline(s, Face::Title, y0 + pad, 34), sheet_w - 40,
                        trace->node[0] ? trace->node : "Trace", kInk);
        char total[48];
        ops::traceTotalText(total, sizeof(total), *trace);
        drawTextClipped(s, Face::Meta, x0 + 20, textBaseline(s, Face::Meta, y0 + pad + 34, 22), sheet_w - 40,
                        total, kDim);
        s.hline(x0 + 1, x1 - 1, y0 + pad + head_h - 1, kLine);
        int ry = y0 + pad + head_h;
        for (int i = 0; i < count; ++i) {
            char label[64] = "";
            char value[24] = "";
            if (i < trace->count) {
                ops::traceHopLabel(label, sizeof(label), trace->hops[i]);
                snprintf(value, sizeof(value), "SNR %.1f", (double)trace->hops[i].snr4 / 4.0);
            } else if (i == trace->count) {
                snprintf(label, sizeof(label), "at this node");
                snprintf(value, sizeof(value), "SNR %.1f", (double)trace->finalSnr4 / 4.0);
            } else {
                snprintf(label, sizeof(label), "%s", total);
            }
            drawTextClipped(s, Face::Row, x0 + 24, textBaseline(s, Face::Row, ry, row_h), sheet_w - 48 - 120,
                            label, kInk);
            if (value[0] != '\0')
                drawTextRight(s, Face::Hint, x1 - 24, textBaseline(s, Face::Hint, ry, row_h), value, kDim);
            if (i + 1 < count) s.hline(x0 + 8, x1 - 8, ry + row_h - 1, kRowLine);
            ry += row_h;
        }
        return;
    }
    if (focus < 0) focus = 0;
    if (focus >= row_count) focus = row_count - 1;

    // The confirm sheet's modal look: a dither scrim (Screen has no alpha blend), then a plate with the gold
    // 2 px outline. The hint bar is drawn after this function, so the keys stay readable.
    for (int y = 0; y < H; y += 2) s.fillRect(0, y, W, 1, kBg);

    const int sheet_w = 560;
    const int row_h   = 42;
    const int head_h  = 64;
    const int pad     = 16;
    const int sheet_h = 2 * pad + head_h + row_count * row_h;
    const int x0      = (W - sheet_w) / 2;
    const int x1      = x0 + sheet_w;
    const int y0      = H - kTmHintH - 8 - sheet_h;

    s.fillRect(x0, y0, sheet_w, sheet_h, kPanel);
    s.frame(x0, y0, sheet_w, sheet_h, 2, kAcc);

    drawTextClipped(s, Face::Title, x0 + 20, textBaseline(s, Face::Title, y0 + pad, 34),
                    sheet_w - 40, c.name[0] ? c.name : "(no name)", kInk);
    char meta[96];
    char age[12];
    mc::formatAge(now, c.heard_at, age, sizeof(age));
    snprintf(meta, sizeof(meta), "%s \xc2\xb7 last heard %s ago%s", typeName(c.type), age,
             c.favourite ? " \xc2\xb7 favourite" : "");
    drawTextClipped(s, Face::Meta, x0 + 20, textBaseline(s, Face::Meta, y0 + pad + 34, 22),
                    sheet_w - 40, meta, kDim);
    s.hline(x0 + 1, x1 - 1, y0 + pad + head_h - 1, kLine);

    int ry = y0 + pad + head_h;
    for (int i = 0; i < row_count; ++i) {
        const bool f = (i == focus);
        if (f) {
            s.fillRect(x0 + 8, ry + 1, sheet_w - 16, row_h - 3, kSelBg);
            s.frame(x0 + 8, ry + 1, sheet_w - 16, row_h - 3, kTmRingW, kAcc);
        }
        drawText(s, Face::Row, x0 + 24, textBaseline(s, Face::Row, ry, row_h), rows[i].label,
                 f ? (uint16_t)kAccInk : (uint16_t)kInk);
        if (rows[i].value[0] != '\0') {
            drawTextRight(s, Face::Hint, x1 - 24, textBaseline(s, Face::Hint, ry, row_h), rows[i].value,
                          rows[i].valueAccent ? (uint16_t)kAcc : (uint16_t)kDim);
        }
        if (i + 1 < row_count) s.hline(x0 + 8, x1 - 8, ry + row_h - 1, kRowLine);
        ry += row_h;
    }
}

}  // namespace ui
}  // namespace om
