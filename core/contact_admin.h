// SPDX-License-Identifier: MIT
// The NODES section's rules (S514/EST-447): removing ONE contact, clearing the table, and the new-node
// notice's stored default.
//
// The contact table itself is `mc_store.h` (a fixed 32-row array with a count). Removal COMPACTS it, the
// same way the channel table compacts and the way the L2's `contacts[]` maintenance does: the rows after
// the removed one move down one place, the vacated last row is zeroed, and the count drops. Order matters
// because `contactsOrder()` is stable (favourites first, then most recently heard) and a rebuilt list must
// not reshuffle equal rows.
//
// Nothing here touches NVS: the device's main task calls these under the store lock and the SAME dirty flag
// `contactsPersistTick()` already owns then rewrites the `mcct` blob through `contactsEncode()`. That is why
// the host test can remove a row, encode the table and decode it back: the bytes the device stores are the
// bytes the test checks.
//
// Plain C++17: no Arduino, no LovyanGFX, no IDF, no NVS, no drawing.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"

namespace om {
namespace mc {

// Set one row's favourite flag (S514/EST-455). False (and the table untouched) for an out-of-range index, the
// same stale-row rule as removal: a list built before an advert changed the table must not mark somebody else.
// The flag is part of the contact record (contact_blob), so on the device the caller also raises the structural
// dirty flag and the SAME `mcct` write persists it.
inline bool contactSetFavourite(ContactStore& s, size_t index, bool on) {
    if (index >= s.count) return false;
    s.c[index].favourite = on;
    // S516/EST-476 rule 4: a Favourite press is a user act on the row, so a session-only row becomes saved. The
    // row stays archive-only (a favourite never implies in_radio); it is simply written normally from now on.
    s.c[index].session_only = false;
    return true;
}

// Remove the row at `index`, shifting the rows after it down and zeroing the vacated slot. False (and the
// table untouched) when the index is out of range: a stale row id from a list built before an advert changed
// the table must never remove somebody else.
inline bool contactRemoveAt(ContactStore& s, size_t index) {
    if (index >= s.count) return false;
    for (size_t i = index; i + 1 < s.count; ++i) s.c[i] = s.c[i + 1];
    s.c[s.count - 1] = Contact{};
    --s.count;
    return true;
}

// Clear every row. The DM threads and their logs are NOT touched: a removed contact's history stays on the
// card, exactly as the confirm text says; only the RAM table (and, on the device, the `mcct` blob) loses it.
inline void contactsClearAll(ContactStore& s) {
    for (size_t i = 0; i < s.count; ++i) s.c[i] = Contact{};
    s.count = 0;
}

// The list sub-page's type word. The same words the advert log uses (mc_link.cpp's advertTypeName), so the
// Nodes page, the log and this list cannot describe one node three ways.
constexpr const char* contactTypeLabel(uint8_t type) {
    switch (type) {
        case 1: return "client";
        case 2: return "repeater";
        case 3: return "room";
        case 4: return "sensor";
        default: return "node";
    }
}

// The new-node notice (NVS, default ON). A corrupt or absent value means ON: a bad byte in flash must not
// silently make a fresh node appear without a word. Stored 0 is the only Off.
constexpr bool newNodeNoticeFromStored(bool present, uint32_t value) {
    return !present || value != 0;
}

}  // namespace mc
}  // namespace om
