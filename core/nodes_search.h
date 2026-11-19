// SPDX-License-Identifier: MIT
// The Nodes page's search rule, ported from the L2's S508 `firmware/src/ui/nodes_search.h` (S514/EST-449).
// Plain C++ so test/host proves it with the system g++: what a typed query matches, the "N of M" the bar
// shows, and the focus moves between the bar and the list.
//
// Filip, S508, corrected on the L2's glass: ONE input field (not two), and text never on the field's border.
// Here the field is the TOP ROW of the Nodes list: always visible, the query on the left, "N of M" on the
// right, and the list below filters as each letter arrives.
//
// One rule, unchanged from the L2: a node is shown when its NAME contains what was typed, ignoring case.
// Nothing else is searched (not the path text, not the age, not the type), because the list shows names and
// a row that matched on something invisible would be a mystery to the reader.
#pragma once

#include <stddef.h>
#include <stdio.h>

#include "mc_store.h"

namespace om {
namespace ui {

// One typed UTF-8 byte per event (the composer's rule), so the field holds whole sequences; 39 bytes + NUL
// is more than any name the contact table can carry and keeps the buffer small on the main task.
constexpr int kNodeQueryBytes = 40;

// ASCII fold only: a contact name may carry UTF-8 bytes and they are compared as they are, which is exactly
// how two names that look the same compare. No locale and no case table in flash.
constexpr char nodeSearchFold(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// True when the node belongs in the filtered list. An empty (or null) query matches every node, which is
// what makes "cleared" and "not searching" the same picture on the glass.
inline bool nodeSearchMatches(const char* name, const char* query) {
  if (query == nullptr || query[0] == 0) return true;
  if (name == nullptr) return false;
  for (const char* start = name; *start != 0; ++start) {
    const char* n = start;
    const char* q = query;
    while (*n != 0 && *q != 0 && nodeSearchFold(*n) == nodeSearchFold(*q)) {
      ++n;
      ++q;
    }
    if (*q == 0) return true;   // the whole query fitted inside the name
  }
  return false;
}

// The one number the bar shows, "2 of 6". Both counts are clamped to three digits (a title line, not a
// table); an empty contact table reads "0 of 0".
inline void nodeSearchCountText(char* out, size_t cap, int matches, int total) {
  if (out == nullptr || cap == 0) return;
  const int m = matches < 0 ? 0 : (matches > 999 ? 999 : matches);
  const int t = total < 0 ? 0 : (total > 999 ? 999 : total);
  snprintf(out, cap, "%d of %d", m, t);
}

// The SHOWN order: walk the Nodes page's own order (favourites first, then newest heard) and keep the
// contacts whose name matches, writing their CONTACT indices into `out` (may be null to count only).
// Returns how many were kept. An empty query keeps the whole order, so the list is identical to "no search".
inline int nodeSearchFilter(const om::mc::ContactStore* store, const int* order, int order_count,
                            const char* query, int* out) {
  if (store == nullptr || order == nullptr || order_count <= 0) return 0;
  int n = 0;
  for (int i = 0; i < order_count; ++i) {
    const int ci = order[i];
    if (ci < 0 || ci >= (int)store->count) continue;   // a stale row id matches nobody
    if (!nodeSearchMatches(store->c[ci].name, query)) continue;
    if (out != nullptr) out[n] = ci;
    ++n;
  }
  return n;
}

// --------------------------------------------------------------------- typing (the composer's own rules)
// Append one byte. A UTF-8 letter is stored as its bytes (the Tanmatsu BSP hands the whole sequence in
// `args_keyboard.utf8[]`; the caller feeds it here byte by byte), so the font draws it only when all of
// them are there. Appending stops before the buffer overflows.
inline bool nodeSearchAppend(char* buf, size_t cap, size_t* len, char c) {
  if (buf == nullptr || len == nullptr || cap == 0) return false;
  if (*len + 1 >= cap) return false;
  buf[*len] = c;
  ++(*len);
  buf[*len] = 0;
  return true;
}

// Delete ONE UTF-8 sequence, not one byte: a Slovenian letter must not become garbage. Returns false when
// there was nothing to delete.
inline bool nodeSearchBackspace(char* buf, size_t* len) {
  if (buf == nullptr || len == nullptr || *len == 0) return false;
  size_t n = *len;
  do {
    --n;
  } while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80);
  *len = n;
  buf[n] = 0;
  return true;
}

// -------------------------------------------------------------------------- focus: the bar and the list
// Up: in the list, the FIRST row steps up into the bar; a deeper row moves up by one. In the bar, Up does
// nothing (returns false). `row` is an index into the FILTERED list.
inline bool nodesFocusUp(bool* bar, int* row) {
  if (bar == nullptr || row == nullptr || *bar) return false;
  if (*row <= 0) {
    *bar = true;
    *row = 0;
  } else {
    --(*row);
  }
  return true;
}

// Down: from the bar to the first MATCH (stays in the bar when nothing matches); in the list, down by one,
// clamped to the last match.
inline bool nodesFocusDown(bool* bar, int* row, int matches) {
  if (bar == nullptr || row == nullptr) return false;
  if (*bar) {
    if (matches <= 0) return false;
    *bar = false;
    *row = 0;
    return true;
  }
  if (*row + 1 < matches) {
    ++(*row);
    return true;
  }
  return false;
}

// Esc on the Nodes page. In the list it is the page's back key, so the caller keeps it. In the bar it first
// clears a non-empty query (and stays); once the query is empty, the NEXT Esc leaves for the list.
enum class NodesEscResult { LeavePage, Cleared, LeftBar };

inline NodesEscResult nodesFocusEsc(bool* bar, char* query, size_t* len, int* row) {
  if (bar == nullptr || !*bar) return NodesEscResult::LeavePage;
  if (query != nullptr && len != nullptr && *len > 0) {
    query[0] = 0;
    *len = 0;
    if (row != nullptr) *row = 0;
    return NodesEscResult::Cleared;
  }
  *bar = false;
  if (row != nullptr) *row = 0;
  return NodesEscResult::LeftBar;
}

// '/' from anywhere on the Nodes page puts the focus in the bar. It is a shortcut, not a character: the
// caller must not also append it.
inline bool nodesSlashOpensSearch(char c) { return c == '/'; }

// The advert key is the LIST's key. While the bar has focus a typed 'a' is TEXT, so only a list-focused 'a'
// may send. One predicate, so the key handler and the test cannot disagree.
inline bool nodesAdvertAllowed(bool bar) { return !bar; }

}  // namespace ui
}  // namespace om
