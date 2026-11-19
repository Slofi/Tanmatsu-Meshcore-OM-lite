// SPDX-License-Identifier: MIT
// The Tanmatsu app's emoji table: a SUBSET of the L2's Noto Color Emoji cells, read with the L2's
// own parser (firmware/src/ui/emoji_table.h, included below — never forked).
//
// Why a subset: the L2's full blob is 623,056 B (1,416 cells); this app has 3,200 KiB per app and a
// 2 MB build check, and it is already ~1.55 MB. `tools/mkemoji_subset.py` writes the same layout with
// 274 cells (120,576 B) — every glyph the L2's picker offers plus Smileys & Emotion, hand gestures,
// hearts and the common symbols (see that script's header).
//
// The app draws each 13x13 cell at 2x (26x26, nearest neighbour) because its text is larger than the
// L2's 13 px line. The width the wrap must use is kEmojiAdvance, and `emojiTextWidth` is the ONE
// implementation of "how wide is this UTF-8 run", shared by Font::textWidthRange and the chat wrap
// callback, so a line can never be measured narrower than it draws.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// One cell, pointing straight into flash: w*h RGB565 words (little endian) then h*ceil(w/2) bytes of
// 4-bit coverage, high nibble = left pixel. Same struct the L2's emoji.h has.
struct EmojiGlyph {
    const uint8_t* rgb   = nullptr;
    const uint8_t* alpha = nullptr;
    uint8_t        w     = 0;
    uint8_t        h     = 0;
};

constexpr int kEmojiScale   = 2;                 // 13 px cells on a 13 px L2 line -> 26 px here
constexpr int kEmojiAdvance = 13 * kEmojiScale;  // the drawn width, no air (EST-457's word)

// Parse the embedded subset blob once. False for a blob the L2 parser rejects (then every emoji keeps
// today's behaviour: a code point the face lacks).
bool emojiUseBlob(const uint8_t* data, uint32_t size);

bool emojiReady();
int  emojiTableCount();
// Null for a code point the subset does not hold (ASCII, a flag, a modifier the walker consumes, ...).
const EmojiGlyph* emojiFind(uint32_t cp);
// The width one emoji takes at this scale, or 0 when it is not in the subset.
int emojiAdvance(uint32_t cp);

// How wide a byte range is, walked one drawable unit at a time with the L2's rules (variation
// selectors, keycap and skin tones belong to their base; a ZWJ joins the part before it; a stray
// modifier takes no room). An emoji counts kEmojiAdvance; anything else asks the face; a code point
// the face lacks keeps today's `missing_w`.
struct EmojiFaceMetrics {
    int (*advance)(void* ctx, uint32_t cp) = nullptr;   // >0 when the face has the glyph
    void* ctx                              = nullptr;
    int   missing_w                        = 0;         // the face's advance for a code point it lacks
};

int emojiTextWidth(const char* s, int len, const EmojiFaceMetrics& m);

}  // namespace ui
}  // namespace om
