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

// The file name for one channel, in the L2's shape: "ch-<hash 2 hex>-<sanitised name>.log". The hash is what
// identifies the channel; the name is a readable convenience and may change without breaking the history.
void historyFileName(uint8_t hash, const char* name, char* out, size_t cap);

}  // namespace mc
}  // namespace om
