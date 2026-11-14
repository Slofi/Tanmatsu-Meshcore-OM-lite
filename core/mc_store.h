// SPDX-License-Identifier: MIT
// What the app REMEMBERS: the nodes it has heard, and the messages on its channels.
//
// Step 4 needs a place for the screens to read from, and the screens are the wrong place to keep it: a ring
// buffer with a drop rule, a contact list keyed by public key, and the unread arithmetic are all decidable
// without a display, so they live here and the host tests pin them. `main/mc_link.cpp` writes, the pages read.
//
// Not here (deliberately): SD history (step 5 — the L2's store design), reply paths, favourites in NVS. The
// structs carry the fields those need so the shape does not change under the screens.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace mc {

constexpr size_t kMaxContacts  = 32;
constexpr size_t kMaxMessages  = 64;    // across all channels; the oldest goes when it is full
constexpr size_t kNameBytes    = 24;    // "TNMT (SI)" plus a NUL, the same width mc_link uses
constexpr size_t kTextBytes    = 160;   // one MeshCore group text, in bytes
constexpr size_t kMaxPathHops  = 8;
constexpr size_t kMaxChannels  = 8;     // matches meshcore::kMaxChannels

// MeshCore's advert type, low bits of the flags byte (AdvertDataHelpers: CHAT 1, REPEATER 2, ROOM 3, SENSOR 4).
enum class NodeType : uint8_t { Chat = 1, Repeater = 2, Room = 3, Sensor = 4 };

struct Contact {
    uint8_t  pub[32] = {0};
    char     name[kNameBytes] = {0};
    uint8_t  type = 0;          // NodeType as it came off the air, 0 = unknown
    uint8_t  path_len = 0;      // hops the advert travelled, in BYTES of path
    uint32_t remote_ts = 0;     // the advert's own timestamp
    uint32_t heard_at = 0;      // our clock when we heard it (for "14h ago")
    int16_t  rssi = 0;
    int16_t  snr_x10 = 0;       // dB x 10: the UI prints "3.2 dB"
    bool     favourite = false;
};

struct ContactStore {
    Contact c[kMaxContacts];
    size_t  count = 0;
};

// Insert or refresh by public key. A repeat advert UPDATES the row (newer timestamp, new name/RSSI) rather
// than adding a second one — that is the whole point of identifying by key. Returns the index, or -1 when the
// store is full and the key is new.
int contactsUpsert(ContactStore& s, const uint8_t pub[32], const char* name, uint8_t type, uint8_t path_len,
                   uint32_t remote_ts, uint32_t heard_at, int rssi, int snr_x10);
int contactsFind(const ContactStore& s, const uint8_t pub[32]);

// The order the Nodes list shows: favourites first, then most recently heard. Writes indices, returns how many.
// A stable order matters more than a clever sort: two rows must not swap places while the user is looking.
size_t contactsOrder(const ContactStore& s, int* out, size_t max_out);

// "2 in the last hour": how many were heard within `window_s` of `now`.
size_t contactsHeardWithin(const ContactStore& s, uint32_t now, uint32_t window_s);

struct Message {
    uint32_t seq = 0;           // assigned by messagesPush: monotonic, the thing unread is counted against
    uint8_t  channel = 0;
    bool     outgoing = false;
    char     sender[kNameBytes] = {0};
    char     text[kTextBytes] = {0};
    uint32_t ts = 0;            // the MeshCore timestamp in the frame
    uint32_t local_ts = 0;      // our clock when it arrived (ordering, and ages when ts is unusable)
    int16_t  snr_x10 = 0;
    bool     direct = false;    // path_len 0 in the received frame
    // The id this message has IN ITS LOG FILE (S503), 0 when it was never written to the card. It is what the
    // Meta record's seen point refers to, so unread can be restored after a restart — `seq` cannot do that
    // because messagesPush restarts it at 1 on every boot.
    uint32_t log_id = 0;
};

struct MessageRing {
    Message  m[kMaxMessages];
    size_t   head = 0;      // index of the OLDEST
    size_t   count = 0;
    uint32_t next_seq = 0;  // the id the next pushed message gets
};

uint32_t       messagesPush(MessageRing& r, const Message& msg);   // returns the id the message got
const Message* messagesAt(const MessageRing& r, size_t i);   // 0 = oldest retained

// Indices of one channel's messages, oldest first. Returns how many were written (may be fewer than the total).
size_t messagesForChannel(const MessageRing& r, uint8_t ch, int* out, size_t max_out);

// How many messages a channel holds — the cheapest thing to compare when deciding "unread".
uint32_t messagesCountForChannel(const MessageRing& r, uint8_t ch);

// Unread is counted by SEQUENCE, not by how many messages the channel holds. The ring drops its oldest, so a
// count-based rule is wrong exactly when the ring is full (a new message does not change the count) — the case
// that matters most. `seen[ch]` is the highest sequence number already read on that channel; anything newer and
// still in the ring is unread, and an evicted message can never come back to haunt the count.
struct ReadState {
    uint32_t seen[kMaxChannels] = {0};   // 0 = nothing read yet (ids start at 1, see messagesPush)
};
uint32_t unreadForChannel(const MessageRing& r, const ReadState& rs, uint8_t ch);
void     markChannelRead(const MessageRing& r, ReadState& rs, uint8_t ch);

// One line for the channel list: the newest message's sender and text, plus the age to print on the right.
struct ChannelSummary {
    bool        has = false;
    const char* sender = "";
    const char* text = "";
    uint32_t    ts = 0;
};
ChannelSummary channelSummary(const MessageRing& r, uint8_t ch);

// "now" / "12m" / "4h" / "3d" — the same shape the L2 prints on its lists. `now` and `ts` are epoch seconds;
// a ts in the future (a peer's clock ahead of ours) reads "now" rather than a negative age.
void formatAge(uint32_t now, uint32_t ts, char* out, size_t n);

}  // namespace mc
}  // namespace om
