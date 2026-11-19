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
                 bool cut, uint16_t colour, bool right) {
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
    if (right) {
        drawTextRightClipped(s, face, x, baseline, max_w, buf, colour);
    } else {
        drawTextClipped(s, face, x, baseline, max_w, buf, colour);
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
            drawText(s, Face::Hint, kTmPad + 4, textBaseline(s, Face::Hint, y, 20), stamp, kDimmer);
            if (m.outgoing) {
                drawTextRightClipped(s, Face::Meta, right_x, textBaseline(s, Face::Meta, y, 20), right_x - left_x,
                                     who, kDim);
            } else {
                drawTextClipped(s, Face::Meta, left_x, textBaseline(s, Face::Meta, y, 20), right_x - left_x, who,
                                kAcc);
            }
            if (m.age[0])
                drawTextRight(s, Face::Hint, W - kTmPad, textBaseline(s, Face::Hint, y, 20), m.age, kDimmer);
            for (int l = 0; l < lines; l++) {
                const int  by      = y + kTmChatHeadH + l * kTmChatLineH;
                const bool lastCut = m.clipped && (l == lines - 1);
                drawMsgLine(s, Face::Body, m.outgoing ? right_x : left_x,
                            textBaseline(s, Face::Body, by, kTmChatLineH + 4), right_x - left_x, m.text, m.line[l],
                            lastCut, kInk, m.outgoing);
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
        drawText(s, Face::Body, kTmPad + 12, bl,
                 empty ? (v.dm_open ? "message this contact, Enter sends" : "message the channel, Enter sends")
                       : v.composer,
                 empty ? (uint16_t)kDimmer : (uint16_t)kInk);
        if (!empty) {
            const Font* f = fontFor(Face::Body);
            const int   tw = (f && f->ready()) ? f->textWidth(v.composer) : 0;
            const int   cx = kTmPad + 12 + tw + 3;
            s.fillRect(cx, cy0 + 12, 2, kTmChatComposerH - 24, kAcc);   // caret
        }
        // S514/EST-444 (Filip): no filled "Send" button — the physical key is named in the hint bar's own cap
        // style, "↵ Enter". Empty draft = dim, text = normal ink; Enter sends exactly as before.
        const int bw = 76, bh = 26;
        const int bx = W - kTmPad - bw - 8, by = cy0 + (kTmChatComposerH - bh) / 2;
        drawEnterKeyCap(s, bx, by, bw, bh, empty ? (uint16_t)kDimmer : (uint16_t)kInk);
    }

    static const HintItem hints_list[] = {{",", "settings"}, {"up/dn", "move"}, {"l/r", "column"}, {"ret", "open"}, {"esc", "back"}};
    static const HintItem hints_open[] = {{",", "settings"}, {"type", "write"}, {"up/dn", "scroll"}, {"ret", "send"}, {"esc", "back"}};
    // A DM thread types and sends exactly like a channel (EST-439), so the two open states share one hint bar.
    drawHintBar(s, tm::Page::Chat, v.open ? hints_open : hints_list, 5);
}

}  // namespace ui
}  // namespace om
