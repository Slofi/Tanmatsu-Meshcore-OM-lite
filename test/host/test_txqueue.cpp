// SPDX-License-Identifier: MIT
// Host tests for the send queue's rules (S513). The device runs this same ring (core/tx_queue.cpp) behind a mutex,
// so these are the rules that decide whether a typed message can be lost:
//   four accepted, the fifth refused; FIFO; a request is a COPY of what the caller gave; and an advert never
//   displaces a queued text.
#include <stdio.h>
#include <string.h>

#include "tx_queue.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                       \
      return;                                                             \
    }                                                                     \
  } while (0)

#define CHECK_EQ(got, want)                                                          \
  do {                                                                               \
    ++g_checks;                                                                      \
    const long g_ = static_cast<long>(got);                                          \
    const long w_ = static_cast<long>(want);                                         \
    if (g_ != w_) {                                                                  \
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,        \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

// 1. Four accepted, the fifth refused — and the four that were accepted are the first four, not the last four.
void fourAcceptedFifthRefused() {
    om::mc::TxQueue q;
    char text[32];
    for (int i = 0; i < 4; i++) {
        snprintf(text, sizeof(text), "msg %d", i);
        CHECK(q.pushText((uint8_t)i, "TNMT", text));
        CHECK_EQ(q.size(), i + 1);
    }
    CHECK(q.full());
    CHECK(!q.pushText(0, "TNMT", "the fifth"));   // refused
    CHECK_EQ(q.size(), 4);                        // nothing was dropped to make room

    for (int i = 0; i < 4; i++) {
        om::mc::TxRequest r;
        CHECK(q.pop(&r));
        CHECK_EQ((int)r.kind, (int)om::mc::TxKind::Text);
        CHECK_EQ(r.channel, i);
        snprintf(text, sizeof(text), "msg %d", i);
        CHECK(strcmp(r.text, text) == 0);
        CHECK(strcmp(r.sender, "TNMT") == 0);
    }
    CHECK(q.empty());
    CHECK(!q.pop(nullptr));
}

// 2. FIFO across a wrap of the ring: pop and push in the middle, then read in the order they went in.
void fifoOrderAcrossWrap() {
    om::mc::TxQueue q;
    CHECK(q.pushText(0, "TNMT", "a"));
    CHECK(q.pushText(1, "TNMT", "b"));
    CHECK(q.pushText(2, "TNMT", "c"));
    om::mc::TxRequest r;
    CHECK(q.pop(&r));
    CHECK(strcmp(r.text, "a") == 0);
    CHECK(q.pushText(3, "TNMT", "d"));   // lands in the slot "a" freed
    CHECK(q.pushText(4, "TNMT", "e"));
    CHECK(!q.pushText(5, "TNMT", "f"));  // still four

    const char* want[] = {"b", "c", "d", "e"};
    for (int i = 0; i < 4; i++) {
        CHECK(q.pop(&r));
        CHECK(strcmp(r.text, want[i]) == 0);
        CHECK_EQ(r.channel, i + 1);
    }
    CHECK(q.empty());
}

// 3. A request is a COPY: rewriting the caller's buffer after the push changes nothing.
void requestIsACopy() {
    om::mc::TxQueue q;
    char composer[200];
    snprintf(composer, sizeof(composer), "half a sen");
    CHECK(q.pushText(0, "TNMT", composer));
    snprintf(composer, sizeof(composer), "SOMETHING ELSE ENTIRELY");
    om::mc::TxRequest r;
    CHECK(q.pop(&r));
    CHECK(strcmp(r.text, "half a sen") == 0);
}

// 4. An advert never displaces a queued text.
void advertDoesNotReplaceText() {
    om::mc::TxQueue q;
    CHECK(q.pushText(0, "TNMT", "keep me"));
    CHECK(q.pushAdvert("TNMT"));   // queued BEHIND the text, not in front of it
    CHECK_EQ(q.size(), 2);
    CHECK(q.hasText());

    om::mc::TxRequest r;
    CHECK(q.pop(&r));
    CHECK_EQ((int)r.kind, (int)om::mc::TxKind::Text);
    CHECK(strcmp(r.text, "keep me") == 0);
    CHECK(q.pop(&r));
    CHECK_EQ((int)r.kind, (int)om::mc::TxKind::Advert);

    // And with the queue full of texts, an advert is refused rather than allowed to push one out.
    om::mc::TxQueue f;
    for (int i = 0; i < 4; i++) CHECK(f.pushText((uint8_t)i, "TNMT", "text"));
    CHECK(!f.pushAdvert("TNMT"));
    CHECK_EQ(f.size(), 4);
    CHECK(f.pop(&r));
    CHECK_EQ((int)r.kind, (int)om::mc::TxKind::Text);
    CHECK(strcmp(r.text, "text") == 0);
}

// 5. The edges: nothing to send is not a request, and a text longer than the slot is cut, not left unterminated.
void edges() {
    om::mc::TxQueue q;
    CHECK(!q.pushText(0, "TNMT", ""));
    CHECK(!q.pushText(0, "TNMT", nullptr));
    CHECK(q.empty());

    char long_text[400];
    memset(long_text, 'x', sizeof(long_text));
    long_text[sizeof(long_text) - 1] = '\0';
    CHECK(q.pushText(0, "TNMT", long_text));
    om::mc::TxRequest r;
    CHECK(q.pop(&r));
    CHECK_EQ(strlen(r.text), om::mc::kTxTextBytes - 1);   // terminated inside the slot
    CHECK(strlen(r.sender) <= om::mc::kTxSenderBytes - 1);
}

}  // namespace

int main() {
    fourAcceptedFifthRefused();
    fifoOrderAcrossWrap();
    requestIsACopy();
    advertDoesNotReplaceText();
    edges();
    if (g_failures == 0) {
        printf("test_txqueue: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    printf("test_txqueue: %d checks, %d FAILURES\n", g_checks, g_failures);
    return 1;
}
