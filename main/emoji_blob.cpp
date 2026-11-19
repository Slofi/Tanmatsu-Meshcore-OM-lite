// SPDX-License-Identifier: MIT
#include "emoji_blob.h"

#include <stdio.h>

#include "emoji.h"   // the subset table (core/emoji.cpp)

extern "C" {
// Embedded by main/CMakeLists.txt (EMBED_FILES "emoji/emoji_subset.bin"). IDF names the symbols after
// the file, not its folder, as with the fonts.
extern const uint8_t emoji_subset_bin_start[] asm("_binary_emoji_subset_bin_start");
extern const uint8_t emoji_subset_bin_end[] asm("_binary_emoji_subset_bin_end");
}

namespace om {
namespace ui {
namespace {
bool s_loaded = false;
}  // namespace

void loadEmoji() {
    if (s_loaded) return;
    s_loaded = true;
    const uint32_t size = (uint32_t)(emoji_subset_bin_end - emoji_subset_bin_start);
    const bool     ok   = emojiUseBlob(emoji_subset_bin_start, size);
    printf("[tm] emoji sub  %6u bytes  %d glyphs  %s\n", (unsigned)size, emojiTableCount(),
           ok ? "ok" : "FAILED");
}

}  // namespace ui
}  // namespace om
