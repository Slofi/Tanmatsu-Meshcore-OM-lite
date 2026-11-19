// SPDX-License-Identifier: MIT
#include "chat_meta.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace mc {
namespace {

void appendStr(char* out, size_t cap, size_t* n, const char* s) {
    if (s == nullptr || *n + 1 >= cap) return;
    const int w = snprintf(out + *n, cap - *n, "%s", s);
    if (w > 0) *n += (size_t)w;
    if (*n >= cap) *n = cap - 1;
    out[*n] = '\0';
}

// " · " between two segments, and nothing at the start of the line.
void appendSep(char* out, size_t cap, size_t* n) {
    if (*n > 0) appendStr(out, cap, n, " \xC2\xB7 ");
}

}  // namespace

bool pathHopLabel(const ContactStore& s, const uint8_t* hash, uint8_t len, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return false;
    out[0] = '\0';
    if (hash == nullptr || len < 1 || len > 3) return false;

    int    match = -1;
    size_t found = 0;
    for (size_t i = 0; i < s.count && i < kMaxContacts; ++i) {
        const Contact& c = s.c[i];
        if (contactKeyIncomplete(c.pub)) continue;   // a prefix-only row is not a node
        if (memcmp(c.pub, hash, len) != 0) continue;
        ++found;
        match = (int)i;
        if (found > 1) break;                         // two is already "never guess"
    }
    if (found == 1 && match >= 0 && s.c[match].name[0] != '\0') {
        snprintf(out, cap, "%s", s.c[match].name);
        return true;
    }

    // The hash itself, byte for byte: "86a8" for a 2-byte hash. Lowercase, like every other key we print.
    size_t n = 0;
    for (uint8_t i = 0; i < len && n + 3 <= cap; ++i) {
        const int w = snprintf(out + n, cap - n, "%02x", (unsigned)hash[i]);
        if (w <= 0) break;
        n += (size_t)w;
    }
    return false;
}

void chatMetaText(const MsgMeta& m, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    size_t n = 0;

    if (!m.outgoing) {
        if (m.hops_known) {
            if (m.hops == 0) {
                appendStr(out, cap, &n, "direct");
            } else {
                char hops[24];
                snprintf(hops, sizeof(hops), "%u hop%s", (unsigned)m.hops, m.hops == 1 ? "" : "s");
                appendStr(out, cap, &n, hops);
                if (m.via[0] != '\0') {
                    appendStr(out, cap, &n, " via ");
                    appendStr(out, cap, &n, m.via);
                }
            }
        }
        if (m.bytes > 0) {
            appendSep(out, cap, &n);
            char b[24];
            snprintf(b, sizeof(b), "%u B", (unsigned)m.bytes);
            appendStr(out, cap, &n, b);
        }
        // One copy is already "the line that arrived"; a second is the fact worth the ink.
        if (m.copies >= 2) {
            appendSep(out, cap, &n);
            char c[24];
            snprintf(c, sizeof(c), "heard %u\xC3\x97", (unsigned)m.copies);
            appendStr(out, cap, &n, c);
        }
        return;
    }

    // Our own line: the delivery word already follows the time, so only bytes and echoes live here.
    if (m.bytes > 0) {
        char b[24];
        snprintf(b, sizeof(b), "%u B", (unsigned)m.bytes);
        appendStr(out, cap, &n, b);
    }
    if (m.copies >= 2) {
        appendSep(out, cap, &n);
        char c[24];
        snprintf(c, sizeof(c), "repeated %u\xC3\x97", (unsigned)m.copies);
        appendStr(out, cap, &n, c);
    }
}

}  // namespace mc
}  // namespace om
