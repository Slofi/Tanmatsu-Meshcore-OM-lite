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

}  // namespace sdhistory
}  // namespace om
