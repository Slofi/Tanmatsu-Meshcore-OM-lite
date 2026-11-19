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

#include "mc_wire.h"   // meshcore::kMaxPath: MeshCore's own limit on a stored route
// The L2's delivery states (firmware/src/ui/dm_delivery.h): Sending / Delivered / NotConfirmed / Failed, with
// dmStateToken() giving the words. Reused rather than re-invented — the same numbers go into the card log's
// `delivery` byte (firmware/src/history/history_log.h, Msg::delivery).
#include "../../firmware/src/ui/dm_delivery.h"

namespace om {
namespace mc {

// S515/EST-463: ONE node table, in PSRAM, holding everything the app has heard (the SD archive) and the nodes
// chosen for the radio alike. The 32-row limit was never the chip's: it was one NVS blob (148-158 B a row) in a
// 16 KB partition shared with channels, scopes, sounds and the name. The table now lives in PSRAM and only the
// `in_radio` rows persist to internal flash (/int/om-lite/radio.bin), so the old cap moves to kMaxRadio.
constexpr size_t kMaxNodes     = 2000;  // every node, archive + radio, one table
constexpr size_t kMaxRadio     = 300;   // how many of them may be marked in_radio at once
// Kept as an alias so every existing user of kMaxContacts keeps working at the new capacity; the name is what
// the pages and the codecs were written against, and renaming 30 call sites would only add diff noise.
constexpr size_t kMaxContacts  = kMaxNodes;
constexpr size_t kMaxMessages  = 64;    // across all channels; the oldest goes when it is full
constexpr size_t kNameBytes    = 24;    // "TNMT (SI)" plus a NUL, the same width mc_link uses
constexpr size_t kTextBytes    = 160;   // one MeshCore group text, in bytes
constexpr size_t kMaxPathHops  = 8;
constexpr uint8_t kOutPathUnknown = 0xFF;   // OUT_PATH_UNKNOWN (ContactInfo.h; MeshCore.h:19) — never a route
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
    // S515/EST-461: NEW = the row was created by the first verified advert of a key that was not in the table
    // (contactsUpsert's insert branch only — never a refresh, never a row restored from NVS/SD). It sorts
    // directly under the favourites, carries a red dot in Nodes, and clears when the selection box lands on it
    // (a key press, or a 1.5 s dwell). Persisted in blob version 3, so a restart does not lose it.
    bool     fresh = false;
    // S515/EST-461, for EST-463's map: the position the advert carried. Only `has_position` makes lat/lon
    // readable (an advert without ADV_LATLON_MASK leaves them zero). Not drawn anywhere yet.
    bool     has_position = false;
    int32_t  lat_e6 = 0;
    int32_t  lon_e6 = 0;
    // S513/EST-434: the highest DM sequence the user has seen in THIS contact's thread. Unread is counted from
    // it, exactly like a channel's ReadState — a restored seen point (SD, item 4) will seed it too.
    uint32_t dm_seen = 0;
    uint32_t dm_seen_log = 0;   // the log id the on-card Meta record already names (0 = none written this boot)
    uint32_t dm_log_next = 1;   // the next id in this contact's dm-<8 hex>.log (1 = a fresh file)
    // S513/EST-439: the route we learned from a PATH packet, exactly as MeshCore keeps ContactInfo::out_path /
    // out_path_len. OUT_PATH_UNKNOWN (0xFF) means "no route, flood"; a COUNT of 0 is a real DIRECT route to a
    // neighbour heard directly (BaseChatMesh.cpp:481-486 only tests against OUT_PATH_UNKNOWN).
    uint8_t  out_path_len = kOutPathUnknown;
    uint8_t  out_path[meshcore::kMaxPath] = {0};
    // S515/EST-463: this node is one of the chosen ones, kept in internal flash (/int/om-lite/radio.bin) so it
    // works without the SD card. EVERY other row is archive-only: it is fully usable (DMs decrypt, ping/trace
    // work) but it is not in the radio's own store. Nothing ever sets this from the air — only the user's
    // addToRadio() and the "Add heard nodes to the radio" setting (node_table.h) do, and the setting defaults Off.
    bool     in_radio = false;
};

// MeshCore's ONE distinction: only OUT_PATH_UNKNOWN means "no route"; a zero-hop path is a direct neighbour.
inline bool contactHasOutPath(const Contact& c) { return c.out_path_len != kOutPathUnknown; }

// S515/EST-463: the table is ONE allocation, not an inline array — 2,000 rows are ~312 KB, which is PSRAM money,
// not internal RAM. `c` stays a plain pointer so every existing `s.c[i]` keeps compiling; the store allocates in
// its constructor through a hook the device installer points at PSRAM (main/mc_link.cpp) and the host tests
// leave at malloc. There is deliberately NO destructor: a ContactStore may be copied by value (the tests do),
// and two copies sharing one array must not free it twice. The device has exactly one long-lived store.
using ContactMemAlloc = void* (*)(size_t bytes);
using ContactMemFree  = void (*)(void* p);
void setContactAlloc(ContactMemAlloc alloc_fn, ContactMemFree free_fn);

struct ContactStore {
    Contact* c    = nullptr;   // kMaxNodes rows, PSRAM on the device (see setContactAlloc)
    size_t   count = 0;
    size_t   cap   = 0;        // rows `c` can hold; 0 means the allocation failed (every caller must cope)

    ContactStore();
    // A copy owns its own allocation and copies only the live rows: the tests hand a small store back by value,
    // and a shared pointer with a destructor would be a double free. 2,000 empty rows cost nothing to copy.
    ContactStore(const ContactStore& o);
    ContactStore& operator=(const ContactStore& o);
    ~ContactStore();
};

// Bytes one full table costs, for the boot log ("the PSRAM bytes the table took").
size_t contactsTableBytes();

// Insert or refresh by public key. A repeat advert UPDATES the row (newer timestamp, new name/RSSI) rather
// than adding a second one — that is the whole point of identifying by key. Returns the index, or -1 when the
// store is full and the key is new. `s.cap == 0` (the PSRAM allocation failed) behaves like a full store.
int contactsUpsert(ContactStore& s, const uint8_t pub[32], const char* name, uint8_t type, uint8_t path_len,
                   uint32_t remote_ts, uint32_t heard_at, int rssi, int snr_x10, bool has_position = false,
                   int32_t lat_e6 = 0, int32_t lon_e6 = 0);
int contactsFind(const ContactStore& s, const uint8_t pub[32]);

// The order the Nodes list shows (S515/EST-461): favourites first, then rows that are still FRESH (newest
// heard first), then everything else by last heard. Writes indices, returns how many.
//
// S515/EST-463: ONE stable bottom-up merge sort instead of the three O(n^2) passes. The comparator is the same
// three groups plus heard_at descending, with the store index as the final tie-break, so a fill of 2,000 rows is
// O(n log n) and the order is identical to v58's for every input (pinned by test_store's ordering checks and by
// test_node_table's 2,000-row timing).
size_t contactsOrder(const ContactStore& s, int* out, size_t max_out);

// How many rows are still fresh (the Nodes sigil's dot). Same flag contactsOrder sorts by.
size_t contactsFreshCount(const ContactStore& s);

// "2 in the last hour": how many were heard within `window_s` of `now`.
size_t contactsHeardWithin(const ContactStore& s, uint32_t now, uint32_t window_s);

// ---- S513/EST-441: a contact is born ONLY from a verified advert --------------------------------------------------
// A DM log's name is `dm-<pubkey[0..3] 8 hex>.log`, so a thread restored from the card knows four bytes of its
// peer and nothing else. v41 turned those four bytes into a contact, and that invented row was then used for the
// shared secret and as the only RX candidate — a DM that could not be opened and a MAC that could not match.
// These four rules are the fix, and they are pure so the host tests can pin them.
//
// True when the key is only a prefix: a null key, or bytes 4..31 all zero. No real Ed25519 key looks like that.
bool contactKeyIncomplete(const uint8_t pub[32]);

// Drop every incomplete row (used straight after the NVS blob is decoded, so a bogus record already on a device
// is cleaned on the next boot). Returns how many were removed; the rest keep their order.
size_t contactsDropIncomplete(ContactStore& s);

// The ONE full-key contact whose key starts with these four bytes, -1 when there is none — and also -1 when two
// of them share the prefix. Two keys can start with the same byte (ours and ERA-3's both started 0x42), so a
// prefix that is not unique names nobody and the thread stays unbound until its advert arrives.
int contactsFindUniquePrefix(const ContactStore& s, const uint8_t prefix[4]);

// May a DM be composed to this peer? Only when it is a contact with a FULL key: the shared secret needs all 32
// bytes, and a prefix-only key would produce a secret the peer cannot derive. Fills `name` (may be empty).
bool dmPeerKey(const ContactStore& s, const uint8_t pub[32], char* name, size_t name_cap);

// S515/EST-462: how a frame travelled — MeshCore's own route field (Packet.h:14-17). TRANSPORT_FLOOD is a
// scoped flood (channel_scope.h builds one for a region). Unknown is honest: a restored record never carries it.
enum class RouteType : uint8_t { Unknown = 0, Flood = 1, ScopedFlood = 2, Direct = 3 };

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
    // S515/EST-462: the REAL path, not the fake direct/flood pair the history record used to carry. `path` holds
    // hash_size * hops bytes (a copy, because the frame buffer is reused); its LAST hash is the hop nearest us.
    // hops_known is false when there is no honest count: our own flood, or a pre-v58 record's fake value.
    bool     hops_known = false;
    uint8_t  hops       = 0;     // repeaters crossed; 0 = direct
    uint8_t  hash_size  = 0;     // path hash size in bytes (1..3); 0 = unknown
    uint8_t  path_bytes = 0;     // hash_size * hops actually kept in `path`
    uint8_t  path[meshcore::kMaxPath] = {0};
    // Bytes and route are live-radio facts; the L2 record keeps neither, so they stay 0/Unknown after a restart.
    uint16_t  frame_len = 0;     // the number the RX/TX log prints
    RouteType route     = RouteType::Unknown;
    // Copies heard: incoming = duplicate frames of this message; outgoing = our own frame heard back (the 0x42
    // self-echo). Counted by the seen ring, never invented for a restored line.
    uint16_t copies = 0;
    // The payload fingerprint the seen ring matches, so a dropped copy can find the line it belongs to.
    uint32_t frame_fp = 0;
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

// --------------------------------------------------------------------------------- direct-message threads (S513/EST-434)
// A DM is not a channel message with a different label: it belongs to ONE contact, it is acknowledged on the air,
// and it is stored in that contact's own log (item 4). The RAM shape matches: one ring of messages, each tagged
// with the peer's full public key, so a thread is a filtered view and no per-thread ring has to be allocated.
constexpr size_t kMaxDms = 64;   // all threads together; the oldest goes when it is full (the channel rule)

struct DmMessage {
    uint32_t seq = 0;           // assigned by dmPush: monotonic, the thing unread is counted against
    uint8_t  peer_pub[32] = {0};
    char     peer_name[kNameBytes] = {0};   // the name as it was when the message arrived
    bool     outgoing = false;
    char     text[kTextBytes] = {0};
    uint32_t ts = 0;            // the MeshCore timestamp in the frame (NOT a clock source)
    uint32_t local_ts = 0;      // our clock when it arrived (ordering and local HH:MM)
    int16_t  snr_x10 = 0;
    bool     direct = false;    // path_len 0 in the received frame
    // S515/EST-462: the same real-path facts a channel message carries (see Message above). A DM has one thread
    // per peer, but the route is the message's own: one line can arrive direct and the next through repeaters.
    bool     hops_known = false;
    uint8_t  hops       = 0;
    uint8_t  hash_size  = 0;
    uint8_t  path_bytes = 0;
    uint8_t  path[meshcore::kMaxPath] = {0};
    uint16_t  frame_len = 0;
    RouteType route     = RouteType::Unknown;
    uint16_t  copies    = 0;
    uint32_t  frame_fp  = 0;
    uint32_t log_id = 0;        // id in that contact's log file (item 4), 0 = not written
    // S513/EST-439: for OUR outgoing DMs, the delivery state the ACK proves (the L2's DmState). An incoming
    // message leaves this at Sending; only an outgoing one is ever advanced, and the log's Update record keeps it
    // across a restart (dm_delivery.h + history_log.h).
    om::ui::DmState delivery = om::ui::DmState::Sending;
};

struct DmRing {
    DmMessage m[kMaxDms];
    size_t    head = 0;
    size_t    count = 0;
    uint32_t  next_seq = 0;
};

uint32_t        dmPush(DmRing& r, const DmMessage& msg);
const DmMessage* dmAt(const DmRing& r, size_t i);

// Indices of one peer's messages, oldest first. Returns how many were written.
size_t dmForPeer(const DmRing& r, const uint8_t pub[32], int* out, size_t max_out);

// A peer's messages newer than its contact row's `dm_seen`. The contact row is the seen point (item 3); a peer
// that is not in the table cannot have a thread yet, so the answer is 0 rather than a guess.
uint32_t dmUnreadForPeer(const DmRing& r, const ContactStore& s, const uint8_t pub[32]);
void     dmMarkPeerRead(const DmRing& r, ContactStore& s, const uint8_t pub[32]);

// The newest log id in a peer's thread (0 when it has none): the value the SD seen point moves to.
uint32_t dmNewestLogId(const DmRing& r, const uint8_t pub[32]);

// One line for the DM column: the newest message and how much of the thread is unread. `contact` is the row in
// the ContactStore, or -1 when the peer left the table (the preview is still real).
struct DmSummary {
    bool        has = false;
    int         contact = -1;
    uint8_t     pub[32] = {0};   // the thread's key (prefix-only for a restored thread not yet rebound)
    const char* name = "";
    const char* text = "";
    uint32_t    ts = 0;
    uint32_t    unread = 0;
};
DmSummary dmSummaryForPeer(const DmRing& r, const ContactStore& s, const uint8_t pub[32]);

// The DM column's order: threads that have messages, newest first. Writes one summary per thread (up to
// `max_out`) and returns how many; a thread with no contact row still appears, named by its 8-hex prefix later.
size_t dmThreadSummaries(const DmRing& r, const ContactStore& s, DmSummary* out, size_t max_out);


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

// S515/EST-461: the ONE counter behind both the chat list's badges and the Chat sigil's dot. Channels are
// counted with unreadForChannel() and DMs with dmUnreadForPeer() — the very functions the cards use, so a dot
// can never disagree with the badge beside it. `channel_count` is clamped to kMaxChannels; a null out gets
// nothing written.
void unreadTotals(const MessageRing& r, const ReadState& rs, size_t channel_count, const DmRing& d,
                  const ContactStore& s, uint32_t* channel_unread, uint32_t* dm_unread);

}  // namespace mc
}  // namespace om
