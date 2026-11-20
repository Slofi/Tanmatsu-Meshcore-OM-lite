// SPDX-License-Identifier: MIT
// S515/EST-463 item 2: the node table's RULES, pure and host-tested.
//
// Filip's two decisions (2026-11-19, S515), and they are the reason this file exists at all:
//   1. NOTHING goes into the radio automatically. A verified advert goes to the ARCHIVE only; the user moves a
//      node to the radio by hand (or sets "Add heard nodes to the radio" to Clients/All and accepts that).
//   2. A FULL RADIO REFUSES. No node is ever pushed out to make room, no matter how old it is.
//
// So there is exactly one table (mc_store.h, PSRAM, kMaxNodes = 2000) and one flag per row, `in_radio`. This file
// owns the five operations that may change it, and every one of them is a plain function over that table:
//
//   advertInArchive()  an advert arrived  -> insert or refresh; insert is `fresh`; in_radio per the setting
//   addToRadio()       the user chose it  -> Ok / AlreadyIn / RadioFull / NotFound, NEVER evicts
//   removeFromRadio()  back to the archive -> stays in the table, still fully usable
//   deleteEverywhere() gone from both stores
//
// A favourite is independent of both: set on any row, in either store, and it does NOT imply in_radio.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"

namespace om {
namespace mc {

// Settings › OM Lite › NODES: "Add heard nodes to the radio" — Off / Clients / All, default Off. Stored in NVS
// beside the other node settings; the Settings ROW itself is the next ticket (EST-464), this is only the value
// and the rule.
enum class HeardToRadio : uint8_t { Off = 0, Clients = 1, All = 2 };

constexpr HeardToRadio kHeardToRadioDefault = HeardToRadio::Off;

// The NVS byte for the setting: anything that is not 1 or 2 reads as Off (an unknown value must never start
// filling the radio). Returns the value to STORE, so a corrupt byte is normalised on the next write.
HeardToRadio heardToRadioFromByte(uint8_t v);
const char*  heardToRadioText(HeardToRadio v);   // "off" / "clients" / "all", for the log

// Does the setting allow a node of this advert type into the radio on its own? Clients = NodeType::Chat (1) only,
// exactly as Filip's "Clients" says: repeaters, rooms and sensors are not clients.
bool heardToRadioAllows(HeardToRadio v, uint8_t type);

// ---- the four operations -----------------------------------------------------------------------------------
enum class RadioResult : uint8_t {
    Ok = 0,
    AlreadyIn,    // the node is in the radio; nothing changed
    RadioFull,    // kMaxRadio rows are already marked, and NOTHING was changed (Filip: a full radio refuses)
    NotFound,     // no row with that key
};

const char* radioResultText(RadioResult r);

// S516/EST-472 item 6: the cap of the store the radio list actually persists to. /int's radio.bin holds
// kMaxRadio rows; without /int the old NVS blob holds kMaxRadioNvs. The caller (main/mc_link.cpp) knows which
// store is mounted, so every rule below takes the cap as an argument instead of assuming 300.
constexpr size_t radioCapFor(bool int_mounted) { return int_mounted ? kMaxRadio : kMaxRadioNvs; }

// An advert from the air, already signature-verified by the caller (mc_link). Insert or refresh by key:
//   * an INSERT sets fresh = true (the S515/EST-461 new-node dot) and in_radio = heardToRadioAllows(setting, type);
//     with the default Off that is false, which is Filip's rule 1.
//   * a REFRESH never changes in_radio: a node the user moved to the archive stays there, and one they moved to
//     the radio stays in the radio. It also never clears `favourite`.
// Returns the store index, or -1 when the table is full and the key is new (the row is dropped, as before).
int advertInArchive(ContactStore& s, HeardToRadio setting, const uint8_t pub[32], const char* name, uint8_t type,
                    uint8_t path_len, uint32_t remote_ts, uint32_t heard_at, int rssi, int snr_x10,
                    bool has_position = false, int32_t lat_e6 = 0, int32_t lon_e6 = 0,
                    size_t radio_cap = kMaxRadio);

// How many rows are marked in_radio. O(n), and n is 2,000 — one pass, no counter to keep in sync (a counter
// would be one more thing a restore could get wrong).
size_t radioCount(const ContactStore& s);

// Mark a row in_radio. NEVER evicts: at `radio_cap` the answer is RadioFull and the table is untouched, including
// `fresh` and the row's other fields. A node already in the radio answers AlreadyIn (idempotent, no write).
// `radio_cap` is radioCapFor(int_mounted): kMaxRadio over /int, kMaxRadioNvs without it.
RadioResult addToRadio(ContactStore& s, const uint8_t pub[32], size_t radio_cap = kMaxRadio);

// Clear in_radio. The row STAYS in the table and stays fully usable (DMs decrypt, ping/trace work); only its
// persistence store changes. NotFound when the key is not in the table.
RadioResult removeFromRadio(ContactStore& s, const uint8_t pub[32]);

// Remove the row from BOTH stores (the node menu's "Delete everywhere", item 3b's UI). Rows after it shift down
// by one, so a caller holding an index must re-find by key afterwards — see main.cpp's node-menu handler, which
// copies the key before the call and re-finds with contactsFind() after it.
bool deleteEverywhere(ContactStore& s, const uint8_t pub[32]);

// Favourites work on either store and never imply in_radio (tested).
bool setFavourite(ContactStore& s, const uint8_t pub[32], bool on);

}  // namespace mc
}  // namespace om
