// SPDX-License-Identifier: MIT
// S515/EST-461: the Nodes page's NEW contact — its frozen order and its acknowledgement (Filip: a red dot on a
// newly heard node "until a selection box is hovered over it, or scrolled over it").
//
// Two rules, both decidable without a display, so test/host pins them:
//
// 1. THE ORDER IS FROZEN WHILE THE PAGE IS OPEN. A list that re-sorts under the cursor is worse than a late
//    sort: the row is acked at once (its dot goes) but it keeps its place until the user leaves Nodes. The
//    freeze is DATA — an array of STORE indices taken on entry — not a timer: the page draws the snapshot,
//    appends a contact that arrived while it was open, and re-sorts only on the next entry.
//
// 2. AN ACK IS A MOVE OR A DWELL. The row is acked when the selection box LANDS on it by a key press (Up/Down,
//    a page move, the own symbol key's return to the top), or when the box has RESTED on it for 1.5 s. Opening
//    the page therefore does not ack the top row the instant it appears; leaving the box alone on it does.
//
// Store indices, never positions: the node menu and the search filter work on STORE rows (EST-449/455), and a
// frozen order of positions would name a different node after one removal.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"

namespace om {
namespace ui {

// How long the box must rest on a row before it counts as looked at. Long enough that scrolling past a row
// with the keyboard does not ack it, short enough that stopping to read does.
constexpr uint32_t kNodesFreshDwellMs = 1500;

struct NodesFrozen {
    int order[om::mc::kMaxContacts] = {};
    int count = 0;
};

// Take the snapshot when the page OPENS, from the live contactsOrder() the page would have drawn.
inline void nodesFrozenBegin(NodesFrozen& f, const int* live, int live_count) {
    f.count = 0;
    if (live == nullptr) return;
    for (int i = 0; i < live_count && f.count < (int)om::mc::kMaxContacts; ++i) f.order[f.count++] = live[i];
}

// Keep the snapshot true to the table while the page is open: append a store row that is not in it yet (a
// contact that arrived), drop rows that left the table (removed, or cleared). NEVER re-order. Returns true when
// the picture changed and the page must repaint.
inline bool nodesFrozenSync(NodesFrozen& f, const om::mc::ContactStore& s) {
    bool changed = false;
    // S515/EST-463: compact IN PLACE. The old second array was 2,000 ints on the caller's stack.
    int  n = 0;
    for (int i = 0; i < f.count; ++i) {
        const int ci = f.order[i];
        if (ci < 0 || ci >= (int)s.count) {
            changed = true;   // the row is gone
            continue;
        }
        if (n != i) f.order[n] = ci;
        n++;
    }
    // New rows are appended in store order, which is the order they were created in (the newest last).
    for (int ci = 0; ci < (int)s.count && n < (int)om::mc::kMaxContacts; ++ci) {
        bool have = false;
        for (int i = 0; i < n; ++i)
            if (f.order[i] == ci) {
                have = true;
                break;
            }
        if (!have) {
            f.order[n++] = ci;
            changed      = true;
        }
    }
    f.count = n;
    return changed;
}

// S516 (EST-473): a row was DELETED and the rows after it shifted DOWN one. `order` holds STORE indices, so
// every entry above the deleted row silently names the NEXT node — the list would show one node twice, lose the
// last one, and Enter would open a menu on a node the user did not pick. "Drop rows that left the table" cannot
// see it: after a shift every index is still in range.
//
// Re-MAP instead of re-sorting, because the freeze is the whole point ("the row keeps its place until the user
// leaves Nodes"): the deleted row's entry goes, every entry above it moves down with its node, and the ORDER the
// user is looking at does not change. `removed` is the deleted row's STORE index, taken BEFORE the delete
// (`contactsFind` on the key, not the position). A store that was cleared needs nothing: every index is then out
// of range and nodesFrozenSync() drops the lot.
inline bool nodesFrozenAfterRemove(NodesFrozen& f, int removed) {
    if (removed < 0) return false;
    bool changed = false;
    int  n       = 0;
    for (int i = 0; i < f.count; ++i) {
        const int e = f.order[i];
        if (e == removed) {
            changed = true;   // this node is gone
            continue;
        }
        const int v = (e > removed) ? e - 1 : e;   // its row moved down one
        if (v != e) changed = true;
        f.order[n++] = v;
    }
    f.count = n;
    return changed;
}

// ------------------------------------------------------ the status bar's notification icons (S516/EST-482)
// The bits moved out of the hint bar and into the top status bar (main/chrome.h, static_asserted there):
// CHAT while a channel or a DM has unread, NODE while a contact is still new. The same two numbers the Chat
// list's badges and the Nodes dot use; main.cpp only supplies the counts.
constexpr uint8_t kStatusIconBitChat = 1u << 0;
constexpr uint8_t kStatusIconBitNode = 1u << 1;

// The mask. No other bit exists: an icon is either shown by its own rule or it is not shown at all.
inline uint8_t statusIconMask(uint32_t channel_unread, uint32_t dm_unread, size_t fresh_count) {
    uint8_t m = 0;
    if (channel_unread + dm_unread > 0) m |= kStatusIconBitChat;
    if (fresh_count > 0) m |= kStatusIconBitNode;
    return m;
}

struct NodesAck {
    int      row      = -1;   // the row the dwell is running on (-1: none)
    uint32_t since_ms = 0;
};

// The page opened with the box on `row`: start the dwell there and ack NOTHING yet.
inline void nodesAckEnter(NodesAck& a, int row, uint32_t now_ms) {
    a.row      = (row >= 0) ? row : -1;
    a.since_ms = now_ms;
}

// A KEY pressed the box onto `row`: that is a look, so it acks at once. The dwell restarts on the new row.
// Returns the row to ack (>= 0), or -1 when there is no row (the search bar, an empty list).
inline int nodesAckKeyMoved(NodesAck& a, int row, uint32_t now_ms) {
    a.row      = (row >= 0) ? row : -1;
    a.since_ms = now_ms;
    return row;
}

// The dwell expired: returns the row to ack, or -1. The caller clears the flag and the ack cannot repeat for
// the same rest (a.row is cleared here).
inline int nodesAckTick(NodesAck& a, uint32_t now_ms) {
    if (a.row < 0) return -1;
    if ((uint32_t)(now_ms - a.since_ms) < kNodesFreshDwellMs) return -1;
    const int row = a.row;
    a.row         = -1;   // one ack per rest; a new rest is a new nodesAckEnter/Moved
    return row;
}

}  // namespace ui
}  // namespace om
