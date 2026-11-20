// SPDX-License-Identifier: MIT
// S515/EST-467: the new-message pop-up's RULES. Real exit code: 0 = every check passed.
//
// These are the decisions that must be right without a screen: what shows, what replaces it, what the
// "+N more" counts, which key is swallowed, when it times out, the same-chat rule, own messages, and the
// screen-off rule (no pop-up queued for later). The drawing and the task lock are main/chrome.cpp; this file
// drives core/msg_popup.h exactly as the main task does.
#include <stdio.h>
#include <string.h>

#include "msg_popup.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,      \
             #got, #want, g_, w_);                                                    \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

using om::ui::MsgPopup;
using om::ui::PopupChat;

void test_show_replace_and_more() {
  printf("test  a first message shows; a newer one replaces it and counts '+N more'\n");
  MsgPopup p;
  uint8_t  peer_a[32] = {1};
  CHECK(!p.visible);

  CHECK(p.offer(1000, false, false, false, PopupChat::Channel, 2, nullptr));
  CHECK(p.visible);
  CHECK_EQ(p.chat, PopupChat::Channel);
  CHECK_EQ(p.channel, 2);
  CHECK_EQ(p.extra, 0);
  CHECK_EQ(p.until_ms, 1000 + om::ui::kMsgPopupMs);

  // A newer message in the SAME pop-up's life replaces the text (the caller stores the text) and counts.
  CHECK(p.offer(3000, false, false, false, PopupChat::Dm, 0, peer_a));
  CHECK_EQ(p.extra, 1);
  CHECK_EQ(p.chat, PopupChat::Dm);
  CHECK(memcmp(p.peer, peer_a, 32) == 0);
  CHECK_EQ(p.until_ms, 3000 + om::ui::kMsgPopupMs);   // the 10 s restarts

  // A batch of three more arrived between two loop passes: +3 at once.
  CHECK(p.offer(4000, false, false, false, PopupChat::Dm, 0, peer_a, 3));
  CHECK_EQ(p.extra, 4);
  CHECK_EQ(p.until_ms, 4000 + om::ui::kMsgPopupMs);
}

// S516 (EST-473): a BATCH with no pop-up up. The radio task offers every message at once and the main loop polls
// twice a second, so two messages can land in one window: the newest is on the plate and the other one is what
// "+N more" has to count, or the user never learns it arrived.
void test_batch_with_nothing_up() {
  printf("test  two messages in one poll: the newest shows and the other is '+1 more'\n");
  MsgPopup p;
  CHECK(p.offer(1000, false, false, false, PopupChat::Channel, 1, nullptr, 2));
  CHECK_EQ(p.extra, 1);
  CHECK(p.visible);
  CHECK_EQ(p.chat, PopupChat::Channel);
  CHECK_EQ(p.channel, 1);

  // One message alone still says nothing extra.
  MsgPopup q;
  CHECK(q.offer(1000, false, false, false, PopupChat::Channel, 1, nullptr));
  CHECK_EQ(q.extra, 0);

  // The cap holds for a batch too (the poll's own counter stops at 99).
  MsgPopup r;
  CHECK(r.offer(1000, false, false, false, PopupChat::Channel, 1, nullptr, 99));
  CHECK_EQ(r.extra, 98);
}

void test_key_swallow() {
  printf("test  any key dismisses and is reported as swallowed; the next key is not\n");
  MsgPopup p;
  CHECK(p.offer(0, false, false, false, PopupChat::Channel, 1, nullptr));
  CHECK(p.key());          // the caller must swallow this key
  CHECK(!p.visible);
  CHECK(!p.key());         // nothing to swallow now
}

void test_timeout_and_wrap() {
  printf("test  it goes by itself at exactly 10 s, and the 32-bit clock wrap is safe\n");
  MsgPopup p;
  CHECK(p.offer(100, false, false, false, PopupChat::Channel, 0, nullptr));
  CHECK(!p.tick(100 + om::ui::kMsgPopupMs - 1));
  CHECK(p.visible);
  CHECK(p.tick(100 + om::ui::kMsgPopupMs));
  CHECK(!p.visible);
  CHECK(!p.tick(999999));   // already gone: no second repaint

  // until_ms = 100, the clock wrapped past 2^32 and is now small: the signed difference still says "expired".
  MsgPopup q;
  q.until_ms = 100;
  q.visible  = true;
  CHECK(q.tick(200));
  CHECK(!q.visible);
}

void test_same_chat_rule() {
  printf("test  a message for the chat already on screen does not pop up, even if one is up\n");
  uint8_t peer[32] = {7};
  MsgPopup p;
  CHECK(!p.offer(0, false, false, true, PopupChat::Channel, 3, nullptr));
  CHECK(!p.visible);   // not queued for later either

  // A different chat's pop-up is already up; a message for the OPEN chat must not clear it.
  CHECK(p.offer(0, false, false, false, PopupChat::Dm, 0, peer));
  CHECK(p.visible);
  CHECK(!p.offer(10, false, false, true, PopupChat::Channel, 3, nullptr));
  CHECK(p.visible);
  CHECK_EQ(p.chat, PopupChat::Dm);

  CHECK(p.sameChat(PopupChat::Dm, 0, peer));
  CHECK(!p.sameChat(PopupChat::Dm, 0, nullptr));
  CHECK(!p.sameChat(PopupChat::Channel, 0, peer));
  uint8_t other[32] = {8};
  CHECK(!p.sameChat(PopupChat::Dm, 0, other));
}

void test_own_and_screen_off() {
  printf("test  our own message never pops up; screen-off drops it and queues nothing for later\n");
  uint8_t peer[32] = {9};
  MsgPopup p;
  CHECK(!p.offer(0, false, true, false, PopupChat::Dm, 0, peer));   // own
  CHECK(!p.visible);

  // A live pop-up, an own echo arrives: leave the pop-up alone.
  CHECK(p.offer(10, false, false, false, PopupChat::Dm, 0, peer));
  CHECK(!p.offer(20, false, true, false, PopupChat::Channel, 1, nullptr));
  CHECK(p.visible);
  CHECK_EQ(p.extra, 0);

  // Screen off: the sound still plays elsewhere, but the pop-up goes and nothing waits.
  CHECK(p.offer(30, true, false, false, PopupChat::Channel, 1, nullptr));
  CHECK(!p.visible);
  CHECK_EQ(p.extra, 0);
  // Waking up must not reveal it: a tick and an unrelated offer see an empty state.
  CHECK(!p.tick(40));
  CHECK(!p.visible);
}

void test_extra_cap() {
  printf("test  the '+N more' counter saturates instead of wrapping\n");
  MsgPopup p;
  CHECK(p.offer(0, false, false, false, PopupChat::Channel, 0, nullptr));
  for (int i = 0; i < 300; ++i)
    (void)p.offer(0, false, false, false, PopupChat::Channel, 0, nullptr);
  CHECK_EQ(p.extra, om::ui::kMsgPopupMaxExtra);
}

}  // namespace

int main() {
  test_show_replace_and_more();
  test_batch_with_nothing_up();
  test_key_swallow();
  test_timeout_and_wrap();
  test_same_chat_rule();
  test_own_and_screen_off();
  test_extra_cap();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
