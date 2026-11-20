// SPDX-License-Identifier: MIT
// S515/EST-463 item 2: the node table's rules (see node_table.h for why they look like this).
#include "node_table.h"

#include <string.h>

namespace om {
namespace mc {

HeardToRadio heardToRadioFromByte(uint8_t v) {
    if (v == (uint8_t)HeardToRadio::Clients) return HeardToRadio::Clients;
    if (v == (uint8_t)HeardToRadio::All) return HeardToRadio::All;
    return HeardToRadio::Off;   // 0, a blank NVS entry and any unknown value all mean "nothing automatic"
}

const char* heardToRadioText(HeardToRadio v) {
    switch (v) {
        case HeardToRadio::Clients: return "clients";
        case HeardToRadio::All: return "all";
        case HeardToRadio::Off:
        default: return "off";
    }
}

bool heardToRadioAllows(HeardToRadio v, uint8_t type) {
    switch (v) {
        case HeardToRadio::Clients: return type == (uint8_t)NodeType::Chat;   // clients only, never a repeater
        case HeardToRadio::All: return true;
        case HeardToRadio::Off:
        default: return false;
    }
}

const char* radioResultText(RadioResult r) {
    switch (r) {
        case RadioResult::Ok: return "ok";
        case RadioResult::AlreadyIn: return "already in the radio";
        case RadioResult::RadioFull: return "radio full";
        case RadioResult::NotFound: return "not found";
    }
    return "?";
}

int advertInArchive(ContactStore& s, HeardToRadio setting, const uint8_t pub[32], const char* name, uint8_t type,
                    uint8_t path_len, uint32_t remote_ts, uint32_t heard_at, int rssi, int snr_x10,
                    bool has_position, int32_t lat_e6, int32_t lon_e6, size_t radio_cap, bool save_heard) {
    if (pub == nullptr) return -1;
    const bool is_new = contactsFind(s, pub) < 0;
    // The insert branch (fresh, in_radio = false, no inherited route) lives in contactsUpsert, and it is the ONLY
    // place a row is born. in_radio is then set here, on an insert only, and only when the setting says so AND
    // there is room in the radio — a full radio silently keeps the node in the archive rather than refusing the
    // advert (the refusal Filip wants is on a deliberate add, not on hearing a node).
    const int idx = contactsUpsert(s, pub, name, type, path_len, remote_ts, heard_at, rssi, snr_x10, has_position,
                                   lat_e6, lon_e6);
    if (idx < 0) return -1;   // table full: the row is dropped, exactly as before
    // S516/EST-476: an INSERT heard with Save Off is session-only and never touches the radio, even when
    // "Add heard nodes to the radio" is All. The user's own Add to radio / Favourite later clears the flag.
    if (is_new && !save_heard) {
        s.c[idx].session_only = true;
        return idx;
    }
    // S516 (EST-472 item 6): `radio_cap` is the store in use (/int 300, NVS 32), never a fixed 300.
    if (is_new && heardToRadioAllows(setting, type) && radioCount(s) < radio_cap) s.c[idx].in_radio = true;
    return idx;
}

size_t radioCount(const ContactStore& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.count && i < s.cap; i++)
        if (s.c[i].in_radio && !s.c[i].session_only) n++;
    return n;
}

size_t sessionOnlyCount(const ContactStore& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.count && i < s.cap; i++)
        if (s.c[i].session_only) n++;
    return n;
}

RadioResult addToRadio(ContactStore& s, const uint8_t pub[32], size_t radio_cap) {
    const int idx = contactsFind(s, pub);
    if (idx < 0) return RadioResult::NotFound;
    if (s.c[idx].in_radio && !s.c[idx].session_only) return RadioResult::AlreadyIn;
    // Filip's rule 2: a full radio REFUSES. Nothing is marked, nothing is evicted, no field of any row moves.
    // S516 (EST-472 item 6): full means full for the store in use, not for /int's cap on a device without /int.
    if (radioCount(s) >= radio_cap) return RadioResult::RadioFull;
    s.c[idx].in_radio = true;
    // S516/EST-476 rule 4: Add to radio is the user saying "save this one", so a session-only row loses the mark
    // here. A REFUSED add returns above and leaves the row session-only, which is the honest outcome.
    s.c[idx].session_only = false;
    return RadioResult::Ok;
}

RadioResult removeFromRadio(ContactStore& s, const uint8_t pub[32]) {
    const int idx = contactsFind(s, pub);
    if (idx < 0) return RadioResult::NotFound;
    if (!s.c[idx].in_radio) return RadioResult::AlreadyIn;   // nothing to do; the archive already holds it
    s.c[idx].in_radio = false;                               // stays in the table: it is still a usable node
    return RadioResult::Ok;
}

bool deleteEverywhere(ContactStore& s, const uint8_t pub[32]) {
    const int idx = contactsFind(s, pub);
    if (idx < 0) return false;
    // Removing from BOTH stores is one row removal: there is one table, and `in_radio` only says which file it
    // is written to. Rows after it shift down — callers must re-find by key (node_table.h).
    for (size_t i = (size_t)idx + 1; i < s.count; i++) s.c[i - 1] = s.c[i];
    s.count--;
    if (s.count < s.cap) s.c[s.count] = om::mc::Contact{};
    return true;
}

bool setFavourite(ContactStore& s, const uint8_t pub[32], bool on) {
    const int idx = contactsFind(s, pub);
    if (idx < 0) return false;
    s.c[idx].favourite = on;   // a favourite in the archive is a favourite; it does NOT move the node to the radio
    // S516/EST-476 rule 4: the Favourite key is a user act on the row, so it also means "save this one". The row
    // stays archive-only (favourite never implies in_radio) but it is no longer session-only; it is written by
    // the normal node encoders from now on.
    s.c[idx].session_only = false;
    return true;
}

}  // namespace mc
}  // namespace om
