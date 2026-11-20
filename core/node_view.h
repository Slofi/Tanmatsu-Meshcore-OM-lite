// SPDX-License-Identifier: MIT
// S515/EST-465: the Nodes page's STORE VIEW — All / Radio / SD only — and the two texts that report the stores
// (the page head's live counts and Settings › OM Lite › NODES' info row and setting value).
//
// The view is a FILTER over the page's own order, never a second list: the frozen order of S515/EST-461 is the
// same array in every view, so a row keeps its place when the filter changes and the search (EST-449) runs on
// top of the view exactly as it runs on the whole list. "Radio" means the row's `in_radio` mark. S515/EST-468:
// the SD file holds EVERY node, radio ones included (one table, one row per node), so the head's SD number is
// the whole table and the "SD only" view is the rows NOT in the radio.
//
// Plain C++17 and pure: no display, no store, no radio. test/host pins the counts, the order per view and the
// exact texts the glass shows.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "mc_store.h"
#include "node_table.h"   // kMaxRadio, HeardToRadio

namespace om {
namespace ui {

enum class NodeView : uint8_t { All = 0, Radio = 1, Archive = 2 };

constexpr NodeView kNodeViewDefault = NodeView::All;

inline const char* nodeViewText(NodeView v) {
    switch (v) {
        case NodeView::Radio: return "Radio";
        case NodeView::Archive: return "SD only";
        case NodeView::All:
        default: return "All";
    }
}

// Left/Right on the Nodes list: `delta` +1 for Right, -1 for Left, clamping at both ends (no wrap: the three
// chips are a row of choices, and wrapping from Archive to All on one press would hide the end of the row).
inline NodeView nodeViewCycle(NodeView v, int delta) {
    int i = (int)v + delta;
    if (i < 0) i = 0;
    if (i > 2) i = 2;
    return (NodeView)i;
}

// How many rows the view holds. `radio` is radioCount(store); "SD only" is every row NOT in the radio.
inline int nodeViewCount(const om::mc::ContactStore& s, NodeView v, size_t radio) {
    if (v == NodeView::Radio) return (int)radio;
    if (v == NodeView::Archive) return (int)(s.count > radio ? s.count - radio : 0);
    return (int)s.count;
}

// The SHOWN order: walk the page's own order (frozen while the page is open) and keep the rows that belong to
// `view`, writing their CONTACT indices into `out` (may be null to count only). Returns how many were kept.
inline int nodeViewFilter(const om::mc::ContactStore& s, const int* order, int order_count, NodeView v, int* out) {
    if (order == nullptr || order_count <= 0) return 0;
    int n = 0;
    for (int i = 0; i < order_count; ++i) {
        const int ci = order[i];
        if (ci < 0 || ci >= (int)s.count) continue;   // a stale row id matches nobody
        if (v == NodeView::Radio && !s.c[ci].in_radio) continue;
        if (v == NodeView::Archive && s.c[ci].in_radio) continue;
        if (out != nullptr) out[n] = ci;
        ++n;
    }
    return n;
}

// The page head's right-hand text: "Radio 10 / 300 · SD 412". `sd` is EVERY node saved on the SD card
// (the radio's rows included; one table, one row per node). No card = "SD -", never a number that implies
// the nodes survive a restart. S516/EST-472 item 6: `radio_cap` is the cap of the store in use
// (radioCapFor(int_mounted)) — 300 over /int, 32 on the NVS fallback — never a hard-coded 300.
inline void nodeStoreCountsText(char* out, size_t cap, size_t radio, size_t sd, bool sd_mounted,
                                size_t radio_cap = om::mc::kMaxRadio) {
    if (out == nullptr || cap == 0) return;
    if (sd_mounted)
        snprintf(out, cap, "Radio %u / %u \xc2\xb7 SD %u", (unsigned)radio, (unsigned)radio_cap, (unsigned)sd);
    else
        snprintf(out, cap, "Radio %u / %u \xc2\xb7 SD -", (unsigned)radio, (unsigned)radio_cap);
}

// Settings › OM Lite › NODES, the info row: the same counts plus the card. A missing card is not a failure
// (the radio list still works), but the SD copy does not survive a restart without it, so it says so.
inline void nodeStoreInfoText(char* out, size_t cap, size_t radio, size_t sd, bool sd_mounted,
                              size_t radio_cap = om::mc::kMaxRadio) {
    if (out == nullptr || cap == 0) return;
    if (sd_mounted)
        snprintf(out, cap, "Radio %u / %u \xc2\xb7 SD %u \xc2\xb7 %s", (unsigned)radio, (unsigned)radio_cap,
                 (unsigned)sd, "SD ok");
    else
        snprintf(out, cap, "Radio %u / %u \xc2\xb7 SD - \xc2\xb7 %s", (unsigned)radio, (unsigned)radio_cap,
                 "no SD card");
}

// Settings › OM Lite › NODES, "Add heard nodes to the radio": the row's value.
inline const char* nodeHeardToRadioValue(om::mc::HeardToRadio v) {
    switch (v) {
        case om::mc::HeardToRadio::Clients: return "Clients";
        case om::mc::HeardToRadio::All: return "All";
        case om::mc::HeardToRadio::Off:
        default: return "Off";
    }
}

// The next setting on Enter: Off -> Clients -> All -> Off. The stored byte is the enum's value.
inline om::mc::HeardToRadio nodeHeardToRadioCycle(om::mc::HeardToRadio v) {
    switch (v) {
        case om::mc::HeardToRadio::Off: return om::mc::HeardToRadio::Clients;
        case om::mc::HeardToRadio::Clients: return om::mc::HeardToRadio::All;
        case om::mc::HeardToRadio::All:
        default: return om::mc::HeardToRadio::Off;
    }
}

}  // namespace ui
}  // namespace om
