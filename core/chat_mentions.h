// SPDX-License-Identifier: MIT
// S516/EST-481: MENTIONS in the MeshCore form — "@[Name]".
//
// The Evidence (Claude, 2026-11-20): of 4,811 MeshCore messages OverMesh stored, 1,044 contain "@[Name]" and 15
// a plain "@Name". OverMesh's own reply writes `@[${name}] ` with [ ] CR LF removed from the name and trimmed
// (overmesh/static/js/app.js, replyToMc). Our reply used to write "@Name " — the L2's older plain form.
//
// Filip's rule for WHAT COUNTS is the position rule: an '@' starts a mention only at the FIRST byte of the text
// or after a space, so "a@b" never counts.
//
// Everything here is string arithmetic with no screen and no store, so the host tests pin it:
//   · the reply prefix (cleaning, empty after cleaning, the byte limit);
//   · the token finder (the position rule, unclosed "@[abc", nested brackets, an empty "@[]", UTF-8 names);
//   · mentionsUs (both forms, case, the separator rule, a name with spaces/parentheses like "TNMT (SI)");
//   · the DRAWN form used by the chat: brackets hidden, spans for the accent colour. The stored text is never
//     changed — the caller builds a separate copy. The wrap then measures THAT copy, so a hidden bracket can
//     never move a line break that the drawing does not also have (core/chat_layout.h's measure contract).
//
// No allocation. No display. The caller supplies every buffer.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"   // om::mc::kNameBytes: the name width the node table and the frames already use

namespace om {
namespace ui {

// One "@[Name]" token in a message. All offsets are BYTES of the stored text; `end` is one past the ']'.
struct MentionToken {
    uint16_t start       = 0;   // the '@'
    uint16_t content     = 0;   // the first byte of the name
    uint16_t content_len = 0;
    uint16_t end         = 0;   // one past the closing ']'
};

// A span of the DRAWN text that is a mention: the '@' plus the name (the whole "@Name", the accent's range).
struct MentionSpan {
    uint16_t start = 0;
    uint16_t len   = 0;
};

// The bound the chat view carries per message. Eight mentions in one 160-byte MeshCore text is already unusual;
// a token past the bound is still DRAWN correctly (brackets hidden), only its accent is lost — never the text.
constexpr int kMentionMaxSpans = 8;

// True when an '@' at `at` may start a mention: the first byte of the text, or after one space. This is the
// rule that makes "a@[b]" and "hi@TNMT" plain text (Filip, S516).
bool mentionAtBoundary(const char* text, size_t at);

// The first valid mention token at or after `from`. A token is "@[" + a non-empty name + "]" where the name
// carries no '[' or ']' (a nested bracket is not a name) and the '@' obeys the position rule. An UNCLOSED
// "@[abc" is not a token; the search continues after its '@', so a later well-formed token is still found.
bool mentionFind(const char* text, size_t from, MentionToken* out);

// Does this message mention `self`? Two forms, both under the position rule:
//   · "@[<self>]"  — the MeshCore form, exact name, ASCII case-insensitive;
//   · "@<self>"    — the plain form, followed by end of text or one of " ,.!?:;" (the L2's rule,
//                    firmware/src/mesh_link.cpp:657-682), also exact and ASCII case-insensitive.
// Bytes >= 0x80 are compared exactly (the L2 only case-folds ASCII), so "Čurek" keeps its exact bytes.
bool mentionsUs(const char* text, const char* self);

// OverMesh's cleaning of a name for the reply prefix: remove '[' ']' CR and LF, then trim leading/trailing
// spaces. False (and `out` empty) when nothing remains. Returns false too when `cap` cannot hold the result
// (never a silently cut name).
bool mentionCleanName(const char* in, char* out, size_t cap);

// The reply prefix, MeshCore's form: "@[<clean name>] " — returns the bytes written, or 0 (and an empty string)
// when the name is empty after cleaning or does not fit `cap`. A DM's reply adds no prefix at all; that rule
// lives with the caller.
size_t mentionReplyPrefix(char* out, size_t cap, const char* name);

// The result of building the drawn form.
struct MentionDrawn {
    size_t len       = 0;   // bytes written to `out` (always NUL-terminated when cap > 0)
    int    spans     = 0;   // mention spans recorded (at most max_spans)
    bool   truncated = false;   // `cap` was too small: `out` ends on a UTF-8 boundary
};

// The DRAWN form of `text`: every valid "@[Name]" becomes "@Name" (both brackets hidden) and its span is
// recorded in DRAWN coordinates. Everything else is copied byte for byte, so the stored text is never touched.
// `spans`/`max_spans` may be null/0 when only the string is wanted (the winding path still asks).
MentionDrawn mentionDrawn(const char* text, char* out, size_t cap, MentionSpan* spans, int max_spans);

}  // namespace ui
}  // namespace om
