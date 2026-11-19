// SPDX-License-Identifier: MIT
// The Nodes page's NODE MENU and its favourite rule (S514/EST-455).
//
// Ported from the L2's `firmware/src/ui/node_menu.h` / `.cpp`: one list of actions per node, built from what the
// node actually is, in the L2's order. The difference is deliberate and is the ticket's first rule: ONLY actions
// that work on the Tanmatsu today are offered. Ping, Trace, Map and Reset path exist in the L2's list and are
// left out here until their code exists — a row that does nothing would be a lie on a screen this honest about
// its state.
//
// The rows that are offered today:
//   Direct message  a client with a full key (a repeater or a room cannot take a text DM)
//   Info            always; the Nodes page's own detail pane is the info view
//   Favourite       a contact with a full key; value "on"/"off", toggled by Enter
//   Remove contact  a contact with a full key; the EST-447 confirm and removal
//
// Everything here is plain C++17 and pure: no display, no NVS, no radio. `main/main.cpp` performs the side
// effects (open the DM thread, remove, persist); the host tests drive the same decisions through this header.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
    Favourite,
    RemoveContact,
};

// Four is the real maximum today (a client: DM, Info, Favourite, Remove). Kept as a bound so a future action
// cannot silently overrun a caller's array.
constexpr int kMaxNodeMenuRows = 4;

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
    if (c.hasKey) {
        add(NodeAction::Favourite, "Favourite", c.favourite ? "on" : "off", c.favourite);
        add(NodeAction::RemoveContact, "Remove contact");
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

}  // namespace ui
}  // namespace om
