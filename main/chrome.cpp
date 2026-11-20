// SPDX-License-Identifier: MIT
#include "chrome.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"   // taskENTER_CRITICAL: the radio task publishes, the main task draws

#include "launcher_page.h"   // textBaseline / drawText* — the one place the baseline math lives
#include "chat_layout.h"     // S516: the UTF-8-safe wrapper the pop-up reuses (host-tested)
#include "chat_mentions.h"   // S516/EST-481: the pop-up shows the DRAWN "@Name" (brackets hidden)
#include "nodes_fresh.h"      // S515/EST-461: the dot mask's bits (GLOBAL scope: see the note below)
#include "volume_popup.h"    // S516/EST-478: the volume pop-up's text and placement (pure, host-tested)
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"   // the L2 palette: one source of truth, never copied

namespace om {
namespace ui {
namespace {

// ---- the app's ONE notice (S513/EST-435) ----------------------------------------------------------------------
// Filled by noticeSet() from whichever task decided it, copied out by noticeText() on the main task. The lock is a
// critical section, not a mutex: the longest it is ever held is one short copy, so it cannot delay the radio.
portMUX_TYPE g_notice_mux = portMUX_INITIALIZER_UNLOCKED;

struct NoticeSlot {
    // 64, not 40: S513/EST-437's "New node: <name> (<type>)" can reach 44 bytes (a 23-character advert name and
    // "repeater"), and a truncated node name would be worse than a wider plate.
    char     text[64] = {0};
    int64_t  until_us = 0;     // esp_timer time after which it is no longer live
};

NoticeSlot g_notice;
uint32_t   g_notice_gen = 0;   // bumped by every set/clear, under the same lock (S513, Claude)
char       g_notice_copy[64] = {0};   // the reader's own copy: drawn text never changes under the drawer

// ---- the new-message pop-up (S515/EST-467) --------------------------------------------------------------------
// The radio task publishes the newest message here (text + its chat key); the main task consumes it once per
// loop pass, applies core/msg_popup.h's rules and draws it. Same critical-section discipline as the notice.
portMUX_TYPE g_popup_mux = portMUX_INITIALIZER_UNLOCKED;

struct PendingPopup {
    char      head[64]  = {0};
    char      text[192] = {0};
    PopupChat chat      = PopupChat::None;
    uint8_t   channel   = 0;
    uint8_t   peer[32]  = {0};
    uint32_t  count     = 0;   // messages published since the main task last polled
};

PendingPopup g_pending_popup;
MsgPopup     g_popup;              // main task's state (see core/msg_popup.h)
uint32_t     g_popup_gen   = 0;    // bumped by every show/replace/dismiss
char         g_popup_live_head[64]  = {0};   // the text of the popup on the glass, committed with the state
char         g_popup_live_text[192] = {0};
char         g_popup_head_copy[64]  = {0};
char         g_popup_text_copy[192] = {0};

// Greedy word wrap for the pop-up's body, at most three lines. The font is the device's own face; "..." is
// ASCII on purpose (the generated font carries Latin-1 + Latin Extended-A, not U+2026).
// S516/EST-472 item 7: this used to be a byte-by-byte greedy wrap, so a full 95-byte line could end INSIDE a
// multi-byte character (a Slovenian "č" became two bytes of garbage on the plate). It now reuses
// om::mc::wrapText — the SAME wrapper the chat page draws with, whose "a multi-byte sequence is never cut" rule is
// host-tested (test_layout, also in the ASan stage). The pop-up is capped at three lines, so a fourth or a clipped
// tail becomes "...".
int measurePopup(void* ctx, const char* ptr, int len) {
    const Font* f = (const Font*)ctx;
    return (f != nullptr && f->ready()) ? f->textWidthRange(ptr, len) : 0;
}

int wrapPopupLines(const Font* f, const char* text, int maxw, char out[3][96]) {
    const char* src = text != nullptr ? text : "";
    const om::mc::Wrapped w = om::mc::wrapText(src, maxw, measurePopup, (void*)f);
    int        n    = w.count < 3 ? (int)w.count : 3;
    const bool more = w.clipped || w.count > 3;
    if (n == 0) {
        out[0][0] = '\0';
        return 0;
    }
    for (int i = 0; i < n; ++i) {
        int len = (int)w.line[i].len;
        if (len > 95) len = 95;   // wrapText never splits a sequence, and its lines fit the width
        memcpy(out[i], src + w.line[i].start, (size_t)len);
        out[i][len] = '\0';
    }
    if (more) {
        char* last = out[n - 1];
        while (last[0] != '\0' && f != nullptr && f->ready() &&
               f->textWidth(last) + f->textWidth("...") > maxw) {
            size_t l = strlen(last);
            while (l > 0 && ((unsigned char)last[l - 1] & 0xC0) == 0x80) --l;   // never split a UTF-8 sequence
            if (l == 0) break;
            last[l - 1] = '\0';
        }
        const size_t l = strlen(last);
        if (l + 4 < 96) snprintf(last + l, 96 - l, "...");
    }
    return n;
}

void drawMessagePopup(Screen& s, const MsgPopupView& v) {
    const int W = kTmW, H = kTmH;

    static char lines[3][96];
    const Font* bf = fontFor(Face::Body);
    const int   pw = 560;
    int         n  = wrapPopupLines(bf, v.text, pw - 48, lines);

    const int ph = 20 + 26 + n * 22 + (v.extra > 0 ? 22 : 0) + 16;
    const int px = (W - pw) / 2;
    const int py = (H - ph) / 2;

    drawRoundedRect(s, px, py, pw, ph, 8, kPanel2);
    s.frame(px, py, pw, ph, 2, kAcc);
    drawText(s, Face::Title, px + 24, textBaseline(s, Face::Title, py + 10, 26), v.head, kAccInk);
    int ly = py + 40;
    for (int i = 0; i < n; ++i) {
        drawText(s, Face::Body, px + 24, textBaseline(s, Face::Body, ly, 22), lines[i], kInk);
        ly += 22;
    }
    if (v.extra > 0) {
        char more[40];
        snprintf(more, sizeof(more), "+%u more", (unsigned)v.extra);
        drawText(s, Face::Body, px + 24, textBaseline(s, Face::Body, ly, 22), more, kDim);
    }
}

// ---- the volume pop-up (S516/EST-478) -------------------------------------------------------------
// main.cpp owns the key decision and calls volumePopupPress() on every press; this slot is the display.
// The plate sits just above the hint bar. The message pop-up is centred and at most 150 px tall, so its
// lowest edge is ~315 px while this plate starts at 390 px: both fit, neither hides the other.
tm::volume::VolumePopup g_volume;
bool                    g_volume_master_on = true;

void drawVolumePopup(Screen& s) {
    int x = 0, y = 0;
    tm::volume::popupRect(kTmW, kTmH, kTmHintH, &x, &y);
    drawRoundedRect(s, x, y, tm::volume::kPopupW, tm::volume::kPopupH, 8, kPanel2);
    s.frame(x, y, tm::volume::kPopupW, tm::volume::kPopupH, 2, g_volume_master_on ? kAcc : kLine);

    char text[32];
    tm::volume::popupText(text, sizeof(text), g_volume.level, g_volume_master_on);
    drawText(s, Face::Title, x + 18, textBaseline(s, Face::Title, y + 4, 26), text,
             g_volume_master_on ? kInk : kDim);

    const int bx = x + 18;
    const int by = y + tm::volume::kPopupH - 18;
    const int bw = tm::volume::kPopupW - 36;
    const int bh = 8;
    s.fillRect(bx, by, bw, bh, kLine);
    const int fill = bw * (int)tm::volume::barPercent(g_volume.level) / 100;
    if (fill > 0) s.fillRect(bx, by, fill, bh, g_volume_master_on ? kAcc : kDim);
    s.frame(bx, by, bw, bh, 1, kLine);
}

}  // namespace

void noticeSet(const char* text, uint32_t ttl_ms) {
    if (text == nullptr || text[0] == '\0') return;
    taskENTER_CRITICAL(&g_notice_mux);
    snprintf(g_notice.text, sizeof(g_notice.text), "%s", text);
    // "Newest wins": a second notice replaces the first and restarts its life, so the strip can never show a
    // stale sentence next to a fresh one.
    g_notice.until_us = esp_timer_get_time() + (int64_t)ttl_ms * 1000;
    ++g_notice_gen;
    taskEXIT_CRITICAL(&g_notice_mux);
}

void noticeClear() {
    taskENTER_CRITICAL(&g_notice_mux);
    g_notice.text[0] = '\0';
    g_notice.until_us = 0;
    ++g_notice_gen;
    taskEXIT_CRITICAL(&g_notice_mux);
}

const char* noticeText() {
    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&g_notice_mux);
    const bool live = g_notice.text[0] != '\0' && now < g_notice.until_us;
    if (live) snprintf(g_notice_copy, sizeof(g_notice_copy), "%s", g_notice.text);
    taskEXIT_CRITICAL(&g_notice_mux);
    return live ? g_notice_copy : nullptr;
}

bool noticeNeedsRepaint() {
    static uint32_t seen_gen  = 0;
    static bool     was_live  = false;
    const int64_t   now       = esp_timer_get_time();
    taskENTER_CRITICAL(&g_notice_mux);
    const uint32_t gen  = g_notice_gen;
    const bool     live = g_notice.text[0] != '\0' && now < g_notice.until_us;
    taskEXIT_CRITICAL(&g_notice_mux);
    const bool changed = gen != seen_gen;   // a new notice (or a clear) since the last pass
    const bool expired = was_live && !live; // the live one just ran out: take it off the glass
    seen_gen = gen;
    was_live = live;
    return changed || expired;
}

// ---- the new-message pop-up (S515/EST-467): the task-safe slot around core/msg_popup.h --------------------------
void msgPopupOffer(const char* head, const char* text, PopupChat chat, uint8_t channel,
                   const uint8_t peer[32]) {
    if (head == nullptr || text == nullptr || text[0] == '\0') return;
    // S516/EST-481: the pop-up draws the same "@Name" form the chat does (brackets hidden), so one message
    // cannot read two ways on one screen. This is a DISPLAY copy; the ring and the card log keep the stored bytes.
    char drawn[192];
    om::ui::mentionDrawn(text, drawn, sizeof(drawn), nullptr, 0);
    taskENTER_CRITICAL(&g_popup_mux);
    snprintf(g_pending_popup.head, sizeof(g_pending_popup.head), "%s", head);
    snprintf(g_pending_popup.text, sizeof(g_pending_popup.text), "%s", drawn);
    g_pending_popup.chat    = chat;
    g_pending_popup.channel = channel;
    if (peer != nullptr)
        memcpy(g_pending_popup.peer, peer, sizeof(g_pending_popup.peer));
    else
        memset(g_pending_popup.peer, 0, sizeof(g_pending_popup.peer));
    if (g_pending_popup.count < kMsgPopupMaxExtra) ++g_pending_popup.count;
    taskEXIT_CRITICAL(&g_popup_mux);
}

bool msgPopupPoll(uint32_t now_ms, bool screen_off, bool chat_open, PopupChat chat, uint8_t channel,
                  const uint8_t peer[32]) {
    bool changed = false;
    taskENTER_CRITICAL(&g_popup_mux);
    if (g_pending_popup.count > 0) {
        const uint32_t arrived = g_pending_popup.count;
        g_pending_popup.count  = 0;
        bool same_chat_open = false;
        if (chat_open && chat != PopupChat::None && g_pending_popup.chat == chat) {
            same_chat_open = (chat == PopupChat::Channel)
                                 ? (g_pending_popup.channel == channel)
                                 : (peer != nullptr && memcmp(g_pending_popup.peer, peer, 32) == 0);
        }
        if (g_popup.offer(now_ms, screen_off, false, same_chat_open, g_pending_popup.chat,
                          g_pending_popup.channel, g_pending_popup.peer, arrived)) {
            snprintf(g_popup_live_head, sizeof(g_popup_live_head), "%s", g_pending_popup.head);
            snprintf(g_popup_live_text, sizeof(g_popup_live_text), "%s", g_pending_popup.text);
            changed = true;
        }
    }
    if (g_popup.tick(now_ms)) changed = true;
    if (changed) ++g_popup_gen;
    taskEXIT_CRITICAL(&g_popup_mux);
    return changed;
}

bool msgPopupKey() {
    taskENTER_CRITICAL(&g_popup_mux);
    const bool done = g_popup.key();
    if (done) ++g_popup_gen;
    taskEXIT_CRITICAL(&g_popup_mux);
    return done;
}

bool msgPopupView(MsgPopupView* out) {
    bool live = false;
    taskENTER_CRITICAL(&g_popup_mux);
    if (g_popup.visible) {
        snprintf(g_popup_head_copy, sizeof(g_popup_head_copy), "%s", g_popup_live_head);
        snprintf(g_popup_text_copy, sizeof(g_popup_text_copy), "%s", g_popup_live_text);
        live = true;
    }
    taskEXIT_CRITICAL(&g_popup_mux);
    if (!live || out == nullptr) return false;
    out->head  = g_popup_head_copy;
    out->text  = g_popup_text_copy;
    taskENTER_CRITICAL(&g_popup_mux);
    out->extra = g_popup.extra;
    taskEXIT_CRITICAL(&g_popup_mux);
    return true;
}

bool msgPopupNeedsRepaint() {
    static uint32_t seen_gen = 0;
    taskENTER_CRITICAL(&g_popup_mux);
    const uint32_t gen = g_popup_gen;
    taskEXIT_CRITICAL(&g_popup_mux);
    if (gen == seen_gen) return false;
    seen_gen = gen;
    return true;
}

// S516/EST-481: this include used to sit HERE, inside namespace om::ui. That only compiled because
// nodes_fresh.h's own #include "mc_store.h" had not been seen yet, so mc_store was nested into
// om::ui::om::mc along with it; once any earlier header included mc_store.h globally (chat_mentions.h does),
// the nested copy vanished and the file would not compile. It belongs at global scope with the other includes.

// ---- S515/EST-461: the hint bar's dots -------------------------------------------------------------------------
// The main task both writes and reads this (the radio task never decides a dot: it changes the store, and the
// main task counts). So it is a plain byte, not a critical section — the same reasoning as the notice's copy,
// with one fewer task involved.
namespace {

// The rule's bits live in core/nodes_fresh.h (host-tested); these two names are the chrome's, and the
// static_asserts make a silent divergence a compile error.
static_assert(kHintDotNodes == om::ui::kHintDotBitNodes, "the Nodes dot bit moved");
static_assert(kHintDotChat == om::ui::kHintDotBitChat, "the Chat dot bit moved");

uint8_t g_hint_dots = 0;

}  // namespace

void    hintDotsSet(uint8_t mask) { g_hint_dots = (uint8_t)(mask & 0x1F); }
uint8_t hintDots() { return g_hint_dots; }

bool hintDotsNeedsRepaint() {
    static uint8_t seen = 0;
    if (g_hint_dots == seen) return false;
    seen = g_hint_dots;
    return true;   // one repaint per CHANGE, so a dot cannot cost a frame per loop pass
}

// ---- the volume pop-up (S516/EST-478): the display slot above -----------------------------------------
void volumePopupPress(uint32_t now_ms, uint8_t level, bool master_on) {
    g_volume.press(now_ms, level);
    g_volume_master_on = master_on;
}

bool volumePopupTick(uint32_t now_ms) {
    const bool was = g_volume.visible;
    g_volume.tick(now_ms);
    return was && !g_volume.visible;
}


// The six page keys, drawn as vectors (BRIEF § 5; the preview tool's recipes, ported).
// Cloud is an OUTLINE like the others (Filip, S499 "fix the cloud symbol").
void drawSymbolSigil(Screen& s, int kind, int cx, int cy, int r, uint16_t c, bool active) {
    switch (kind) {
        case 0: {  // X = the F1 key (S516/EST-478)
            const int a = r - 1;
            s.px(cx - a, cy - a, c);
            s.px(cx + a, cy - a, c);
            s.px(cx - a, cy + a, c);
            s.px(cx + a, cy + a, c);
            for (int i = 1; i < a; ++i) {
                s.px(cx - i, cy - i, c);
                s.px(cx + i, cy - i, c);
                s.px(cx - i, cy + i, c);
                s.px(cx + i, cy + i, c);
            }
            break;
        }
        case 1: {  // triangle
            const int xs[3] = {cx, cx + r, cx - r};
            const int ys[3] = {cy - r, cy + r, cy + r};
            s.polyline(xs, ys, 3, 2, c);
            break;
        }
        case 2:  // square
            s.frame(cx - r, cy - r, 2 * r, 2 * r, 2, c);
            break;
        case 3:  // circle
            s.ring(cx, cy, r, 2, c);
            break;
        case 4: {  // cloud = the tri-circle key
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
        case 5: {  // diamond
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

void drawKeyCap(Screen& s, int x, int y, int w, int h, const char* text, uint16_t ink) {
    // The hint bar's own cap: the fill and the outline are the SAME two colours the bar has always used
    // (S513), so a chip in the bar and the composer's Enter cap cannot drift apart.
    s.fillRect(x, y, w, h, rgb565(0x232220));
    s.frame(x, y, w, h, 1, rgb565(0x3a3936));
    drawTextCentred(s, Face::Hint, x + w / 2, textBaseline(s, Face::Hint, y, h), text, ink);
}

void drawEnterKeyCap(Screen& s, int x, int y, int w, int h, uint16_t ink) {
    s.fillRect(x, y, w, h, rgb565(0x232220));
    s.frame(x, y, w, h, 1, rgb565(0x3a3936));

    // The return arrow (U+21B5 shape) as strokes: rise at the right, bar to the left, head at the left tip.
    // 10 px wide inside the cap, vertically centred on the hint face. The font has no such glyph.
    const Font* f  = fontFor(Face::Hint);
    const int   tw = (f != nullptr && f->ready()) ? f->textWidth("Enter") : 28;
    const int   gw = 10;
    const int   total = gw + 4 + tw;
    int         gx    = x + (w - total) / 2;
    if (gx < x + 8) gx = x + 8;
    const int cy = y + h / 2;

    s.hline(gx + 1, gx + gw, cy + 2, ink);        // bar, under the arrow head
    s.vline(gx + gw - 1, cy - 3, cy + 3, ink);    // the return's rise
    s.px(gx + 1, cy + 2, ink);                    // the head: the tip plus two short diagonals
    s.px(gx + 2, cy + 1, ink);
    s.px(gx + 2, cy + 3, ink);

    drawText(s, Face::Hint, gx + gw + 4, textBaseline(s, Face::Hint, y, h), "Enter", ink);
}

void drawHintBar(Screen& s, tm::Page active, const HintItem* hints, int hint_count,
                 bool compact_page_keys) {
    const int W = kTmW, H = kTmH;

    // S515/EST-467: the new-message pop-up comes first and WINS over the one notice while it is up. Both are
    // chrome, so every page gets them from this one call without knowing they exist.
    MsgPopupView pv;
    if (msgPopupView(&pv)) {
        drawMessagePopup(s, pv);
    } else if (const char* notice = noticeText()) {
        // S514 (Claude, Filip: "those pop-ups are too small, make them bigger and centered"): a panel in the middle
        // of the screen in the Title face, gold outline, sized to the text (at least 360 px, never wider than the
        // screen). It is drawn last, over the page, and goes with the next repaint after its 3 s.
        const Font* f  = fontFor(Face::Title);
        const int   tw = (f != nullptr && f->ready()) ? f->textWidth(notice) : 300;
        const int   ph = 72;
        int         pw = tw + 64;
        if (pw < 360) pw = 360;
        if (pw > W - 2 * kTmPad) pw = W - 2 * kTmPad;
        const int px = (W - pw) / 2;
        const int py = (H - ph) / 2;
        drawRoundedRect(s, px, py, pw, ph, 8, kPanel2);
        s.frame(px, py, pw, ph, 2, kAcc);
        const int tx = (tw + 64 <= pw) ? px + (pw - tw) / 2 : px + 32;
        drawText(s, Face::Title, tx, textBaseline(s, Face::Title, py, ph), notice, kAccInk);
    }

    // S516/EST-478: the volume plate is chrome, so every page shows it. It sits just above the hint bar and
    // below the message pop-up's lowest edge, so a message arriving during its 1.5 s never has its text hidden.
    if (g_volume.visible) drawVolumePopup(s);

    s.fillRect(0, H - kTmHintH, W, kTmHintH, kBar);
    s.hline(0, W, H - kTmHintH, kLine);
    const int hy = H - kTmHintH;
    const int cy = hy + kTmHintH / 2;
    const int bl = textBaseline(s, Face::Hint, hy, kTmHintH);

    // S516/EST-478: SIX page keys, X first, in Page order (Launcher, Nodes, Chat, Map, Rptr, Settings).
    static const char* page_names[6] = {"LAUNCHER", "NODES", "CHAT", "MAP", "RPTR", "SETTINGS"};
    const Font*        hf            = fontFor(Face::Hint);

    // Where the right-hand page-key cluster begins. The LEFT hints are drawn only while they fit before it,
    // so a page's own actions can never push a chip under the sigils. Six keys need tighter gaps than the
    // old five (18 after the name, 14 after the sigil) or the cluster would eat half the bar.
    // S516/EST-480: with the labels dropped (compact) the same 18/14 gaps are kept, so only the words go and
    // the sigils keep their even spacing.
    int sx = W - kTmPad;
    for (int i = 5; i >= 0; i--) {
        const int tw = (!compact_page_keys && hf && hf->ready()) ? hf->textWidth(page_names[i]) : 0;
        sx -= tw;
        sx -= 18;
        sx -= 14;
    }
    const int right_start = sx;

    int hx = kTmPad;
    for (int i = 0; i < hint_count && hints && i < 6; i++) {
        if (hints[i].key == nullptr || hints[i].key[0] == '\0') continue;
        // S515/EST-467: the cap grows with its key text, because "Enter" is five glyphs and a 30 px cap
        // clipped it. A short key still gets the old 30 px, so no bar quietly changes width for nothing.
        const int kw = (hf && hf->ready()) ? (hf->textWidth(hints[i].key) + 10 > 30
                                                  ? hf->textWidth(hints[i].key) + 10
                                                  : 30)
                                          : 30;
        const int lw = (hf && hf->ready()) ? hf->textWidth(hints[i].label) : 30;
        // Tighter than the old 6/14/16 spacing: a page can carry four or five action chips, and the six
        // page keys must all stay readable on the right.
        const int item_w = kw + 4 + 8 + lw + 10;
        if (hx + item_w > right_start - 8) break;   // no room: stop rather than overlap the page keys
        drawKeyCap(s, hx, cy - 9, kw, 18, hints[i].key, kDim);
        hx += kw + 4;
        drawText(s, Face::Hint, hx, bl, hints[i].label, kDimmer);
        hx += 8 + lw + 10;
    }

    sx = W - kTmPad;
    const uint8_t dots = g_hint_dots;
    for (int i = 5; i >= 0; i--) {
        const bool on = (static_cast<int>(active) == i);
        const int  tw = (!compact_page_keys && hf && hf->ready()) ? hf->textWidth(page_names[i]) : 0;
        sx -= tw;
        // S516/EST-480: compact keeps the sigils (and the dot) and drops only the words.
        if (!compact_page_keys)
            drawText(s, Face::Hint, sx, bl, page_names[i], on ? (uint16_t)kAccInk : kDimmer);
        sx -= 18;
        drawSymbolSigil(s, i, sx, cy, 7, on ? (uint16_t)kAcc : (uint16_t)kDim, on);
        // S515/EST-461: the notification dot, at the sigil's TOP-RIGHT and clear of its gold ring (the ring's
        // nearest point is ~4.9 px from the centre on that diagonal, the dot's edge ~6 px). kBad is the chat
        // list's unread-badge red, so the two reds on the glass cannot drift apart. S516/EST-478: the bits
        // still mean Nodes (1, now the triangle) and Chat (2, now the square) — the NEW keys.
        if ((dots & (1u << i)) != 0) s.disc(sx + 9, cy - 8, 3, kBad);
        sx -= 14;
    }
}

}  // namespace ui
}  // namespace om
