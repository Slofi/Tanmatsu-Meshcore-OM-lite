// SPDX-License-Identifier: MIT
// The emoji subset blob, embedded in the app image (main/CMakeLists.txt EMBED_FILES) and handed to
// core/emoji.cpp, which parses it with the L2's own emoji_table.h. The .bin is built by
// tools/mkemoji_subset.py from the L2's firmware/src/ui/emoji_data.bin (never at build time on the
// device, and never committed from a different source).
#pragma once

namespace om {
namespace ui {

// Parse the embedded subset once and log its size, glyph count and verdict. Idempotent; a blob the
// L2 parser rejects leaves every emoji at today's behaviour (the face's missing-glyph advance).
void loadEmoji();

}  // namespace ui
}  // namespace om
