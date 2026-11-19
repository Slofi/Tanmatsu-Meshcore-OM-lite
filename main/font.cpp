// SPDX-License-Identifier: MIT
#include "font.h"

#include <stdio.h>
#include <string.h>

#include "../../firmware/src/ui/text_wrap.h"   // the L2's UTF-8/emoji unit walker (textUnitAt): one owner
#include "emoji.h"                             // the subset table + the one width rule (core/emoji.cpp)

extern "C" {
// Embedded by main/CMakeLists.txt (EMBED_FILES). IDF names the symbols after the file.
extern const uint8_t sans_title22_bin_start[] asm("_binary_sans_title22_bin_start");
extern const uint8_t sans_title22_bin_end[] asm("_binary_sans_title22_bin_end");
extern const uint8_t sans_row17_bin_start[] asm("_binary_sans_row17_bin_start");
extern const uint8_t sans_row17_bin_end[] asm("_binary_sans_row17_bin_end");
extern const uint8_t sans_body13_bin_start[] asm("_binary_sans_body13_bin_start");
extern const uint8_t sans_body13_bin_end[] asm("_binary_sans_body13_bin_end");
extern const uint8_t sans_meta12_bin_start[] asm("_binary_sans_meta12_bin_start");
extern const uint8_t sans_meta12_bin_end[] asm("_binary_sans_meta12_bin_end");
extern const uint8_t sans_hint11_bin_start[] asm("_binary_sans_hint11_bin_start");
extern const uint8_t sans_hint11_bin_end[] asm("_binary_sans_hint11_bin_end");
extern const uint8_t mono_kv12_bin_start[] asm("_binary_mono_kv12_bin_start");
extern const uint8_t mono_kv12_bin_end[] asm("_binary_mono_kv12_bin_end");
}

namespace om {
namespace ui {
namespace {

inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline uint16_t be16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

// mkvlw.py packs each glyph record as "&gt;IIIIiiI" = 7 x 4 bytes = 28 (code, h, w, advance, dy, dx, pad).
// S499: this was 24 in the first version, which shifted every lookup and made the text invisible on glass.
constexpr uint32_t kRecordBytes = 28;

// Minimal UTF-8: enough for ASCII + Latin-1/Extended-A, which is what the faces carry.
// S514/EST-457: textWidth*/draw now walk units with the L2's textUnitAt (which also knows the emoji
// sequences), so this local decode is gone — one walker, not two that could disagree about an emoji.

// Blend `fg` over the destination pixel with 8-bit coverage. RGB565 in, RGB565 out.
inline uint16_t blend565(uint16_t dst, uint16_t fg, uint8_t a) {
    if (a == 0) return dst;
    if (a == 255) return fg;
    const uint32_t ia = 255 - a;
    const uint32_t r  = (((fg >> 11) & 0x1F) * a + ((dst >> 11) & 0x1F) * ia) / 255;
    const uint32_t g  = (((fg >> 5) & 0x3F) * a + ((dst >> 5) & 0x3F) * ia) / 255;
    const uint32_t b  = ((fg & 0x1F) * a + (dst & 0x1F) * ia) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

}  // namespace

bool Font::load(const uint8_t* data, uint32_t len, const char* name) {
    ready_ = false;
    data_  = data;
    len_   = len;
    strncpy(name_, name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = 0;
    if (data == nullptr || len < 24) return false;

    const uint32_t count   = be32(data + 0);
    const uint32_t version = be32(data + 4);
    ascent_                = (int16_t)be32(data + 16);
    descent_               = (int16_t)be32(data + 20);
    if (version != 3 || count == 0 || count > 4000) return false;      // mkvlw.py writes 3
    const uint32_t records = 24 + count * kRecordBytes;
    if (records > len) return false;
    count_   = count;
    bitmaps_ = records;
    ready_   = true;
    return true;
}

bool Font::glyph(uint32_t code, Glyph* out) const {
    if (!ready_ || out == nullptr) return false;
    // Records are sorted by code point (mkvlw.py sorts), so binary search is exact.
    uint32_t lo = 0, hi = count_;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        const uint8_t* r   = data_ + 24 + mid * kRecordBytes;
        const uint32_t c   = be32(r);
        if (c == code) {
            // The bitmap offset is not stored: walk the records before it once (cheap, 24 B each).
            out->code    = code;
            out->h       = (uint16_t)be32(r + 4);
            out->w       = (uint16_t)be32(r + 8);
            out->advance = (uint16_t)be32(r + 12);
            out->dy      = (int16_t)be32(r + 16);
            out->dx      = (int16_t)be32(r + 20);
            uint32_t off = 0;
            for (uint32_t i = 0; i < mid; i++) {
                const uint8_t* p = data_ + 24 + i * kRecordBytes;
                off += (uint32_t)be32(p + 4) * (uint32_t)be32(p + 8);
            }
            out->bitmap = bitmaps_ + off;
            return true;
        }
        if (code < c)
            hi = mid;
        else
            lo = mid + 1;
    }
    return false;
}

// One face advance for the shared width rule (core/emoji.cpp asks this per code point).
namespace {
int faceAdvanceCb(void* ctx, uint32_t cp) {
    const Font* f = (const Font*)ctx;
    Glyph       g;
    return (f != nullptr && f->glyph(cp, &g)) ? (int)g.advance : 0;
}
}  // namespace

int Font::textWidth(const char* utf8) const {
    if (!ready_ || utf8 == nullptr) return 0;
    return textWidthRange(utf8, (int)strlen(utf8));
}

// Width of a BYTE RANGE, not a C string (S503: the wrap code measures candidate line ends that are not
// NUL-terminated). Walked one drawable unit at a time by the L2's own textUnitAt, so an emoji counts its
// DRAWN width (26 px), a variation selector / skin tone is free and a wrapped line is exactly as wide as
// it draws. S514/EST-457: the emoji branch is the one change; every other code point keeps today's rule.
int Font::textWidthRange(const char* utf8, int len) const {
    if (!ready_ || utf8 == nullptr || len <= 0) return 0;
    EmojiFaceMetrics m;
    m.advance   = faceAdvanceCb;
    m.ctx       = (void*)this;
    m.missing_w = (int)(ascent_ / 2);   // unknown: a half-em placeholder width, as before
    return emojiTextWidth(utf8, len, m);
}

int Font::textWidthN(const char* utf8, int max_px) const {
    if (!ready_ || utf8 == nullptr) return 0;
    int w = 0;
    const int len = (int)strlen(utf8);
    for (int i = 0; i < len;) {
        TextUnit u;
        const int next = textUnitAt(utf8, len, i, &u);
        if (next <= i) break;
        i = next;
        if (u.zero) continue;
        int adv = emojiAdvance(u.cp);
        if (adv == 0) {
            Glyph g;
            adv = glyph(u.cp, &g) ? (int)g.advance : (int)(ascent_ / 2);
        }
        if (w + adv > max_px) break;
        w += adv;
    }
    return w;
}

// S514/EST-457: one 13x13 Noto cell, drawn at kEmojiScale (26x26, nearest neighbour), its 4-bit
// coverage blended over whatever is already on the framebuffer (the caller's plate has been drawn by
// now, and the port is readable, so no background colour has to be guessed). Vertically centred on the
// text line: the line runs from (baseline - ascent) to (baseline + descent).
namespace {
void drawEmojiScaled(uint16_t* px, int fb_w, int fb_h, int x, int y_base, const EmojiGlyph& e, int ascent,
                     int descent) {
    if (px == nullptr || e.rgb == nullptr || e.alpha == nullptr || e.w == 0 || e.h == 0) return;
    const int side = (int)e.w * kEmojiScale;
    const int top  = y_base - (ascent - descent) / 2 - side / 2;
    const int stride = (e.w + 1) / 2;
    for (int dy = 0; dy < side; ++dy) {
        const int y = top + dy;
        if (y < 0 || y >= fb_h) continue;
        const int sy = dy / kEmojiScale;
        for (int dx = 0; dx < (int)e.w * kEmojiScale; ++dx) {
            const int sx = dx / kEmojiScale;
            const uint8_t packed = e.alpha[(size_t)sy * stride + sx / 2];
            const uint8_t nibble = (sx & 1) ? (uint8_t)(packed & 0x0F) : (uint8_t)(packed >> 4);
            if (nibble == 0) continue;
            const int px_x = x + dx;
            if (px_x < 0 || px_x >= fb_w) continue;
            const uint16_t rgb = (uint16_t)(e.rgb[((size_t)sy * e.w + sx) * 2] |
                                            ((uint16_t)e.rgb[((size_t)sy * e.w + sx) * 2 + 1] << 8));
            uint16_t& dst = px[(size_t)y * fb_w + px_x];
            dst = blend565(dst, rgb, (uint8_t)(nibble * 17));
        }
    }
}
}  // namespace

int Font::draw(void* fb, int fb_w, int fb_h, int x, int y_base, const char* utf8, uint16_t colour) const {
    if (!ready_ || utf8 == nullptr || fb == nullptr) return x;
    uint16_t* px  = (uint16_t*)fb;
    int       pen = x;
    const int len = (int)strlen(utf8);
    // One drawable unit at a time, with the L2's rules (S514/EST-457): a variation selector, a skin tone
    // and U+20E3 belong to their base; a ZWJ belongs to the part before it; a stray modifier draws
    // nothing. An emoji the subset holds draws at 2x; a code point it does not hold keeps today's
    // behaviour (no pixels, half-em advance).
    for (int i = 0; i < len;) {
        TextUnit u;
        const int next = textUnitAt(utf8, len, i, &u);
        if (next <= i) break;
        i = next;
        if (u.zero) continue;
        if (const EmojiGlyph* e = emojiFind(u.cp)) {
            drawEmojiScaled(px, fb_w, fb_h, pen, y_base, *e, ascent_, descent_);
            pen += kEmojiAdvance;
            continue;
        }
        Glyph g;
        if (!glyph(u.cp, &g) || g.w == 0 || g.h == 0) {
            pen += glyph(u.cp, &g) ? g.advance : (int)(ascent_ / 2);
            continue;
        }
        const int gx0 = pen + g.dx;
        const int gy0 = y_base - g.dy;
        const uint8_t* src = data_ + g.bitmap;
        for (int row = 0; row < g.h; row++) {
            const int y = gy0 + row;
            if (y < 0 || y >= fb_h) continue;
            const uint8_t* line = src + (size_t)row * g.w;
            for (int col = 0; col < g.w; col++) {
                const uint8_t a = line[col];
                if (!a) continue;
                const int px_x = gx0 + col;
                if (px_x < 0 || px_x >= fb_w) continue;
                uint16_t& dst = px[(size_t)y * fb_w + px_x];
                dst = blend565(dst, colour, a);
            }
        }
        pen += g.advance;
    }
    return pen;
}

namespace {
Font s_title, s_row, s_body, s_meta, s_hint, s_kv;
bool s_loaded = false;

void loadOne(Font& f, const uint8_t* b, const uint8_t* e, const char* name) {
    f.load(b, (uint32_t)(e - b), name);
    printf("[tm] font %-12s %6u bytes  %s\n", name, (unsigned)(e - b), f.ready() ? "ok" : "FAILED");
}
}  // namespace

void loadFonts() {
    if (s_loaded) return;
    s_loaded = true;
    loadOne(s_title, sans_title22_bin_start, sans_title22_bin_end, "sans_title22");
    loadOne(s_row, sans_row17_bin_start, sans_row17_bin_end, "sans_row17");
    loadOne(s_body, sans_body13_bin_start, sans_body13_bin_end, "sans_body13");
    loadOne(s_meta, sans_meta12_bin_start, sans_meta12_bin_end, "sans_meta12");
    loadOne(s_hint, sans_hint11_bin_start, sans_hint11_bin_end, "sans_hint11");
    loadOne(s_kv, mono_kv12_bin_start, mono_kv12_bin_end, "mono_kv12");
}

const Font* fontFor(Face f) {
    switch (f) {
        case Face::Title: return &s_title;
        case Face::Row: return &s_row;
        case Face::Body: return &s_body;
        case Face::Meta: return &s_meta;
        case Face::Hint: return &s_hint;
        case Face::Kv: return &s_kv;
    }
    return &s_body;
}

}  // namespace ui
}  // namespace om
