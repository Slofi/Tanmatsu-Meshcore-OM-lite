// SPDX-License-Identifier: MIT
// VLW font decoding for the Tanmatsu app: 8-bit coverage per pixel, the same blobs the L2 draws with.
//
// The format is owned by the L2's generator (`firmware/tools/mkvlw.py`); `tools/mkfonts.py` only
// extracts its output into a flat .bin. Layout:
//
//     24-byte header  >IIIIII   glyph_count, encoder_version, y_advance, unused, ascent, descent
//     24 B per glyph  >IIIIiiI  code, h, w, advance, dy, dx, 0
//     glyph bitmaps             concatenated, h*w bytes each, 8-bit coverage, row-major
//
// `dy` is the distance from the BASELINE up to the glyph top (mkvlw: ascent - bbox_top), `dx` the left
// bearing. So a glyph drawn for a baseline at y_base sits at y = y_base - dy, x = pen_x + dx.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

struct Glyph {
    uint32_t code;
    uint16_t h, w;
    uint16_t advance;
    int16_t  dy, dx;
    uint32_t bitmap;  // byte offset into the blob's bitmap area
};

class Font {
  public:
    // Parse a blob. Returns false (and leaves the font unusable) if the header is not a VLW blob.
    bool load(const uint8_t* data, uint32_t len, const char* name);

    bool    ready() const { return ready_; }
    int16_t ascent() const { return ascent_; }
    int16_t descent() const { return descent_; }
    int16_t lineHeight() const { return ascent_ + descent_; }
    const char* name() const { return name_; }

    bool glyph(uint32_t code, Glyph* out) const;     // false when the face has no such glyph
    int          textWidth(const char* utf8) const;   // advance sum, in px
    int          textWidthN(const char* utf8, int max_px) const;  // stops when it would overflow
    int          textWidthRange(const char* utf8, int len) const;  // advance sum over a BYTE RANGE (S503)

    // Draw at (x, y_base) and return the pen's end x. `blend` mixes the coverage onto the framebuffer.
    int draw(void* fb, int fb_w, int fb_h, int x, int y_base, const char* utf8, uint16_t colour) const;

  private:
    const uint8_t* data_    = nullptr;
    uint32_t       len_     = 0;
    uint32_t       count_   = 0;
    uint32_t       bitmaps_ = 0;   // offset where the bitmaps start
    int16_t        ascent_  = 0;
    int16_t        descent_ = 0;
    bool           ready_   = false;
    char           name_[20] = {0};
};

// The six faces this app ships (embedded .bin files, see main/CMakeLists.txt EMBED_FILES).
enum class Face { Title, Row, Body, Meta, Hint, Kv };

void      loadFonts();
const Font* fontFor(Face f);

}  // namespace ui
}  // namespace om
