// SPDX-License-Identifier: MIT
#include "chat_layout.h"

#include <string.h>

namespace om {
namespace mc {
namespace {

// Length in bytes of the UTF-8 sequence starting at p (1 for ASCII, 1 for a broken byte so the walk always
// advances). Never reads past `end`.
int seqLen(const char* p, const char* end) {
    const unsigned char c = (unsigned char)*p;
    int                 n = 1;
    if (c >= 0xF0)
        n = 4;
    else if (c >= 0xE0)
        n = 3;
    else if (c >= 0xC0)
        n = 2;
    if (p + n > end) return 1;
    // A continuation byte where a lead byte should be: treat it as one byte rather than swallow a neighbour.
    for (int i = 1; i < n; ++i) {
        if (((unsigned char)p[i] & 0xC0) != 0x80) return 1;
    }
    return n;
}

bool isSpace(char c) { return c == ' ' || c == '\t'; }

}  // namespace

Wrapped wrapText(const char* text, int width, MeasureFn measure, void* ctx) {
    Wrapped out;
    if (text == nullptr || text[0] == '\0') {
        out.count = 1;   // one empty line: the caller's row height must not depend on the text
        return out;
    }
    const char* const begin = text;
    const char* const end   = text + strlen(text);
    if (width <= 0 || measure == nullptr) {
        // No usable geometry: one line with everything. The screen clips it, which is the old behaviour and
        // never worse than dropping the text.
        out.line[0].start = 0;
        out.line[0].len   = (uint16_t)(end - begin);
        out.count         = 1;
        return out;
    }

    const char* p = begin;
    while (p < end && out.count < kMaxLines) {
        while (p < end && isSpace(*p)) ++p;   // a break consumes the spaces it broke at
        if (p >= end) break;

        // How far can this line reach? Advance a whole UTF-8 sequence at a time and keep the last position that
        // still fits. `lastSpace` remembers the best place to break between words.
        const char* lineStart = p;
        const char* fitsEnd   = p;    // one past the last sequence that fit
        const char* lastSpace = nullptr;
        const char* q         = p;
        while (q < end) {
            const int n = seqLen(q, end);
            if (measure(ctx, lineStart, (int)(q + n - lineStart)) > width) break;
            q += n;
            fitsEnd = q;
            if (q < end && isSpace(*q)) lastSpace = q;
        }

        bool clippedHere = false;
        if (fitsEnd == lineStart) {
            // Not even one character fits (a very narrow column): take one sequence so the walk always advances.
            fitsEnd = lineStart + seqLen(lineStart, end);
            clippedHere = true;
        }

        const char* lineEnd = fitsEnd;
        if (fitsEnd < end && lastSpace != nullptr && lastSpace > lineStart) {
            lineEnd = lastSpace;   // break between words
        }

        out.line[out.count].start = (uint16_t)(lineStart - begin);
        out.line[out.count].len   = (uint16_t)(lineEnd - lineStart);
        ++out.count;

        p = lineEnd;
        if (lineEnd == fitsEnd && clippedHere) {
            // Could not place this sequence at all; stop rather than loop on it.
            out.clipped = true;
            break;
        }
    }

    if (p < end) {
        const char* rest = p;
        while (rest < end && isSpace(*rest)) ++rest;
        if (rest < end) out.clipped = true;   // text is left over: the last drawn line gets an ellipsis
    }
    if (out.count == 0) {
        out.line[0].start = 0;
        out.line[0].len   = 0;
        out.count         = 1;
    }
    return out;
}

namespace {

// Walk back from `last` while the blocks still fit. The newest message is always included, even when one
// message alone is taller than the window: the alternative is a blank screen with a scroll indicator.
int firstFrom(const int* lines, int line_h, int gap_h, int height, int last) {
    int used  = 0;
    int first = last;
    for (int i = last; i >= 0; --i) {
        const int n     = (lines[i] > 0) ? lines[i] : 1;
        const int block = n * line_h + gap_h;
        if (i != last && used + block > height) break;
        used += block;
        first = i;
    }
    return first;
}

}  // namespace

Window chatWindowAt(const int* lines, int count, int line_h, int gap_h, int height, int scroll) {
    Window w;
    if (count <= 0 || lines == nullptr) {
        w.last = -1;
        return w;
    }
    if (line_h <= 0) line_h = 1;
    if (gap_h < 0) gap_h = 0;

    w.max_scroll = firstFrom(lines, line_h, gap_h, height, count - 1);

    // Only 0..count-1: the caller follows a selected MESSAGE, and every message must be reachable, including the
    // oldest one above the normal topmost window.
    if (scroll < 0) scroll = 0;
    if (scroll > count - 1) scroll = count - 1;

    const int last  = count - 1 - scroll;
    const int first = firstFrom(lines, line_h, gap_h, height, last);

    w.first        = first;
    w.last         = last;
    w.hidden_above = first;
    w.hidden_below = count - 1 - last;
    return w;
}

Window chatWindow(const int* lines, int count, int line_h, int gap_h, int height, int scroll) {
    Window w;
    if (count <= 0 || lines == nullptr) {
        w.last = -1;
        return w;
    }
    if (line_h <= 0) line_h = 1;
    if (gap_h < 0) gap_h = 0;

    // How far back scrolling MEANS anything: as far as it takes to put the oldest message at the top of a full
    // window. When everything already fits, that is 0 — pressing Up in a short channel must not hide the newest
    // message to reveal history that is not there (S503, caught on the glass at three messages).
    w.max_scroll = firstFrom(lines, line_h, gap_h, height, count - 1);

    if (scroll < 0) scroll = 0;
    if (scroll > w.max_scroll) scroll = w.max_scroll;

    const int last  = count - 1 - scroll;
    const int first = firstFrom(lines, line_h, gap_h, height, last);

    w.first        = first;
    w.last         = last;
    w.hidden_above = first;
    w.hidden_below = count - 1 - last;
    return w;
}

int historyReadCount(const uint32_t* log_ids, int count, uint32_t seen_log_id) {
    if (log_ids == nullptr || count <= 0 || seen_log_id == 0) return 0;
    int read = 0;
    for (int i = 0; i < count; ++i) {
        if (log_ids[i] == 0) continue;                      // not on the card: cannot be claimed as read
        if (log_ids[i] > seen_log_id) break;                // ids ascend with the file, so this ends the prefix
        read = i + 1;
    }
    return read;
}

}  // namespace mc
}  // namespace om
