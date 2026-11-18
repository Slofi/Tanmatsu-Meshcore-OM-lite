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

constexpr int kListRowH = 64;   // the channel list keeps the Launcher's row height: one row is one channel

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

void drawChat(Screen& s, const ChatView& v) {
    const int W = kTmW, H = kTmH;
    s.clear((uint16_t)kBg);
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    // In open mode the head's right side says where the window is instead of counting: scrolled back, "how many
    // older / newer" is what the two Up/Down keys are FOR, and a bare count would hide that anything is missing.
    char head_right[64];
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
    } else {
        snprintf(head_right, sizeof(head_right), "%d channel%s \xc2\xb7 %d unread", v.channel_count,
                 v.channel_count == 1 ? "" : "s", v.unread_total);
    }
    const int head_bot = drawPageHead(s, "Chat", head_right);

    const int list_x0 = kTmPad;
    const int list_x1 = kTmPad + kTmListW;
    const int work_top = head_bot;
    const int work_bot = H - kTmHintH;

    // ===================================================================================== the channel list
    if (!v.open) {
        drawText(s, Face::Meta, list_x0, work_top + 24, "CHANNELS", kDimmer);
        char count[32];
        snprintf(count, sizeof(count), "%d chats", v.row_count);
        drawTextRight(s, Face::Hint, list_x1 - kTmRingInset, work_top + 24, count, kDimmer);

        int y = work_top + 34;
        for (int i = 0; i < v.row_count && y + kListRowH <= work_bot - 8; i++) {
            const ChatRowView& r = v.rows[i];
            const bool         f = (i == v.focused);
            if (f) {
                s.fillRect(list_x0, y, list_x1 - kTmRingInset - list_x0, kListRowH - 2, kSelBg);
                s.frame(list_x0, y, list_x1 - kTmRingInset - list_x0, kListRowH - 2, kTmRingW, kAcc);
            }
            drawTextClipped(s, Face::Row, list_x0 + 12, textBaseline(s, Face::Row, y + 4, 30), 
                            list_x1 - kTmRingInset - 12 - list_x0 - 50, r.title,
                            f ? (uint16_t)kAccInk : (uint16_t)kInk);
            drawTextClipped(s, Face::Meta, list_x0 + 12, textBaseline(s, Face::Meta, y + 34, 22),
                            list_x1 - kTmRingInset - 12 - list_x0 - 50, r.body, kDim);
            if (r.right[0])
                drawTextRight(s, Face::Hint, list_x1 - kTmRingInset - 6, textBaseline(s, Face::Hint, y + 6, 24),
                              r.right, kDim);
            if (r.unread) {
                // the L2's unread badge: a filled circle with the count, red, bottom-right of the row
                const int bx = list_x1 - kTmRingInset - 18, by = y + kListRowH - 24;
                s.disc(bx, by, 9, kBad);
                drawTextCentred(s, Face::Hint, bx, textBaseline(s, Face::Hint, by - 9, 18), r.left, kWhite);
            }
            if (i + 1 < v.row_count) s.hline(list_x0, list_x1 - kTmRingInset, y + kListRowH - 1, kRowLine);
            y += kListRowH;
        }
    }

    // ======================================================================================= one channel
    if (v.open) {
        char title[40];
        snprintf(title, sizeof(title), "# %s", v.channel_name);
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
            drawText(s, Face::Hint, kTmPad + 4, textBaseline(s, Face::Hint, y, 20), m.hhmm, kDimmer);
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
            drawText(s, Face::Body, kTmPad + 58, kTmChatTop + 24, "No messages on this channel yet.", kDim);
            drawText(s, Face::Body, kTmPad + 58, kTmChatTop + 48, "Type below and press Enter to send.", kDimmer);
        }

        // ---- composer: one line, a caret, and the Send key hint
        const int cy0 = work_bot - kTmChatComposerH - 4;
        s.fillRect(kTmPad, cy0, W - 2 * kTmPad, kTmChatComposerH, kPanel);
        s.frame(kTmPad, cy0, W - 2 * kTmPad, kTmChatComposerH, 1, kLine);
        const int bl = textBaseline(s, Face::Body, cy0, kTmChatComposerH);
        const bool empty = (v.composer == nullptr || v.composer[0] == '\0');
        drawText(s, Face::Body, kTmPad + 12, bl, empty ? "message the channel, Enter sends" : v.composer,
                 empty ? (uint16_t)kDimmer : (uint16_t)kInk);
        if (!empty) {
            const Font* f = fontFor(Face::Body);
            const int   tw = (f && f->ready()) ? f->textWidth(v.composer) : 0;
            const int   cx = kTmPad + 12 + tw + 3;
            s.fillRect(cx, cy0 + 12, 2, kTmChatComposerH - 24, kAcc);   // caret
        }
        const int bw = 86, bh = 34;
        const int bx = W - kTmPad - bw - 8, by = cy0 + (kTmChatComposerH - bh) / 2;
        s.fillRect(bx, by, bw, bh, kSelBg2);
        s.frame(bx, by, bw, bh, 1, kAcc);
        drawTextCentred(s, Face::Hint, bx + bw / 2, textBaseline(s, Face::Hint, by, bh), "Send", kAccInk);
    }

    static const HintItem hints_list[] = {{"up/dn", "move"}, {"ret", "open"}, {"esc", "back"}};
    static const HintItem hints_open[] = {{"type", "write"}, {"up/dn", "scroll"}, {"ret", "send"}, {"esc", "back"}};
    drawHintBar(s, tm::Page::Chat, v.open ? hints_open : hints_list, v.open ? 4 : 3);
}

}  // namespace ui
}  // namespace om
