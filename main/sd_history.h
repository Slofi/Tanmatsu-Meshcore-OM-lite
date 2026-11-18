// SPDX-License-Identifier: MIT
// Chat history on the SD card: the device side of core/mc_history.*.
//
// One file per channel under /sdcard/om-lite/history/, in the L2's record format, so the same log can be read by
// either device. An append opens, writes one record, flushes and closes: a power cut can then cost at most the
// record being written, and the L2's torn-tail rule makes the reader stop cleanly instead of failing the file.
//
// The card is not always there. Everything here answers false instead of pretending, and the app keeps working
// with RAM-only history: that is a visible limitation, not a silent failure.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_history.h"   // SeenPoint: the seen point the card returns
#include "mc_store.h"

namespace om {
namespace sdhistory {

// Call once after the SD is mounted. Makes the directory and reports whether history is on.
bool begin();
bool ready();

// Append one message to its channel's log. `hash` is the channel's 1-byte hash (what identifies the file).
// `log_id` is the record's id IN THE FILE (see core/mc_history.h): consecutive per file, and the seen point in
// the channel's Meta record refers to it.
bool append(uint8_t channel, uint8_t hash, const char* name, const mc::Message& m, uint32_t log_id);

// Read a channel's newest `max_out` messages, oldest first. `out_max_log_id` (optional) receives the highest id
// the file holds, so the caller can continue numbering after a restart.
size_t load(uint8_t channel, uint8_t hash, const char* name, mc::Message* out, size_t max_out,
            uint32_t* out_max_log_id = nullptr);

// Append the seen point (the format's Meta record) for one channel. Called when a channel is marked read and the
// value changed, so a channel left open does not append on every redraw. Last record wins on load.
bool appendSeen(uint8_t channel, uint8_t hash, const char* name, uint32_t seen_log_id, uint16_t unread);

// The seen point stored in a channel's log. `found` is false when the log has no Meta record (an older log).
mc::SeenPoint loadSeen(uint8_t channel, uint8_t hash, const char* name);

// ---------------------------------------------------------------- direct-message logs (S513/EST-434, item 4)
// One file per contact, named in the L2's shape: dm-<pubkey[0..3] 8 hex>.log. The file name is the thread's id;
// a record does not carry the peer's key (the L2 works the same way), so the 32-byte key is supplied by the caller
// and the first four bytes of it are what names the file.
bool appendDm(const uint8_t pub[32], const mc::DmMessage& m, uint32_t log_id);
size_t loadDm(const uint8_t pub[32], const char* peer_name, mc::DmMessage* out, size_t max_out,
              uint32_t* out_max_log_id = nullptr);
bool appendDmSeen(const uint8_t pub[32], uint32_t seen_log_id, uint16_t unread);

// S513/EST-439: append the delivered mark to a DM's log as the L2's Update record (history_log.h Type::Update),
// naming the message by its log id. The state survives a restart because loadDm applies it in file order.
bool appendDmState(const uint8_t pub[32], uint32_t log_id, om::ui::DmState state, uint32_t ack_wall);
mc::SeenPoint loadDmSeen(const uint8_t pub[32]);

// Scan the history directory at boot and restore every dm-*.log into `ring`/`contacts`. A restored thread's contact
// row has only the first four bytes of the key (that is all the file name holds) with the rest zero; when a real
// advert for that contact arrives, main/mc_link.cpp rebinds the row and its messages to the full key. Returns how
// many threads were restored.
size_t restoreDmThreads(mc::DmRing& ring, mc::ContactStore& contacts);

}  // namespace sdhistory
}  // namespace om
