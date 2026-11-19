// SPDX-License-Identifier: MIT
// Chat layout: how a message becomes lines, and how many messages fit on the screen.
//
// Both questions are arithmetic, not drawing, so they live in core/ and the host tests pin them. The screen only
// asks "which lines does this message occupy" and "which messages does this window show"; it never decides a
// break point itself.
//
// Why a measure callback instead of a fixed character width: the real width of a line comes from the font, and
// the font is device data. The caller passes a function that measures a byte range, so the same wrap code runs
// on the host with a fake 10-px-per-character font (the tests) and on the P4 with the real VLW font.
//
// S503: message wrapping was a named gap — long texts were ellipsised on one line and scrolling inside a channel
// did not exist (Up/Down were swallowed to avoid jumping pages).
#pragma once

#include <stdint.h>

namespace om {
namespace mc {

// Four lines is what a 160-byte MeshCore text needs at this font and width, and it keeps the view struct small.
constexpr int kMaxLines = 4;

struct Line {
    uint16_t start = 0;   // byte offset into the source text
    uint16_t len   = 0;   // byte length, always ending on a UTF-8 boundary
};

struct Wrapped {
    Line    line[kMaxLines];
    uint8_t count   = 0;
    bool    clipped = false;   // true when the text did not fit in kMaxLines lines
};

// Measures the pixel width of a byte range of already-UTF-8 text.
using MeasureFn = int (*)(void* ctx, const char* ptr, int len);

// Greedy word wrap at `width` px. Rules, all host-tested:
//   · a break happens between words when one exists in the accepted span, otherwise inside the word (a URL or a
//     long word is hard-broken rather than pushed to its own line);
//   · a multi-byte UTF-8 sequence is never cut (a Slovenian č must not become two bytes of garbage);
//   · the spaces that a break consumes are not drawn at the start of the next line;
//   · empty text still occupies one (empty) line, so the caller's row height stays uniform;
//   · at most kMaxLines lines; anything left over sets `clipped`.
Wrapped wrapText(const char* text, int width, MeasureFn measure, void* ctx);

// Which messages a channel window shows. `lines[i]` is how many wrapped lines message i occupies (the caller gets
// them from wrapText), `line_h`/`gap_h` are pixels, `height` is the space available, and `scroll` counts messages
// skipped from the NEWEST end (0 = newest at the bottom, the normal state).
//   first..last  inclusive indices to draw, oldest first; `last < first` means nothing to draw;
//   max_scroll   the largest meaningful `scroll`: the point where the OLDEST message sits at the top of a full
//                window. It is 0 when everything already fits, so a key press in a short channel does nothing
//                instead of hiding the newest message (the caller must clamp its own scroll against this);
//   hidden_above / hidden_below  how many messages the window leaves out, for the on-screen indicators.
struct Window {
    int first        = 0;
    int last         = -1;
    int max_scroll   = 0;
    int hidden_above = 0;
    int hidden_below = 0;
};

Window chatWindow(const int* lines, int count, int line_h, int gap_h, int height, int scroll);

// S515/EST-469: the SAME window with the caller's scroll NOT clamped to max_scroll. Selection mode scrolls to a
// MESSAGE, not "back in history", and the oldest messages sit ABOVE the topmost full window chatWindow() allows
// (that bound exists so a plain Up press never hides the newest message while merely scrolling). Here `scroll` is
// clamped only to 0..count-1, and the selected message is reachable: chatSelScroll() puts it at the bottom.
Window chatWindowAt(const int* lines, int count, int line_h, int gap_h, int height, int scroll);

// How many of the loaded messages count as READ, given their on-card log ids (oldest first, ascending) and the
// seen point stored in the log's Meta record. Ids are consecutive per file, so this is a prefix length; it is a
// function rather than a loop in the caller because getting it wrong is silent: unread would either never clear
// or clear itself on every boot. A seen point of 0 (no Meta record yet) reads nothing.
int historyReadCount(const uint32_t* log_ids, int count, uint32_t seen_log_id);

}  // namespace mc
}  // namespace om
