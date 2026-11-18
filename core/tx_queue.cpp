// SPDX-License-Identifier: MIT
#include "tx_queue.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace mc {

namespace {

void copyText(char* dst, size_t cap, const char* src) {
    if (dst == nullptr || cap == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);   // always terminated, even when the text is longer than the slot
}

}  // namespace

bool TxQueue::pushText(uint8_t channel, const char* sender, const char* text) {
    if (text == nullptr || text[0] == '\0') return false;   // nothing to send is not a request
    if (full()) return false;                              // the caller keeps its text and says so
    TxRequest& r = q_[(head_ + len_) % kTxQueueSlots];
    r            = TxRequest{};   // no stale text from an earlier request survives in the slot
    r.kind       = TxKind::Text;
    r.channel    = channel;
    copyText(r.sender, sizeof(r.sender), sender);
    copyText(r.text, sizeof(r.text), text);
    ++len_;
    return true;
}

bool TxQueue::pushAdvert(const char* sender) {
    if (full()) return false;   // never: an advert does not displace anything, least of all a message
    TxRequest& r = q_[(head_ + len_) % kTxQueueSlots];
    r            = TxRequest{};
    r.kind       = TxKind::Advert;
    copyText(r.sender, sizeof(r.sender), sender);
    ++len_;
    return true;
}

bool TxQueue::pushFrame(TxKind kind, const uint8_t* frame, size_t len) {
    if (kind != TxKind::Frame) return false;   // this entry point is only for built frames
    if (frame == nullptr || len == 0 || len > kTxFrameBytes) return false;
    if (full()) return false;                  // the caller keeps the reply and logs the refusal
    TxRequest& r = q_[(head_ + len_) % kTxQueueSlots];
    r            = TxRequest{};                // no stale frame bytes survive in the slot
    r.kind       = TxKind::Frame;
    memcpy(r.frame, frame, len);
    r.frame_len  = len;
    ++len_;
    return true;
}

bool TxQueue::pop(TxRequest* out) {
    if (len_ == 0 || out == nullptr) return false;
    *out = q_[head_];   // copied out by value: the slot is free for the next push
    head_ = (head_ + 1) % kTxQueueSlots;
    --len_;
    return true;
}

bool TxQueue::hasText() const {
    for (size_t i = 0; i < len_; i++) {
        if (q_[(head_ + i) % kTxQueueSlots].kind == TxKind::Text) return true;
    }
    return false;
}

}  // namespace mc
}  // namespace om
