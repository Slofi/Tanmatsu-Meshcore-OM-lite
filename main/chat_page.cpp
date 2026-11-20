// SPDX-License-Identifier: MIT
#include "chat_page.h"

#include <stdio.h>
#include <string.h>

#include "chrome.h"
#include "launcher_page.h"   // drawText* / textBaseline
#include "nodes_page.h"      // drawTextClipped
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"

namespace om {
namespace ui {
namespace {

// ---- rounded plates (S513/EST-430) ------------------------------------------------------------------------
// The rounded fill and the card plate now live in chrome.cpp (drawRoundedRect / drawCardPlate), because the
// start menu's three app cards (S513/EST-432) draw the same style: one implementation, not two that can drift.
// One card of the list: name (left, clipped), unread badge (right, only when > 0), then the newest message as
// "sender: text" with its local HH:MM on the right. Both lines stay inside the card's padding.
void drawChatCard(Screen& s, const ChatRowView& r, const mc::CardRect& rect, bool focused) {
    drawCardPlate(s, rect.x, rect.y, rect.w, rect.h, kTmCardRadius, focused ? kTmRingW : 1,
                  focused ? (uint16_t)kAcc : (uint16_t)kLine, focused ? (uint16_t)kSelBg : (uint16_t)kPanel);

    const int pad  = 12;
    const int x0   = rect.x + pad;
    const int x1   = rect.x + rect.w - pad;
    const int name_w = x1 - x0 - (r.unread ? 26 : 0);   // the badge owns the right end of the first line

    drawTextClipped(s, Face::Row, x0, textBaseline(s, Face::Row, rect.y + 6, 28), name_w, r.title,
                    focused ? (uint16_t)kAccInk : (uint16_t)kInk);

    if (r.unread) {
        // the L2's unread badge, unchanged: a filled red disc with the count, at the card's top-right
        const int bx = x1 - 9, by = rect.y + 6 + 14;
        s.disc(bx, by, 9, kBad);
        drawTextCentred(s, Face::Hint, bx, textBaseline(s, Face::Hint, by - 9, 18), r.left, kWhite);
    }

    // second line: the preview clipped, the time right-aligned in the same dim colour family as the L2's meta
    const int time_w = 44;
    drawTextClipped(s, Face::Meta, x0, textBaseline(s, Face::Meta, rect.y + 36, 24), (x1 - time_w - 8) - x0, r.body,
                    kDim);
    if (r.right[0]) drawTextRight(s, Face::Hint, x1, textBaseline(s, Face::Hint, rect.y + 38, 22), r.right, kDimmer);
}

// Draw one wrapped line: a byte range of the message, copied into a small scratch buffer because the draw
// helpers take C strings. A line whose message was cut gets a plain "..." — the font subset has no ellipsis
// glyph, and a row that ends mid-sentence with nothing after it reads like a bug.
void drawMsgLine(Screen& s, Face face, int x, int baseline, int max_w, const char* text, const mc::Line& ln,
                 bool cut, uint16_t colour, bool right, const MentionSpan* spans, int span_count,
                 uint16_t mention_colour) {
    char   buf[mc::kTextBytes + 4];
    size_t n = ln.len;
    if (n > sizeof(buf) - 4) n = sizeof(buf) - 4;
    if (text != nullptr && n > 0) memcpy(buf, text + ln.start, n);
    if (cut) {
        buf[n++] = '.';
        buf[n++] = '.';
        buf[n++] = '.';
    }
    buf[n] = '\0';

    const Font* f = fontFor(face);
    if (spans == nullptr || span_count <= 0 || f == nullptr || !f->ready()) {
        if (right) {
            drawTextRightClipped(s, face, x, baseline, max_w, buf, colour);
        } else {
            drawTextClipped(s, face, x, baseline, max_w, buf, colour);
        }
        return;
    }

    // S516/EST-481: the line carries mention spans (byte ranges in the DRAWN text, which `text` already is).
    // The pieces are drawn one after another at the font's own advance widths, so the accent starts and stops
    // on the mention's exact bytes. The wrap is measured over this SAME drawn text (main.cpp), so the line fits
    // exactly as measured: the hidden brackets cannot shift a break the drawing does not also have.
    int    cx = right ? (x - f->textWidth(buf)) : x;
    size_t at = 0;
    auto   piece = [&](size_t a, size_t b, uint16_t ink) {
        if (b <= a) return;
        char seg[mc::kTextBytes + 4];
        size_t m = b - a;
        if (m > sizeof(seg) - 1) m = sizeof(seg) - 1;
        memcpy(seg, buf + a, m);
        seg[m] = '\0';
        drawTextClipped(s, face, cx, baseline, max_w, seg, ink);
        cx += f->textWidth(seg);
    };
    for (int i = 0; i < span_count; ++i) {
        const long lo = (long)spans[i].start - (long)ln.start;
        const long hi = lo + (long)spans[i].len;
        if (hi <= 0 || lo >= (long)n) continue;
        const size_t a = (size_t)(lo < 0 ? 0 : lo);
        const size_t b = (size_t)(hi > (long)n ? (long)n : hi);
        if (a > at) piece(at, a, colour);
        piece(a, b, mention_colour);
        at = b;
    }
    if (at < n) piece(at, n, colour);
}

// --------------------------------------------------------------------------------- the message sheets (S515/EST-469)
// ONE bottom sheet, the node menu's house style: a dither scrim (Screen has no alpha blend), a panel with the gold
// 2 px outline, a two-line head, then one row per line. The message menu draws its rows focusable; the Info sheet
// passes no focus at all. One implementation, so the app cannot grow two menu looks.
struct SheetLine {
    const char* label   = "";
    const char* value   = "";
    bool        dim     = false;
    bool        focused = false;
};

void drawChatSheet(Screen& s, const char* title, const char* sub, const SheetLine* rows, int count, int pad,
                   int head_h, int row_h) {
    if (rows == nullptr || count <= 0) return;
    const int W = kTmW, H = kTmH;
    for (int y = 0; y < H; y += 2) s.fillRect(0, y, W, 1, kBg);

    const int sheet_w = 620;
    const int sheet_h = 2 * pad + head_h + count * row_h;
    const int x0      = (W - sheet_w) / 2;
    const int x1      = x0 + sheet_w;
    int       y0      = H - kTmHintH - 8 - sheet_h;
    if (y0 < 4) y0 = 4;

    s.fillRect(x0, y0, sheet_w, sheet_h, kPanel);
    s.frame(x0, y0, sheet_w, sheet_h, 2, kAcc);
    drawTextClipped(s, Face::Title, x0 + 20, textBaseline(s, Face::Title, y0 + pad, 34), sheet_w - 40,
                    title != nullptr ? title : "", kInk);
    drawTextClipped(s, Face::Meta, x0 + 20, textBaseline(s, Face::Meta, y0 + pad + 34, 22), sheet_w - 40,
                    sub != nullptr ? sub : "", kDim);
    s.hline(x0 + 1, x1 - 1, y0 + pad + head_h - 1, kLine);

    int ry = y0 + pad + head_h;
    for (int i = 0; i < count; ++i) {
        const SheetLine& r = rows[i];
        if (r.focused) {
            s.fillRect(x0 + 8, ry + 1, sheet_w - 16, row_h - 3, kSelBg);
            s.frame(x0 + 8, ry + 1, sheet_w - 16, row_h - 3, kTmRingW, kAcc);
        }
        // A dim row keeps its reason on the right (the node menu's own rule); focus still lands on it so the
        // reason can be read, and Enter runs nothing.
        drawText(s, Face::Row, x0 + 24, textBaseline(s, Face::Row, ry, row_h), r.label,
                 r.focused ? (uint16_t)kAccInk : (r.dim ? (uint16_t)kDimmer : (uint16_t)kInk));
        if (r.value[0] != '\0')
            drawTextRight(s, Face::Hint, x1 - 24, textBaseline(s, Face::Hint, ry, row_h), r.value,
                          r.dim ? (uint16_t)kDimmer : (uint16_t)kDim);
        if (i + 1 < count) s.hline(x0 + 8, x1 - 8, ry + row_h - 1, kRowLine);
        ry += row_h;
    }
}

// --------------------------------------------------------------------- the name picker (S516/EST-481)
// The suggestion pop-up sits just ABOVE the composer (Filip: "suggestion is a pop-up window with nodes matching
// already typed letters"). It is NOT modal: the composer keeps the caret between the brackets and every key still
// goes to the composer's mention mode. Each row is the name and a short tag (fav / radio); a plain SD/session row
// carries no tag. The house focus look is the selection box's: kSelBg inside a 2 px gold ring.
void drawMentionPicker(Screen& s, const ChatView& v, int composer_top) {
    if (!v.mention_pick || v.mention_count <= 0) return;
    const int row_h = 30;
    const int pad   = 8;
    const int pw    = 460;
    const int x0    = kTmPad;
    int       count = v.mention_count;
    if (count > om::ui::kMentionPickRows) count = om::ui::kMentionPickRows;
    const int ph = 2 * pad + count * row_h;
    int       y1 = composer_top - 6;
    int       y0 = y1 - ph;
    if (y0 < kTmChatTop) y0 = kTmChatTop;   // never over the page's title line

    s.fillRect(x0, y0, pw, ph, kPanel);
    s.frame(x0, y0, pw, ph, 2, kAcc);
    for (int i = 0; i < count; ++i) {
        const MentionRow& r  = v.mention_rows[i];
        const int         ry = y0 + pad + i * row_h;
        const bool        on = (i == v.mention_focus);
        if (on) {
            s.fillRect(x0 + 6, ry + 1, pw - 12, row_h - 3, kSelBg);
            s.frame(x0 + 6, ry + 1, pw - 12, row_h - 3, kTmRingW, kAcc);
        }
        drawTextClipped(s, Face::Row, x0 + 16, textBaseline(s, Face::Row, ry, row_h), pw - 120, r.name,
                        on ? (uint16_t)kAccInk : (uint16_t)kInk);
        const char* tag = r.favourite ? "fav" : (r.in_radio ? "radio" : "");
        if (tag[0] != '\0')
            drawTextRight(s, Face::Hint, x0 + pw - 16, textBaseline(s, Face::Hint, ry, row_h), tag,
                          on ? (uint16_t)kAccInk : (uint16_t)kDim);
        if (i + 1 < count) s.hline(x0 + 8, x0 + pw - 8, ry + row_h - 1, kRowLine);
    }
}

}  // namespace

mc::CardGrid chatCardGrid() {
    return mc::cardGrid(kTmPad, kTmCardWorkTop, kTmW - kTmPad, kTmH - kTmHintH, kTmCardGutter, kTmCardHeaderH,
                        kTmCardH, kTmCardGap);
}

void drawChat(Screen& s, const ChatView& v) {
    const int W = kTmW, H = kTmH;
    s.clear((uint16_t)kBg);
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    // In open mode the head's right side says where the window is instead of counting: scrolled back, "how many
    // older / newer" is what the two Up/Down keys are FOR, and a bare count would hide that anything is missing.
    char head_right[96];
    if (v.open) {
        if (v.hidden_above > 0 && v.hidden_below > 0)
            snprintf(head_right, sizeof(head_right), "%d older \xc2\xb7 %d newer", v.hidden_above, v.hidden_below);
        else if (v.hidden_above > 0)
            snprintf(head_right, sizeof(head_right), "%d older", v.hidden_above);
        else if (v.hidden_below > 0)
            snprintf(head_right, sizeof(head_right), "%d newer", v.hidden_below);
        else
            snprintf(head_right, sizeof(head_right), "%d message%s", v.msg_count, v.msg_count == 1 ? "" : "s");
        // S513: a refused send wins the slot for a few seconds — the composer still holds the text, and this is
        // the one place on the page that already says something short-lived.
        if (v.notice != nullptr && v.notice[0] != '\0') snprintf(head_right, sizeof(head_right), "%s", v.notice);
        // S513/EST-440: the open CHANNEL's send scope leads the head's right text when it is not unscoped.
        // "scope #si · 12 messages" / "no scope · ..."; drawPageHead already draws this line in the dim face.
        // A DM thread has no channel scope, and an unscoped channel leaves the head exactly as it was.
        if (!v.dm_open && v.scope[0] != '\0') {
            char with_scope[128];
            snprintf(with_scope, sizeof(with_scope), "%.31s \xc2\xb7 %.90s", v.scope, head_right);
            snprintf(head_right, sizeof(head_right), "%.95s", with_scope);
        }
    } else {
        if (v.dm_count > 0) {
            snprintf(head_right, sizeof(head_right), "%d channel%s \xc2\xb7 %d unread \xc2\xb7 %d DM thread%s",
                     v.channel_count, v.channel_count == 1 ? "" : "s", v.unread_total, v.dm_count,
                     v.dm_count == 1 ? "" : "s");
        } else {
            snprintf(head_right, sizeof(head_right), "%d channel%s \xc2\xb7 %d unread", v.channel_count,
                     v.channel_count == 1 ? "" : "s", v.unread_total);
        }
    }
    const int head_bot = drawPageHead(s, "Chat", head_right);

    const int work_top = head_bot;
    const int work_bot = H - kTmHintH;

    // ============================================================ the list: two columns of cards (S513/EST-430)
    // Channels left, Direct messages right, equal widths, one card per chat. The geometry, the focus and the
    // scroll are all core/chat_cards.* — this block only walks what the model gives it.
    if (!v.open) {
        const mc::CardGrid g = chatCardGrid();
        static const char* const kHeads[mc::kCardCols] = {"Channels", "Direct messages"};
        for (int col = 0; col < mc::kCardCols; col++)
            drawText(s, Face::Meta, g.col_x[col], textBaseline(s, Face::Meta, g.y0, g.header_h), kHeads[col],
                     kDimmer);

        for (int col = 0; col < mc::kCardCols; col++) {
            const ChatRowView* list  = (col == mc::kColChannels) ? v.rows : v.dms;
            const int          count = (col == mc::kColChannels) ? v.row_count : v.dm_count;
            // The page never trusts a scroll it did not compute: clamp it to what this column can actually scroll.
            int scroll = v.scroll[col];
            const int max_scroll = (count > g.visible) ? count - g.visible : 0;
            if (scroll < 0) scroll = 0;
            if (scroll > max_scroll) scroll = max_scroll;

            for (int i = 0; i < count && i < kChatRows; i++) {
                const mc::CardRect rect = mc::cardRect(g, col, i, scroll);
                // A card the scroll has put outside the work box is not drawn, ever: the hint bar is not a
                // drawing area, and a half card would collide with it.
                if (rect.y < g.cards_y || rect.y + rect.h > g.y1) continue;
                drawChatCard(s, list[i], rect, col == v.focus_col && i == v.focus_row);
            }
        }

        // The DM column's empty state: the L2's own line, in the dim colour, and no card.
        if (v.dm_count == 0)
            drawText(s, Face::Body, g.col_x[mc::kColDms] + 12, textBaseline(s, Face::Body, g.cards_y, 24),
                     "No direct messages yet", kDim);
    }

    // ======================================================================================= one chat
    if (v.open) {
        char title[40];
        if (v.dm_open) snprintf(title, sizeof(title), "DM: %s", v.channel_name);
        else snprintf(title, sizeof(title), "# %s", v.channel_name);
        drawTextClipped(s, Face::Row, kTmPad, textBaseline(s, Face::Row, work_top, 34), W - 2 * kTmPad - 120,
                        title, kInk);

        // The window was decided in main.cpp by core/chatWindow (which messages fit, given their wrapped
        // heights), so this only walks what it was handed. Each message is one head line (time + who) and as many
        // body lines as its wrap says — S503: a long bot reply is READ, not cut to one line.
        int y = kTmChatTop + 4;
        for (int i = 0; i < v.msg_count; i++) {
            const ChatView::Msg& m = v.msgs[i];
            const int lines = (m.line_count > 0) ? m.line_count : 1;
            // Never draw into the composer, whatever the caller says.
            if (y + kTmChatHeadH + (lines - 1) * kTmChatLineH + kTmChatLineH > kTmChatBot + kTmChatGapH) break;
            // S515/EST-469: the selection box. The Nodes list's focus look, around the WHOLE message — its head
            // line (time + who) and every body line — drawn under the text so the frame never hides a glyph.
            // Four pixels wider than the text on both sides, because "around that message" has to read as one
            // block and not as two separate marks.
            if (i == v.sel) {
                const int sy = y - 2;
                const int sh = kTmChatHeadH + lines * kTmChatLineH + 2;
                s.fillRect(kTmPad - 4, sy, W - 2 * (kTmPad - 4), sh, kSelBg);
                s.frame(kTmPad - 4, sy, W - 2 * (kTmPad - 4), sh, kTmRingW, kAcc);
            }
            // WHO said it decides WHERE it sits (Filip, S502: "my messages on the right, others on the left, so
            // it is clear who said what"). Incoming hugs the left edge under the time column; our own hugs the
            // right edge, with the age column kept clear. "you" replaces the node name here only — the frame on
            // the air still carries the node's real name.
            const int   left_x  = kTmChatBodyX0;
            const int   right_x = kTmChatBodyX1;
            const char* who     = m.outgoing ? "you" : m.sender;
            // S513/EST-439: our own line carries its delivery state AFTER the time ("09:12 delivered"), because
            // "the radio said it went out" is not the same as "the recipient acknowledged it" (dm_delivery.h).
            char stamp[32];
            if (m.outgoing && m.state[0] != '\0')
                snprintf(stamp, sizeof(stamp), "%s %s", m.hhmm, m.state);
            else
                snprintf(stamp, sizeof(stamp), "%s", m.hhmm);
            const int   base    = textBaseline(s, Face::Hint, y, 20);
            drawText(s, Face::Hint, kTmPad + 4, base, stamp, kDimmer);
            // S515/EST-462: the dim time row, in the SAME ink family as the clock, drawn where it cannot collide:
            // after the sender's own width on an incoming line, and right-aligned just left of "you" on ours. The
            // widths and the gap are core/chat_meta.h's constants, so a host test pins both edges.
            const Font* meta_f = fontFor(Face::Meta);
            const Font* hint_f = fontFor(Face::Hint);
            if (m.outgoing) {
                drawTextRightClipped(s, Face::Meta, right_x, base, right_x - left_x, who, kDim);
                const int you_w = (meta_f != nullptr && meta_f->ready()) ? meta_f->textWidth(who) : 0;
                const int meta_w = om::mc::metaOutgoingMaxW(left_x, right_x, you_w);
                if (m.meta[0] != '\0' && meta_w > 0) {
                    drawTextRightClipped(s, Face::Hint, om::mc::metaOutgoingRight(right_x, you_w), base, meta_w,
                                         m.meta, kDimmer);
                }
            } else {
                drawTextClipped(s, Face::Meta, left_x, base, right_x - left_x, who, kAcc);
                const int who_w = (meta_f != nullptr && meta_f->ready()) ? meta_f->textWidth(who) : 0;
                const int age_w = (hint_f != nullptr && hint_f->ready() && m.age[0] != '\0')
                                      ? hint_f->textWidth(m.age)
                                      : 0;
                const int age_x  = W - kTmPad - age_w;
                const int meta_w = om::mc::metaIncomingMaxW(left_x, who_w, age_x);
                if (m.meta[0] != '\0' && meta_w > 0) {
                    drawTextClipped(s, Face::Hint, om::mc::metaIncomingX(left_x, who_w), base, meta_w, m.meta,
                                    kDimmer);
                }
            }
            if (m.age[0])
                drawTextRight(s, Face::Hint, W - kTmPad, base, m.age, kDimmer);
            for (int l = 0; l < lines; l++) {
                const int  by      = y + kTmChatHeadH + l * kTmChatLineH;
                const bool lastCut = m.clipped && (l == lines - 1);
                drawMsgLine(s, Face::Body, m.outgoing ? right_x : left_x,
                            textBaseline(s, Face::Body, by, kTmChatLineH + 4), right_x - left_x, m.text, m.line[l],
                            lastCut, kInk, m.outgoing, m.mention, m.mention_count, kAcc);
            }
            y += kTmChatHeadH + lines * kTmChatLineH + kTmChatGapH;
        }
        if (v.msg_count == 0) {
            drawText(s, Face::Body, kTmPad + 58, kTmChatTop + 24,
                     v.dm_open ? "No messages in this thread yet." : "No messages on this channel yet.", kDim);
            if (!v.dm_open)
                drawText(s, Face::Body, kTmPad + 58, kTmChatTop + 48, "Type below and press Enter to send.",
                         kDimmer);
        }

        // ---- composer: one line, a caret, and the Send key hint. S513/EST-439: a DM thread gets the SAME
        // composer as a channel (Enter sends, a full queue keeps the text and the head says why).
        const int cy0 = work_bot - kTmChatComposerH - 4;
        s.fillRect(kTmPad, cy0, W - 2 * kTmPad, kTmChatComposerH, kPanel);
        s.frame(kTmPad, cy0, W - 2 * kTmPad, kTmChatComposerH, 1, kLine);
        const int  bl    = textBaseline(s, Face::Body, cy0, kTmChatComposerH);
        const bool empty = (v.composer == nullptr || v.composer[0] == '\0');
        // S516 (Claude, review of EST-481): in mention mode the draft ends in the auto ']' and the caret sits
        // just before it. Drawn as three parts (text before the caret, the caret, then the ']' moved 4 px right)
        // because a caret drawn over the text covered the bracket's thin stroke: "@[Al" read as if the ']'
        // were missing.
        const Font*  f       = fontFor(Face::Body);
        const size_t clen    = empty ? 0 : strlen(v.composer);
        const bool   split   = !empty && v.mention_active && clen > 0 && v.composer[clen - 1] == ']';
        static char  before[256];
        if (split) {
            const size_t k = (clen - 1 < sizeof(before) - 1) ? clen - 1 : sizeof(before) - 1;
            memcpy(before, v.composer, k);
            before[k] = '\0';
        }
        drawText(s, Face::Body, kTmPad + 12, bl,
                 empty ? (v.dm_open ? "message this contact, Enter sends" : "message the channel, Enter sends")
                       : (split ? before : v.composer),
                 empty ? (uint16_t)kDimmer : (uint16_t)kInk);
        if (!empty) {
            const int tw = (f && f->ready()) ? f->textWidth(split ? before : v.composer) : 0;
            const int cx = kTmPad + 12 + tw + (split ? 2 : 3);
            s.fillRect(cx, cy0 + 12, 2, kTmChatComposerH - 24, kAcc);   // caret
            if (split) drawText(s, Face::Body, cx + 4, bl, "]", kInk);
        }
        // S516/EST-481: the suggestion pop-up sits just above the composer, so the pick and the draft are read
        // together. Drawn before the key caps so the caps stay on top of nothing important.
        drawMentionPicker(s, v, cy0);
        // S514/EST-444 (Filip): no filled "Send" button — the physical key is named in the hint bar's own cap
        // style, "↵ Enter". Empty draft = dim, text = normal ink; Enter sends exactly as before.
        const int bw = 76, bh = 26;
        const int bx = W - kTmPad - bw - 8, by = cy0 + (kTmChatComposerH - bh) / 2;
        drawEnterKeyCap(s, bx, by, bw, bh, empty ? (uint16_t)kDimmer : (uint16_t)kInk);

        // ---- S515/EST-469: the message menu and the Info sheet, in the node menu's house style, above the page
        // (the composer goes under the scrim, so it is plain that typing is off while a sheet is up). The hint bar
        // is drawn after this, so its keys stay readable.
        if (v.info_open && v.info_count > 0) {
            static SheetLine lines[kMaxInfoRows];
            for (int i = 0; i < v.info_count && i < kMaxInfoRows; ++i) {
                lines[i].label   = v.info[i].label;
                lines[i].value   = v.info[i].value;
                lines[i].dim     = false;
                lines[i].focused = false;
            }
            // 14 rows of 26 px + the 56 px head + 12 px pads is 444 px: the tallest sheet that still clears the
            // hint bar at 800x480, which is why kMaxInfoRows is what it is (the builder makes "+N more" fit).
            drawChatSheet(s, v.info_title, v.info_sub, lines, v.info_count, 12, 56, 26);
        } else if (v.menu_open && v.menu_count > 0) {
            static SheetLine lines[kMaxChatMenuRows];
            for (int i = 0; i < v.menu_count && i < kMaxChatMenuRows; ++i) {
                lines[i].label   = v.menu[i].label;
                lines[i].value   = v.menu[i].value;
                lines[i].dim     = v.menu[i].dim;
                lines[i].focused = (i == v.menu_focus);
            }
            drawChatSheet(s, v.menu_title, v.menu_sub, lines, v.menu_count, 16, 64, 42);
        }
    }

    // S516/EST-478: Settings is the diamond in the page-key cluster; the old `, settings` chip is gone.
    static const HintItem hints_list[] = {{"Enter", "open"}, {"esc", "back"}, {"up/dn", "move"}, {"l/r", "column"}};
    static const HintItem hints_open[] = {{"Enter", "send"}, {"esc", "back"}, {"type", "write"}};
    // S515/EST-469: three states of the open chat, each with the keys that actually work there. Up/Down no
    // longer SCROLL — that was S503's "up/dn scroll". Up enters the selection box, and the box is what moves.
    static const HintItem hints_sel[]  = {{"up/dn", "move"}, {"Enter", "menu"}, {"esc", "type"}};
    static const HintItem hints_menu[] = {{"up/dn", "move"}, {"Enter", "choose"}, {"esc", "close"}};
    static const HintItem hints_info[] = {{"esc", "close"}};
    // S516/EST-481: mention mode has its own keys (and they are the ones that actually work: Enter picks, it
    // does NOT send). Three chips, the same count as the normal composer's bar, so nothing is squeezed.
    static const HintItem hints_mention[] = {{"up/dn", "choose"}, {"Enter", "pick"}, {"esc", "close"}};
    // A DM thread types and sends exactly like a channel (EST-439), so the two open states share one hint bar.
    if (!v.open)
        drawHintBar(s, tm::Page::Chat, hints_list, 4);
    else if (v.info_open)
        drawHintBar(s, tm::Page::Chat, hints_info, 1);
    else if (v.menu_open)
        drawHintBar(s, tm::Page::Chat, hints_menu, 3);
    else if (v.sel >= 0)
        drawHintBar(s, tm::Page::Chat, hints_sel, 3);
    else if (v.mention_active)
        drawHintBar(s, tm::Page::Chat, hints_mention, 3);
    else
        drawHintBar(s, tm::Page::Chat, hints_open, 3);
}

}  // namespace ui
}  // namespace om
