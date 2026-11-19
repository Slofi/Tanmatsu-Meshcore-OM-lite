// SPDX-License-Identifier: MIT
// Host test S514/EST-457: the emoji SUBSET blob and the way the app draws and measures it.
//
//  * the subset parses with the L2's OWN parser (firmware/src/ui/emoji_table.h), every entry sorted,
//    4-aligned, in range and with ink — and it stays under the 120 KiB cap;
//  * lookups: every curated picker code point is IN, ASCII/ZWJ/VS16/skin tone/flags are OUT;
//  * the L2's text walker (firmware/src/ui/text_wrap.cpp) on ZWJ / variation selector / skin tone /
//    keycap sequences;
//  * the width rule the draw uses (core/emoji.cpp::emojiTextWidth) and the chat wrap
//    (core/chat_layout.cpp) with emoji in the text: no wrapped line is wider than its width, and an
//    emoji counts its DRAWN 26 px — a code point the subset lacks keeps today's half-em advance.
//
// The blob is read from the tree, so a stale or unsorted subset fails here, not on the glass.
#include <stdio.h>
#include <string.h>

#include <vector>

#include "chat_layout.h"
#include "emoji.h"
#include "ui/emoji_table.h"
#include "ui/text_wrap.h"

using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                        \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                      \
        }                                                  \
    } while (0)

// The cap the ticket sets, and the byte size a 13x13 cell takes in the L2 layout.
static const int kCapBytes = 120 * 1024;

static std::vector<uint8_t> loadBlob() {
    std::vector<uint8_t> data;
    FILE* f = fopen("../../main/emoji/emoji_subset.bin", "rb");
    if (f == nullptr) {
        printf("FAIL cannot open ../../main/emoji/emoji_subset.bin (run from test/host)\n");
        ++g_fail;
        return data;
    }
    uint8_t buf[16384];
    size_t  n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    return data;
}

static bool cellHasInk(const EmojiBlob& b, uint32_t offset) {
    const size_t rgb   = (size_t)b.w * b.h * 2;
    const size_t alpha = (size_t)b.h * ((b.w + 1) / 2);
    for (size_t i = 0; i < alpha; ++i)
        if (b.data[offset + rgb + i] != 0) return true;
    return false;
}

// ---- 1. the blob, through the L2 parser -----------------------------------------------------------
static void testBlob() {
    const std::vector<uint8_t> data = loadBlob();
    if (data.empty()) return;
    CHECK((int)data.size() <= kCapBytes);                      // 120 KiB HARD CAP
    const EmojiBlob b = emojiParseBlob(data.data(), (uint32_t)data.size());
    CHECK(b.ok);
    CHECK(b.w == 13 && b.h == 13);                             // the L2's cells, unchanged
    CHECK(b.count >= 250 && b.count <= 279);                   // 274 when written
    CHECK(b.dataOffset == 16u + (uint32_t)b.count * 8u);
    CHECK(b.dataOffset + (uint32_t)b.count * 432u == data.size());   // 429 used + 3 pad, 4-aligned

    uint32_t previous = 0;
    for (int i = 0; i < (int)b.count; ++i) {
        EmojiIndexEntry e;
        CHECK(emojiEntry(b, i, &e));
        if (i > 0) CHECK(e.cp > previous);
        previous = e.cp;
        CHECK(e.offset >= b.dataOffset);
        CHECK((e.offset & 3u) == 0);
        CHECK((e.offset - b.dataOffset) % 432u == 0);
        CHECK(e.offset + 432u <= b.size);
        CHECK(cellHasInk(b, e.offset));
        EmojiIndexEntry found;
        CHECK(emojiFindIn(b, e.cp, &found) && found.offset == e.offset);
    }
}

// ---- 2. lookups: in and out of the subset ---------------------------------------------------------
static void testLookups() {
    EmojiIndexEntry e;
    const std::vector<uint8_t> data = loadBlob();
    if (data.empty()) return;
    const EmojiBlob b = emojiParseBlob(data.data(), (uint32_t)data.size());

    // In: a representative from each group the ticket names.
    CHECK(emojiFindIn(b, 0x1F600, &e));   // grinning face (Smileys & Emotion)
    CHECK(emojiFindIn(b, 0x1F602, &e));   // tears of joy
    CHECK(emojiFindIn(b, 0x1F44D, &e));   // thumbs up (People & Body)
    CHECK(emojiFindIn(b, 0x2764, &e));    // heavy heart
    CHECK(emojiFindIn(b, 0x2705, &e));    // check mark button
    CHECK(emojiFindIn(b, 0x26A0, &e));    // warning
    CHECK(emojiFindIn(b, 0x1F525, &e));   // fire
    CHECK(emojiFindIn(b, 0x2B50, &e));    // star
    CHECK(emojiFindIn(b, 0x2601, &e));    // cloud (weather)
    // Out: the sequence-only code points and things the subset deliberately drops.
    CHECK(!emojiFindIn(b, 'A', &e));
    CHECK(!emojiFindIn(b, 0x200D, &e));   // ZWJ
    CHECK(!emojiFindIn(b, 0xFE0F, &e));   // VS16
    CHECK(!emojiFindIn(b, 0x1F3FD, &e));  // skin tone (a modifier)
    CHECK(!emojiFindIn(b, 0x1F1F8, &e));  // regional indicator (a flag is a pair of boxes)
    CHECK(!emojiFindIn(b, 0x1F5FF, &e));  // moai: NOT in the subset (the "uncovered" case)
}

// The device path (core/emoji.cpp) over the same blob.
static void testSubsetApi(const std::vector<uint8_t>& data) {
    CHECK(emojiUseBlob(data.data(), (uint32_t)data.size()));
    CHECK(emojiReady());
    EmojiBlob b;
    b = emojiParseBlob(data.data(), (uint32_t)data.size());
    CHECK(emojiTableCount() == (int)b.count);
    const EmojiGlyph* g = emojiFind(0x1F44D);
    CHECK(g != nullptr && g->w == 13 && g->h == 13 && g->rgb != nullptr && g->alpha != nullptr);
    CHECK(emojiAdvance(0x1F44D) == 26);       // the drawn width: 13 x kEmojiScale
    CHECK(emojiFind(0x1F5FF) == nullptr);     // out of the subset
    CHECK(emojiFind('A') == nullptr);
    CHECK(emojiAdvance('A') == 0);
}

// ---- 3. the L2's walker on the sequences ----------------------------------------------------------
static int units(const char* s, TextUnit* out, int max) {
    const int len = (int)strlen(s);
    int       n   = 0;
    for (int i = 0; i < len && n < max;) {
        TextUnit u;
        const int next = textUnitAt(s, len, i, &u);
        if (next <= i) break;
        i    = next;
        out[n++] = u;
    }
    return n;
}

static void testWalker() {
    TextUnit u[8];

    // A heart and its presentation selector: ONE unit, the selector consumed (not drawn, no width).
    CHECK(units("\xe2\x9d\xa4\xef\xb8\x8f", u, 8) == 1);   // U+2764 U+FE0F
    CHECK(u[0].cp == 0x2764 && u[0].coreEnd == 3 && u[0].end == 6 && !u[0].zero);

    // A thumbs up with a skin tone: ONE unit, the modifier consumed.
    CHECK(units("\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd", u, 8) == 1);   // U+1F44D U+1F3FD
    CHECK(u[0].cp == 0x1F44D && u[0].coreEnd == 4 && u[0].end == 8);

    // A ZWJ family: three units, each ZWJ consumed by the part before it.
    CHECK(units("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d"
                "\xf0\x9f\x91\xa7",
                u, 8) == 3);
    CHECK(u[0].cp == 0x1F468 && u[0].end == 7);
    CHECK(u[1].cp == 0x1F469 && u[1].start == 7 && u[1].coreEnd == 11 && u[1].end == 14);
    CHECK(u[2].cp == 0x1F467 && u[2].start == 14 && u[2].coreEnd == 18 && u[2].end == 18);

    // A selector with no base at all is its own unit, marked zero (no room, no pixels).
    CHECK(units("\xef\xb8\x8f", u, 8) == 1);
    CHECK(u[0].zero && u[0].cp == 0xFE0F);
    // ...while after a printable base it is consumed INTO that unit (the L2's rule).
    CHECK(units(" \xef\xb8\x8f", u, 8) == 1);
    CHECK(u[0].cp == ' ' && u[0].end == 4 && !u[0].zero);

    // Keycap: '1' + VS16 + U+20E3 is one unit.
    CHECK(units("1\xef\xb8\x8f\xe2\x83\xa3", u, 8) == 1);
    CHECK(u[0].cp == '1' && u[0].end == 7);
}

// ---- 4. the width rule the draw uses, and the chat wrap -------------------------------------------
// A fake face: 10 px per ASCII letter, nothing else. The real one is the VLW font; the rule is the same.
static int fakeAdvance(void* ctx, uint32_t cp) {
    (void)ctx;
    return cp < 0x80 ? 10 : 0;
}
static const int kMissing = 6;   // the device's ascent/2 for a code point the face lacks

static int fakeMeasure(void* ctx, const char* ptr, int len) {
    EmojiFaceMetrics m;
    m.advance   = fakeAdvance;
    m.ctx       = ctx;
    m.missing_w = kMissing;
    return emojiTextWidth(ptr, len, m);
}

static void testWidthsAndWrap() {
    const EmojiFaceMetrics m = {fakeAdvance, nullptr, kMissing};

    // An emoji is 26 px, whatever the face thinks; text is 10 px per ASCII letter.
    CHECK(emojiTextWidth("\xf0\x9f\x91\x8d", 4, m) == 26);
    CHECK(emojiTextWidth("ab", 2, m) == 20);
    CHECK(emojiTextWidth("\xf0\x9f\x91\x8d", 4, m) == 26);

    // The selector and the skin tone are free: heart + VS16 is ONE 26 px glyph.
    CHECK(emojiTextWidth("\xe2\x9d\xa4\xef\xb8\x8f", 6, m) == 26);
    CHECK(emojiTextWidth("\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd", 8, m) == 26);

    // A ZWJ sequence draws its parts in order: heart + VS16 + ZWJ + fire is TWO 26 px glyphs.
    CHECK(emojiTextWidth("\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x94\xa5", 10, m) == 52);

    // Out of the subset: today's behaviour — the face's missing advance, not a guessed emoji width.
    CHECK(emojiTextWidth("\xf0\x9f\x97\xbf", 4, m) == kMissing);   // U+1F5FF moai

    // The wrap the Chat page runs: core/chat_layout.cpp with this measure at the body column's width
    // (main/chat_page.h: kTmChatBodyW = 738 - 74 = 664 px on the 800 px panel).
    const int kWidth = 664;
    const char* text = "Radio check \xf0\x9f\x91\x8d\xf0\x9f\x91\x8d\xf0\x9f\x98\x80 and a "
                       "longer tail so the line really has to break somewhere sensible \xf0\x9f\x94\xa5";
    const om::mc::Wrapped w = om::mc::wrapText(text, kWidth, fakeMeasure, nullptr);
    CHECK(w.count >= 2);
    for (int i = 0; i < (int)w.count; ++i) {
        const int lineW = fakeMeasure(nullptr, text + w.line[i].start, w.line[i].len);
        CHECK(lineW <= kWidth);   // the emoji width is measured, so a line can never overflow
    }
    // A line made of emoji alone: 26 px each, four fit in 104 px, five do not.
    const char* four = "\xf0\x9f\x91\x8d\xf0\x9f\x91\x8d\xf0\x9f\x91\x8d\xf0\x9f\x91\x8d";
    CHECK(fakeMeasure(nullptr, four, 16) == 104);
    const om::mc::Wrapped tight = om::mc::wrapText(four, 104, fakeMeasure, nullptr);
    CHECK(tight.count == 1);
    const om::mc::Wrapped tighter = om::mc::wrapText(four, 103, fakeMeasure, nullptr);
    CHECK(tighter.count == 2);
    for (int i = 0; i < (int)tighter.count; ++i)
        CHECK(fakeMeasure(nullptr, four + tighter.line[i].start, tighter.line[i].len) <= 103);

    // A truncated sequence decodes as the replacement char per byte, never as a glyph.
    CHECK(fakeMeasure(nullptr, "\xf0\x9f\x91", 3) == 3 * kMissing);
}

int main() {
    const std::vector<uint8_t> data = loadBlob();
    printf("emoji subset: %u bytes\n", (unsigned)data.size());
    testBlob();
    testLookups();
    if (!data.empty()) testSubsetApi(data);
    testWalker();
    testWidthsAndWrap();
    if (g_fail == 0) printf("emoji: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
