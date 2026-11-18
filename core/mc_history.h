// SPDX-License-Identifier: MIT
// Chat history on the SD card, in the L2's format — not a second format.
//
// The L2 already has this problem solved and host-tested: `firmware/src/history/history_log.h` holds the record
// layout (10-byte header, CRC-32, a torn last record that a reader must stop at) and the message payload. That
// header is plain C++, so the Tanmatsu uses the SAME encoder and decoder rather than inventing a log the other
// device could not read (BRIEF § 6: reuse, do not rewrite).
//
// What is here is only what this app needs today: encode one message as a record, and read a log back into the
// newest N messages. The L2 store's queue, rotation, updates and paging stay in the L2 until this app needs them.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"

namespace om {
namespace mc {

// One record for one message, in the L2's history format. Returns the record's length, or 0 when it does not
// fit. `log_id` is the message's id IN THE FILE — consecutive, one per stored message. That is what the format's
// Meta record points at and what makes unread survive a restart, and it is deliberately NOT the ring's sequence
// number: that one restarts at 1 every boot (the L2 keeps the two apart for the same reason, `history_log.h`
// Msg: `logId` is the log's id, `seq` is "the RAM ring's per-boot id, kept for the USB dump"). A zero `log_id`
// falls back to the ring's seq, so an older caller still writes a usable record.
int historyEncode(const Message& m, uint32_t log_id, uint8_t* out, int cap);

// S513/EST-434: one DM record, in the SAME L2 format. A DM differs only in the format's `dm` flag and in the
// name (the peer's), so this is the channel encoder with the flag set, not a second layout. S513/EST-439: the
// record also carries the L2's `delivery` byte (history_log.h Msg::delivery), Sending at write time.
int historyEncodeDm(const DmMessage& m, uint32_t log_id, uint8_t* out, int cap);

// S513/EST-439: the delivered mark, the L2's way — an Update record naming a message's log id, appended after
// the message (history_log.h Type::Update / kUpdateState). One record per state change, so a restart applies it
// in file order and the message comes back Delivered without re-sending anything.
int historyEncodeDmState(uint32_t log_id, om::ui::DmState state, uint32_t ack_wall, uint8_t* out, int cap);

// Reads a DM log back into DmMessages (newest `max_out`, oldest first), applying every Update record to the
// message whose log id it names. `peer_pub` and `peer_name` identify the
// file's thread: a record does not carry the peer's key (the L2 names the file dm-<8 hex>), so the caller supplies
// them. The latest Meta record is the seen point and is applied by the caller.
size_t historyLoadDm(const uint8_t* data, size_t len, const uint8_t peer_pub[32], const char* peer_name,
                     DmMessage* out, size_t max_out, uint32_t* out_max_log_id = nullptr);

// Reads a whole log (oldest record first) and keeps the NEWEST `max_out` messages, oldest first, so the caller
// can push them into the ring in order. `channel` is the file's chat, not a record field. Stops at the first
// record that does not validate, which is exactly the torn-tail rule the L2 uses: a half-written last record
// costs that record and nothing before it. `out_max_log_id` (optional) receives the highest log id seen, so the
// caller can continue the numbering after a restart.
size_t historyLoad(const uint8_t* data, size_t len, uint8_t channel, Message* out, size_t max_out,
                   uint32_t* out_max_log_id = nullptr);

// The seen point — the format's Meta record {seenLogId u32, unread u16}. The last one in the file wins, exactly
// as the L2's loader applies it. One is written when a channel is marked read and the value CHANGED, so a
// channel left open does not append a record on every redraw.
int historyEncodeMeta(uint32_t seen_log_id, uint16_t unread, uint8_t* out, int cap);

struct SeenPoint {
    uint32_t seen_log_id = 0;
    uint16_t unread      = 0;
    bool     found       = false;   // false: no Meta record yet (a log written before S503)
};

SeenPoint historyReadSeen(const uint8_t* data, size_t len);

// ---- S513/EST-441: putting ONE restored thread into the store ------------------------------------------------
// The per-thread half of `sdhistory::restoreDmThreads()`, here so the host test pins the rule instead of the FAT
// call around it. The rule: a thread restored from `dm-<8 hex>.log` is a THREAD, never a contact. A contact is
// born from a verified advert only, so this function does not call contactsUpsert() and cannot invent a row.
//
// What it does do:
//   · names the thread from its own records (the first INCOMING record carries the peer's name); with none, the
//     8-hex prefix the caller built from the file name;
//   · if the key is prefix-only, looks for the ONE full-key contact already in the table (an NVS row that came
//     from an advert) whose key starts with those four bytes and rebinds the thread's messages to that full key.
//     None, or two with the same prefix, means it stays orphaned — guessing here is how v41 got it wrong;
//   · pushes the messages oldest first and gives the contact row (when there is one) the seen point and the next
//     log id. A record's `log_id` is the previous boot's; `seq` is dmPush's, and the Meta record names log ids.
struct RestoredDm {
    size_t   messages = 0;      // pushed into the ring
    uint32_t max_log_id = 0;
    uint32_t seen_seq = 0;      // the seq of the newest record the Meta point covers (0 = nothing read)
    bool     seen_found = false;
    bool     rebound = false;   // the prefix was matched to one full-key contact
    char     label[kNameBytes] = {0};   // what the thread is called until an advert renames it
};

size_t dmRestoreThread(DmRing& ring, ContactStore& contacts, const uint8_t pub[32], const DmMessage* recs,
                       size_t n, const SeenPoint& sp, RestoredDm* out);

// The file name for one channel, in the L2's shape: "ch-<hash 2 hex>-<sanitised name>.log". The hash is what
// identifies the channel; the name is a readable convenience and may change without breaking the history.
void historyFileName(uint8_t hash, const char* name, char* out, size_t cap);

}  // namespace mc
}  // namespace om
