// SPDX-License-Identifier: MIT
// The Nodes page's NODE MENU and its favourite rule (S514/EST-455).
//
// Ported from the L2's `firmware/src/ui/node_menu.h` / `.cpp`: one list of actions per node, built from what the
// node actually is, in the L2's order. The rule stays: ONLY actions that work on the Tanmatsu today are offered.
// S514/EST-456 brings Ping and Trace in (core/node_ops.*); Map and Reset path still exist in the L2's list and
// are left out here until their code exists — a row that does nothing would be a lie on a screen this honest
// about its state.
//
// The rows that are offered today:
//   Direct message  a client with a full key (a repeater or a room cannot take a text DM)
//   Info            always; the Nodes page's own detail pane is the info view
//   Ping            a contact with a full key (a status request works over a path or as a flood)
//   Trace           a contact with a full key AND a stored route: the packet is aimed down out_path
//   Favourite       a contact with a full key; value "on"/"off", toggled by Enter
//   Remove contact  a contact with a full key; the EST-447 confirm and removal
//
// S514/EST-456: Ping/Trace are the L2's conditions exactly (firmware/src/ui/node_menu.cpp:32-38): Ping on
// hasKey, Trace on hasKey && pathKnown. With no route the Trace row is ABSENT, never dimmed.
//
// Everything here is plain C++17 and pure: no display, no NVS, no radio. `main/main.cpp` performs the side
// effects (open the DM thread, remove, persist); the host tests drive the same decisions through this header.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "node_table.h"   // S515/EST-465: kMaxRadio, for the "Radio full (300 / 300)" sentence

namespace om {
namespace ui {

// MeshCore's advert types (lib/meshcore/helpers/AdvertDataHelpers.h); the same numbers mc::NodeType uses.
constexpr uint8_t kNodeMenuAdvChat     = 1;
constexpr uint8_t kNodeMenuAdvRepeater = 2;
constexpr uint8_t kNodeMenuAdvRoom     = 3;
constexpr uint8_t kNodeMenuAdvSensor   = 4;

enum class NodeAction : uint8_t {
    None = 0,
    DirectMessage,
    Info,
    Ping,
    Trace,
    AddToRadio,      // S515/EST-465: the row is not in the radio
    MoveToArchive,   // S515/EST-465: the row is in the radio (clears the mark; the node stays usable)
    Favourite,
    DeleteEverywhere,   // S515/EST-465: one table, so this is the old Remove contact, named honestly
    RemoveContact,      // kept for the Settings › NODES list's own confirm path (EST-447)
};

// Seven is the real maximum (a client with a route: DM, Info, Ping, Trace, the store row, Favourite,
// Delete everywhere). Kept as a bound so a future action cannot silently overrun a caller's array.
constexpr int kMaxNodeMenuRows = 7;

struct NodeMenuRow {
    NodeAction  action      = NodeAction::None;
    const char* label       = "";
    const char* value       = "";      // the right-hand text (favourite state), "" = none
    bool        valueAccent = false;   // the value is a state that is ON (gold), never inferred from "on"
};

// One node's facts, filled by the shell from the contact table.
struct NodeMenuContext {
    uint8_t advType   = 0;
    bool    hasKey    = false;   // a real contact, so its public key is known
    bool    favourite = false;
    // S515/EST-465: which store the row is in. The menu says "Add to radio" or "Move to archive", never both.
    bool    in_radio  = false;
    // S514/EST-456: ContactInfo::out_path_len != OUT_PATH_UNKNOWN (core/mc_store.h contactHasOutPath).
    bool    pathKnown = false;
    // A ping/trace to THIS node is in flight: the row says "waiting..." and Esc stops waiting.
    bool    pingPending  = false;
    bool    tracePending = false;
};

// The rows in the L2's order, restricted to what works here. Returns 0..kMaxNodeMenuRows.
inline int nodeMenuRows(const NodeMenuContext& c, NodeMenuRow* out, int max) {
    if (out == nullptr || max <= 0) return 0;
    int n = 0;
    auto add = [&](NodeAction action, const char* label, const char* value = "", bool accent = false) {
        if (n >= max) return;
        out[n].action      = action;
        out[n].label       = label != nullptr ? label : "";
        out[n].value       = value != nullptr ? value : "";
        out[n].valueAccent = accent;
        ++n;
    };

    // A client can be written to; a repeater, room or sensor cannot take a text DM, so the row is left out
    // rather than offered and refused.
    if (c.hasKey && c.advType == kNodeMenuAdvChat) add(NodeAction::DirectMessage, "Direct message");
    add(NodeAction::Info, "Info");
    if (c.hasKey) add(NodeAction::Ping, "Ping", c.pingPending ? "waiting..." : "");
    if (c.hasKey && c.pathKnown) add(NodeAction::Trace, "Trace", c.tracePending ? "waiting..." : "");
    if (c.hasKey) {
        // S515/EST-465: one store row, worded by where the node is now. The action is never both.
        if (c.in_radio) add(NodeAction::MoveToArchive, "Move to archive");
        else add(NodeAction::AddToRadio, "Add to radio");
        add(NodeAction::Favourite, "Favourite", c.favourite ? "on" : "off", c.favourite);
        // One table, two files: "Delete everywhere" is what removing the row really does now (EST-463). The
        // FAVOURITE flag and the DM history are not touched by it.
        add(NodeAction::DeleteEverywhere, "Delete everywhere");
    }
    return n;
}

// Up/Down inside the sheet. Clamped at both ends (no wrap): the sheet is short and a wrap would hide how many
// rows there are. `focus` outside the list is clamped into it first.
inline int nodeMenuFocusMove(int focus, int delta, int count) {
    if (count <= 0) return 0;
    if (focus < 0) focus = 0;
    if (focus >= count) focus = count - 1;
    focus += delta;
    if (focus < 0) focus = 0;
    if (focus >= count) focus = count - 1;
    return focus;
}

// The action Enter would run, or NodeAction::None for an index outside the list.
inline NodeAction nodeMenuActionAt(const NodeMenuRow* rows, int count, int focus) {
    if (rows == nullptr || focus < 0 || focus >= count) return NodeAction::None;
    return rows[focus].action;
}

// "&lt;name&gt;: favourite" / "&lt;name&gt;: not favourite", the ticket's exact words. A blank name is named
// "this node" rather than producing ": favourite".
inline void nodeFavouriteNotice(char* out, size_t cap, const char* name, bool on) {
    if (out == nullptr || cap == 0) return;
    const char* who = (name != nullptr && name[0] != '\0') ? name : "this node";
    snprintf(out, cap, "%s: %s", who, on ? "favourite" : "not favourite");
}

// After a toggle the list is rebuilt; the focus must FOLLOW the node, not stay on a row number. Returns the
// node's position in the SHOWN (filtered) order, or -1 if it is not there.
inline int nodeMenuFocusFollow(const int* shown, int shown_count, int store_index) {
    if (shown == nullptr) return -1;
    for (int i = 0; i < shown_count; ++i) {
        if (shown[i] == store_index) return i;
    }
    return -1;
}

// ---------------------------------------------------------------- S515/EST-465: the store actions' words
// "Radio full (300 / 300): move a node to the archive first" — Filip's rule 2, in the sheet's exact words.
// The numbers come from kMaxRadio, so the sentence cannot drift from the cap.
inline void nodeRadioFullNotice(char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    snprintf(out, cap, "Radio full (%u / %u): move a node to the archive first",
             (unsigned)om::mc::kMaxRadio, (unsigned)om::mc::kMaxRadio);
}

inline const char* nodeAddedToRadioNotice() { return "Added to the radio"; }
inline const char* nodeMovedToArchiveNotice() { return "Moved to the archive"; }

// Delete everywhere's question. The row is removed from BOTH files; its DM thread and dm-*.log are never
// touched (main/mc_link.cpp contactsRemove/contactsClear and core/node_table.cpp deleteEverywhere only remove
// the contact row), which is what the sentence promises.
inline void nodeDeleteConfirmText(char* out, size_t cap, const char* name) {
    if (out == nullptr || cap == 0) return;
    const char* who = (name != nullptr && name[0] != '\0') ? name : "this node";
    snprintf(out, cap, "Delete %s from the radio and the archive? Its DM history stays on the card.", who);
}

// After a delete the rows shift down. The caller re-finds the node BY KEY (contactsFind on the new table) and
// lands the focus on it. When the key is gone (it was the deleted row, or it was never shown), the focus is
// clamped to the list it now has, so an empty view never leaves an index past the end.
inline int nodeMenuFocusAfterRemove(const int* shown, int shown_count, int new_store_index, int old_focus) {
    if (shown_count <= 0) return 0;
    if (new_store_index >= 0) {
        const int pos = nodeMenuFocusFollow(shown, shown_count, new_store_index);
        if (pos >= 0) return pos;
    }
    if (old_focus < 0) return 0;
    if (old_focus >= shown_count) return shown_count - 1;
    return old_focus;
}

}  // namespace ui
}  // namespace om
