// SPDX-License-Identifier: MIT
#include "emoji.h"

#include <stddef.h>
#include <string.h>

// The L2's own table parser and UTF-8 walker, compiled from that tree (core/CMakeLists.txt puts
// ../../firmware/src on the include path; test/host/run.sh passes the same -I). One owner: a rule
// change in the L2's emoji handling reaches this app by rebuilding, not by copying.
#include "ui/emoji_table.h"
#include "ui/text_wrap.h"

namespace om {
namespace ui {
namespace {

const uint8_t* s_data   = nullptr;
uint32_t       s_size   = 0;
EmojiBlob      s_blob;
bool           s_parsed = false;

const EmojiBlob& blob() {
    if (!s_parsed) {
        s_parsed = true;
        s_blob   = emojiParseBlob(s_data, s_size);
    }
    return s_blob;
}

}  // namespace

bool emojiUseBlob(const uint8_t* data, uint32_t size) {
    s_data   = data;
    s_size   = size;
    s_parsed = false;
    return blob().ok;
}

bool emojiReady() { return blob().ok; }

int emojiTableCount() { return blob().ok ? (int)blob().count : 0; }

const EmojiGlyph* emojiFind(uint32_t cp) {
    static EmojiGlyph glyph;
    const EmojiBlob&  b = blob();
    EmojiIndexEntry   e;
    if (!emojiFindIn(b, cp, &e)) return nullptr;
    glyph.rgb   = b.data + e.offset;
    glyph.alpha = glyph.rgb + (size_t)b.w * b.h * 2;
    glyph.w     = (uint8_t)b.w;
    glyph.h     = (uint8_t)b.h;
    return &glyph;
}

int emojiAdvance(uint32_t cp) { return emojiFind(cp) != nullptr ? kEmojiAdvance : 0; }

int emojiTextWidth(const char* s, int len, const EmojiFaceMetrics& m) {
    if (s == nullptr || len <= 0) return 0;
    int w = 0;
    for (int i = 0; i < len;) {
        TextUnit u;
        const int next = textUnitAt(s, len, i, &u);
        if (next <= i) break;   // a walker that cannot advance must not spin
        i = next;
        if (u.zero) continue;   // a stray selector/keycap with no base takes no room
        const int e = emojiAdvance(u.cp);
        if (e > 0) {
            w += e;
            continue;
        }
        const int face = (m.advance != nullptr) ? m.advance(m.ctx, u.cp) : 0;
        w += face > 0 ? face : m.missing_w;
    }
    return w;
}

}  // namespace ui
}  // namespace om
