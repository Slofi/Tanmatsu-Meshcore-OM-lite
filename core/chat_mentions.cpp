// SPDX-License-Identifier: MIT
#include "chat_mentions.h"
#include "chat_mention_edit.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace ui {
namespace {

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c; }

// The L2's comparison: byte by byte, ASCII case-insensitive, everything >= 0x80 exact.
bool equalFold(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}

bool isSeparator(char c) {
    return c == '\0' || c == ' ' || c == ',' || c == '.' || c == '!' || c == '?' || c == ':' || c == ';';
}

// The byte length of the UTF-8 sequence starting at `s[0]` (1..4), clamped to what is left. An invalid lead
// byte is one byte, so a broken sequence is copied as-is rather than swallowed.
size_t utf8SeqLen(const char* s, size_t left) {
    const unsigned char c = (unsigned char)s[0];
    size_t                n = 1;
    if ((c & 0xE0) == 0xC0) n = 2;
    else if ((c & 0xF0) == 0xE0) n = 3;
    else if ((c & 0xF8) == 0xF0) n = 4;
    return n <= left ? n : 1;
}

}  // namespace

bool mentionAtBoundary(const char* text, size_t at) {
    if (text == nullptr) return false;
    if (at == 0) return true;
    return text[at - 1] == ' ';
}

bool mentionFind(const char* text, size_t from, MentionToken* out) {
    if (text == nullptr) return false;
    for (size_t at = from; text[at] != '\0'; ++at) {
        if (text[at] != '@') continue;
        // Filip's rule: an '@' mid-word is just an '@'. (Checked before the '[' so "a@[b]" never resolves.)
        if (!mentionAtBoundary(text, at)) continue;
        if (text[at + 1] != '[') continue;

        const size_t content = at + 2;
        size_t       j       = content;
        bool         nested  = false;
        while (text[j] != '\0' && text[j] != ']') {
            if (text[j] == '[') {
                nested = true;
                break;
            }
            ++j;
        }
        if (nested) continue;                // "@[a[b]c]": not a name; a later '@' may still start one
        if (text[j] == '\0') continue;       // unclosed "@[abc": not a token
        if (j == content) continue;          // an empty "@[]" names nobody
        if (out != nullptr) {
            out->start       = (uint16_t)at;
            out->content     = (uint16_t)content;
            out->content_len = (uint16_t)(j - content);
            out->end         = (uint16_t)(j + 1);
        }
        return true;
    }
    return false;
}

bool mentionsUs(const char* text, const char* self) {
    if (text == nullptr || self == nullptr) return false;
    const size_t n = strlen(self);
    if (n == 0) return false;

    for (size_t at = 0; text[at] != '\0'; ++at) {
        if (text[at] != '@') continue;
        if (!mentionAtBoundary(text, at)) continue;

        // 1. the MeshCore form: "@[<self>]", exact name (case-insensitive).
        MentionToken tok;
        if (mentionFind(text, at, &tok) && tok.start == at) {
            if ((size_t)tok.content_len == n && equalFold(text + tok.content, self, n)) return true;
            // Not us: the token's own '@' cannot start another token, but bytes after it can be searched.
            at = tok.end - 1;
            continue;
        }

        // 2. the L2's plain form: "@<self>" then the end or a separator. A "[" right after the '@' is the
        //    bracket form's territory (already tested above), and self can never start with '['.
        if (text[at + 1] == '[') continue;
        if (strlen(text + at + 1) < n) continue;
        if (!equalFold(text + at + 1, self, n)) continue;
        if (isSeparator(text[at + 1 + n])) return true;
    }
    return false;
}

bool mentionCleanName(const char* in, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return false;
    out[0] = '\0';
    if (in == nullptr) return false;

    // Pass 1: the byte count that survives the bracket/CR/LF removal. A cap that cannot hold the WHOLE cleaned
    // name answers false — never a silently cut name.
    size_t total = 0;
    for (size_t i = 0; in[i] != '\0'; ++i) {
        const char c = in[i];
        if (c == '[' || c == ']' || c == '\r' || c == '\n') continue;
        ++total;
    }
    if (total == 0 || total + 1 > cap) return false;

    // Pass 2: the copy, then the trim (leading and trailing spaces only).
    size_t n = 0;
    for (size_t i = 0; in[i] != '\0'; ++i) {
        const char c = in[i];
        if (c == '[' || c == ']' || c == '\r' || c == '\n') continue;
        out[n++] = c;
    }
    out[n] = '\0';

    size_t a = 0;
    while (a < n && out[a] == ' ') ++a;
    size_t b = n;
    while (b > a && out[b - 1] == ' ') --b;
    if (b == a) {
        out[0] = '\0';
        return false;
    }
    memmove(out, out + a, b - a);
    out[b - a] = '\0';
    return true;
}

size_t mentionReplyPrefix(char* out, size_t cap, const char* name) {
    if (out == nullptr || cap == 0) return 0;
    out[0] = '\0';
    char clean[om::mc::kNameBytes];
    if (!mentionCleanName(name, clean, sizeof(clean))) return 0;

    // "@[" + name + "] " + NUL
    const size_t need = 2 + strlen(clean) + 3;
    if (need > cap) return 0;
    const int w = snprintf(out, cap, "@[%s] ", clean);
    if (w <= 0 || (size_t)w >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w;
}

MentionDrawn mentionDrawn(const char* text, char* out, size_t cap, MentionSpan* spans, int max_spans) {
    MentionDrawn r;
    if (out == nullptr || cap == 0) return r;
    out[0] = '\0';
    if (text == nullptr) return r;

    const size_t n = strlen(text);
    size_t       at = 0, o = 0;
    while (at < n) {
        MentionToken tok;
        if (text[at] == '@' && mentionFind(text, at, &tok) && tok.start == at) {
            // "@[Name]" -> "@Name": one byte less on each side, so the drawn form always fits when the stored
            // one did (the cap check is still here for a caller's smaller buffer).
            const size_t need = 1 + (size_t)tok.content_len;
            if (o + need + 1 > cap) {
                r.truncated = true;
                break;
            }
            const size_t span_start = o;
            out[o++]                = '@';
            memcpy(out + o, text + tok.content, tok.content_len);
            o += tok.content_len;
            if (spans != nullptr && r.spans < max_spans) {
                spans[r.spans].start = (uint16_t)span_start;
                spans[r.spans].len   = (uint16_t)need;
                ++r.spans;
            }
            at = tok.end;
            continue;
        }

        // Copy one whole UTF-8 sequence. A cap that cannot hold it ends the copy on a boundary.
        const size_t seq = utf8SeqLen(text + at, n - at);
        if (o + seq + 1 > cap) {
            r.truncated = true;
            break;
        }
        memcpy(out + o, text + at, seq);
        o += seq;
        at += seq;
    }
    out[o] = '\0';
    r.len  = o;
    return r;
}

// ==================================================================== the typing rules and the picker (S516/EST-481)
namespace {

bool sameName(const char* a, const char* b) {
    const size_t na = strlen(a), nb = strlen(b);
    return na == nb && equalFold(a, b, na);
}

}  // namespace

int mentionOpenIndex(const char* buf, size_t len) {
    if (buf == nullptr || len < 3) return -1;
    if (buf[len - 1] != ']') return -1;
    size_t i = len - 1;
    while (i > 0) {
        --i;
        if (buf[i] == ']') return -1;   // a second ']': the tail is not one clean mention
        if (buf[i] == '[') {
            if (i == 0 || buf[i - 1] != '@') return -1;
            return (int)i;
        }
    }
    return -1;
}

MentionAt mentionTypeAt(char* buf, size_t* len, size_t cap, bool* active) {
    if (buf == nullptr || len == nullptr || active == nullptr) return MentionAt::NoRoom;
    if (*active) return MentionAt::Plain;          // a caller bug must not open a second bracket
    if (*len + 1 >= cap) return MentionAt::NoRoom;

    const bool opens = (*len == 0) || (buf[*len - 1] == ' ');
    buf[(*len)++] = '@';
    buf[*len]     = '\0';

    // "@[]" needs three bytes plus the NUL. When it does not fit, the '@' stays a plain character: no half a
    // mention can ever reach the wire (the ticket's byte-limit rule).
    if (opens && *len + 3 < cap) {
        buf[(*len)++] = '[';
        buf[(*len)++] = ']';
        buf[*len]     = '\0';
        *active       = true;
        return MentionAt::Opened;
    }
    return MentionAt::Plain;
}

MentionType mentionTypeChar(char* buf, size_t* len, size_t cap, bool* active, char c) {
    if (buf == nullptr || len == nullptr || active == nullptr || !*active) return MentionType::Ignored;
    const int open = mentionOpenIndex(buf, *len);
    if (open < 0) {
        *active = false;   // the draft was edited behind the mode's back: end it rather than corrupt it
        return MentionType::Ignored;
    }
    if (c == ']') {
        *active = false;   // step OVER the auto ']': no second bracket, the typed text stays
        return MentionType::SteppedOver;
    }
    if (c == '[' || c == '\0') return MentionType::Ignored;   // a nested bracket is not a name
    if (*len + 2 > cap) return MentionType::Ignored;            // the composer is full

    // Insert before the auto ']' (the last byte), moving it and the NUL right by one.
    memmove(buf + *len, buf + *len - 1, 2);
    buf[*len - 1] = c;
    ++*len;
    return MentionType::Inserted;
}

size_t mentionQuery(const char* buf, size_t len, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return 0;
    out[0] = '\0';
    const int open = mentionOpenIndex(buf, len);
    if (open < 0) return 0;
    const size_t start = (size_t)open + 1;
    size_t       n     = len - 1 - start;   // between '[' and the closing ']'
    if (n > cap - 1) n = cap - 1;
    memcpy(out, buf + start, n);
    out[n] = '\0';
    return n;
}

bool mentionLeave(bool* active) {
    if (active == nullptr || !*active) return false;
    *active = false;
    return true;
}

MentionPick mentionPick(char* buf, size_t* len, size_t cap, bool* active, const char* name) {
    if (buf == nullptr || len == nullptr || active == nullptr || !*active) return MentionPick::NotActive;
    const int open = mentionOpenIndex(buf, *len);
    if (open < 0) {
        *active = false;
        return MentionPick::NotActive;
    }
    char clean[om::mc::kNameBytes];
    if (!mentionCleanName(name, clean, sizeof(clean))) return MentionPick::TooLong;

    const size_t clean_len = strlen(clean);
    const size_t total     = (size_t)open + 1 + clean_len + 2;   // "@[" + name + "] "
    if (total + 1 > cap) return MentionPick::TooLong;             // refused, the draft is untouched

    memcpy(buf + open + 1, clean, clean_len);
    buf[open + 1 + clean_len]     = ']';
    buf[open + 2 + clean_len]     = ' ';
    buf[total]                    = '\0';
    *len                          = total;
    *active                       = false;
    return MentionPick::Picked;
}

MentionBack mentionBackspace(char* buf, size_t* len, bool* active) {
    if (buf == nullptr || len == nullptr || active == nullptr || !*active) return MentionBack::NotActive;
    const int open = mentionOpenIndex(buf, *len);
    if (open < 0) {
        *active = false;
        return MentionBack::NotActive;
    }
    const size_t content_start = (size_t)open + 1;
    const size_t content_end   = *len - 1;   // the closing ']'
    if (content_end == content_start) {
        // An EMPTY "@[]": one Backspace removes BOTH brackets and leaves the plain '@' ("no mention is done").
        buf[open] = '\0';
        *len      = (size_t)open;
        *active   = false;
        return MentionBack::RemovedBrackets;
    }

    // Remove one whole UTF-8 sequence from the end of the typed name.
    size_t s = content_end - 1;
    while (s > content_start && ((unsigned char)buf[s] & 0xC0) == 0x80) --s;
    memmove(buf + s, buf + content_end, *len - content_end + 1);   // ']' and the NUL move left
    *len -= content_end - s;
    return MentionBack::RemovedChar;
}

bool mentionMatches(const char* name, const char* query) {
    if (name == nullptr) return false;
    if (query == nullptr || query[0] == '\0') return true;
    const size_t nn = strlen(name), nq = strlen(query);
    if (nq > nn) return false;
    for (size_t i = 0; i + nq <= nn; ++i) {
        if (equalFold(name + i, query, nq)) return true;
    }
    return false;
}

bool mentionOutranks(const MentionRow& a, const MentionRow& b) {
    const int ga = a.favourite ? 0 : (a.in_radio ? 1 : 2);
    const int gb = b.favourite ? 0 : (b.in_radio ? 1 : 2);
    if (ga != gb) return ga < gb;
    if (a.heard_at != b.heard_at) return a.heard_at > b.heard_at;
    return strcmp(a.name, b.name) < 0;
}

int mentionInsert(MentionRow* rows, int* count, int max, const MentionRow& cand) {
    if (rows == nullptr || count == nullptr || max <= 0) return -1;
    if (cand.name[0] == '\0') return -1;

    // One name, one row: two nodes sharing a name would offer the same insertion twice. The better-flagged
    // copy keeps the slot.
    for (int i = 0; i < *count; ++i) {
        if (!sameName(rows[i].name, cand.name)) continue;
        if (!mentionOutranks(cand, rows[i])) return i;
        memmove(rows + i, rows + i + 1, (size_t)(*count - i - 1) * sizeof(MentionRow));
        --*count;
        break;
    }

    int pos = *count;
    for (int i = 0; i < *count; ++i) {
        if (mentionOutranks(cand, rows[i])) {
            pos = i;
            break;
        }
    }
    if (pos >= max) return -1;   // did not make the top `max`

    if (*count < max) {
        memmove(rows + pos + 1, rows + pos, (size_t)(*count - pos) * sizeof(MentionRow));
        rows[pos] = cand;
        ++*count;
    } else {
        memmove(rows + pos + 1, rows + pos, (size_t)(max - 1 - pos) * sizeof(MentionRow));
        rows[pos] = cand;
    }
    return pos;
}

}  // namespace ui
}  // namespace om
