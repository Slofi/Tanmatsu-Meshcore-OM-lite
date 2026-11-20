// SPDX-License-Identifier: MIT
// S516/EST-481: typing an @mention in the chat composer — the KEYSTROKE rules and the name picker's ranking.
//
// Filip's words (S516, 22:5x-23:0x): *"when the @ is typed, automatically add the first '[' after it. If it is
// deleted, no mention is done.."* then *"no, also the closing ], and the way to get 'past it' is to press -> key..
// so in practice, after I type @ there is a 'preset' of [ ] waiting to insert a name, with suggestion is a
// pop-up window with nodes matching already typed letters.."*
//
// The composer today is APPEND-ONLY. The minimum cursor this ticket adds is exactly what the rule needs: the
// caret sits at the END of the draft, or just before the auto ']' of an OPEN mention. Nothing else is editable,
// so a mention is always the TAIL of the draft ("…@[typed]") and no general cursor arithmetic is needed. The
// state is one bool; a second one (`key` in the shell) is the pop-up, which Esc closes without leaving the mode.
//
// What is pinned here, and why it is pure: every case in the ticket's list is a silent failure on the glass —
// an '@' that opens brackets mid-word, Enter that sends while a name is still being picked, a "]]" at the end.
// All functions take the caller's draft buffer; none allocates.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "chat_mentions.h"   // mentionCleanName, om::mc::kNameBytes

namespace om {
namespace ui {

constexpr int kMentionPickRows = 5;   // the ticket's "up to 5"

// ---- the draft ----------------------------------------------------------------------------------------------
// An OPEN mention is the tail "@[<typed>]" of the draft; `mentionOpenIndex` is -1 for anything else. The check
// is deliberately strict (no '[' or ']' inside the typed name), because the picker and the drawing both rely on
// "the first ']' from the end closes it".
int mentionOpenIndex(const char* buf, size_t len);

// What a typed '@' did. The function ALWAYS appends the '@' it was given:
//   Plain    — mid-word ('a@'), or no room for the brackets: the '@' is a plain character;
//   Opened   — "@[]" appended at the position rule (empty draft, or the previous byte is a space);
//   NoRoom   — there was not even room for the '@'; nothing changed.
enum class MentionAt : uint8_t { Plain = 0, Opened = 1, NoRoom = 2 };
MentionAt mentionTypeAt(char* buf, size_t* len, size_t cap, bool* active);

// One typed character while the mode is on (or off: Ignored means "the composer's normal typing applies").
//   letters go between the brackets (before the auto ']');
//   ']' steps OVER the auto bracket: the mode ends, nothing is inserted (never "]]");
//   '[' is ignored inside the brackets (a nested bracket is not a name, so it can never be typed);
//   a full buffer ignores the character (the composer's byte limit, as today).
enum class MentionType : uint8_t { Ignored = 0, Inserted = 1, SteppedOver = 2 };
MentionType mentionTypeChar(char* buf, size_t* len, size_t cap, bool* active, char c);

// The typed letters, for the picker ("" when the mode is off). Returns the length.
size_t mentionQuery(const char* buf, size_t len, char* out, size_t cap);

// Right arrow, or Enter with no suggestion: leave the mode where it is (the typed text stays exactly as typed,
// the auto ']' stays). Returns true when the mode was on.
bool mentionLeave(bool* active);

// Enter on a suggestion: the bracket content becomes the CLEANED name, the cursor goes past ']' and a space is
// added; the mode ends. A name the buffer cannot hold changes NOTHING and answers TooLong — the shell says so
// ("no room for that name") rather than cutting a name in half and mentioning the wrong node.
enum class MentionPick : uint8_t { Picked = 0, TooLong = 1, NotActive = 2 };
MentionPick mentionPick(char* buf, size_t* len, size_t cap, bool* active, const char* name);

// Backspace: one UTF-8 sequence of the typed name; on an EMPTY "@[]" BOTH brackets go and a plain '@' is left
// (the mode ends — "if it is deleted, no mention is done").
enum class MentionBack : uint8_t { RemovedChar = 0, RemovedBrackets = 1, NotActive = 2 };
MentionBack mentionBackspace(char* buf, size_t* len, bool* active);

// ---- the suggestion ranking ---------------------------------------------------------------------------------
// One candidate, COPIED out of the node table under its lock (never a pointer into it): the name, the two flags
// the pop-up tags, and the recency the order uses.
struct MentionRow {
    char     name[om::mc::kNameBytes] = "";
    bool     favourite                = false;
    bool     in_radio                 = false;
    uint32_t heard_at                 = 0;
};

// Case-insensitive substring, as the ticket asks. Empty query matches everything (the whole list is a
// suggestion once the brackets are typed); bytes >= 0x80 are compared exactly.
bool mentionMatches(const char* name, const char* query);

// The ticket's order: favourites first, then rows in the radio, then SD/session rows; inside a group the most
// recently heard first; a name tie is broken by the name so the answer is deterministic.
bool mentionOutranks(const MentionRow& a, const MentionRow& b);

// Insert `cand` into a sorted top-`max` list. Deduplicated by name (case-insensitive): two rows with the same
// name would offer the same insertion twice, and the better-flagged one keeps the slot. Returns the row index,
// or -1 when the candidate did not make the list.
int mentionInsert(MentionRow* rows, int* count, int max, const MentionRow& cand);

}  // namespace ui
}  // namespace om
