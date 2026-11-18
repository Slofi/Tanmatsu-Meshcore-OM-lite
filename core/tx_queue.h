// SPDX-License-Identifier: MIT
// The send queue's RULES (S513): what a send request is, and the order/limits a queue of them keeps.
//
// Why this is not just "a variable": the radio has ONE owner (the radio task), and a key handler on the main task
// can produce requests faster than the air can carry them (a 50-byte frame at SF8 / 62.5 kHz is about half a
// second). S501's single `g_pending` slot let the main task write the very buffer the radio task was reading, and
// a second request silently REPLACED the first — a message could vanish after the composer had already cleared.
//
// The rules, in one place so the host tests test the code the device runs:
//   · capacity is FOUR requests; the fifth push is refused (never a replacement, never a drop of an older one);
//   · order is FIFO, so a burst is sent in the order it was typed;
//   · a request is COPIED BY VALUE (channel, sender, text), so the caller's buffer is its own again the moment
//     pushText() returns and the radio task can read the copy safely;
//   · an advert NEVER displaces a queued text, and the schedule may ask hasText() before spending a slot on an
//     advert at all.
//
// Device glue (main/mc_link.cpp) puts a mutex around this ring so two tasks can push/pop; the ring itself stays
// free of FreeRTOS so the same logic is exercised on the host.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace mc {

constexpr size_t kTxQueueSlots = 4;
constexpr size_t kTxSenderBytes = 32;
constexpr size_t kTxTextBytes   = 200;

enum class TxKind : uint8_t { Text = 0, Advert = 1 };

// One request, complete in itself: nothing points back at a caller's buffer.
struct TxRequest {
    TxKind  kind    = TxKind::Text;
    uint8_t channel = 0;                      // which channel a text goes to (ignored for an advert)
    char    sender[kTxSenderBytes] = {0};     // the node's own name, copied in at push time
    char    text[kTxTextBytes]     = {0};     // the message body (empty for an advert)
};

class TxQueue {
  public:
    // A text for channel `channel`. False when the queue is full (the caller keeps its text) or the arguments are
    // not a message. Never replaces a queued request.
    bool pushText(uint8_t channel, const char* sender, const char* text);

    // An advert. False when the queue is full: an advert is never allowed to push a message out of the way.
    bool pushAdvert(const char* sender);

    // The oldest request, removed from the queue. False when the queue is empty.
    bool pop(TxRequest* out);

    // A text is waiting: the advert schedule uses this to stay out of the way of what the user typed.
    bool hasText() const;
    bool empty() const { return len_ == 0; }
    bool full() const { return len_ == kTxQueueSlots; }
    size_t size() const { return len_; }
    void clear() { head_ = 0; len_ = 0; }

  private:
    TxRequest q_[kTxQueueSlots];
    size_t    head_ = 0;   // index of the oldest request
    size_t    len_  = 0;   // how many are in the ring
};

}  // namespace mc
}  // namespace om
