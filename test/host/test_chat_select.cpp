// SPDX-License-Identifier: MIT
// Host tests for selecting a chat message, its menu, and the Info sheet (S515/EST-469). Real exit code: 0 = pass.
//
// What these pin, and why each is the kind of rule that fails silently on the glass:
//   · the box is kept by SEQ, so a message that arrives while it is up must NOT move the frame;
//   · Down past the newest LEAVES the box (the ticket's second way out), Up at the oldest stops;
//   · a channel message carries only the sender's NAME, so a name that matches two nodes must name nobody
//     (exactly chat_meta's rule for a path hash) and the row must say why;
//   · our own line offers Info only;
//   · the reply prefix is the MeshCore apps' "@Name " — and a DM's reply adds nothing;
//   · the Info sheet says "not kept after a restart" for the live-radio facts a restored record does not carry,
//     instead of printing a zero that would read as a measurement. No RSSI, no SNR (DEVICE.md § 5b).
#include <stdio.h>
#include <string.h>

#include "chat_select.h"
#include "mc_store.h"

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

#define CHECK_STR(got, want)                                                        \
  do {                                                                              \
    ++g_checks;                                                                     \
    if (strcmp((got), (want)) != 0) {                                               \
      printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (got), (want));   \
      ++g_failures;                                                                 \
      return;                                                                       \
    }                                                                               \
  } while (0)

// A full-key contact: a prefix-only row (bytes 4..31 all zero) is NOT a node and must never resolve a name.
void addContact(om::mc::ContactStore& s, const uint8_t* head, size_t head_len, const char* name, uint8_t seed,
                bool full_key = true) {
    uint8_t pub[32] = {0};
    for (size_t i = 0; i < sizeof(pub); ++i) pub[i] = (uint8_t)(0xA0 + i + seed);
    for (size_t i = 0; i < head_len && i < sizeof(pub); ++i) pub[i] = head[i];
    if (!full_key) {
        for (size_t i = 4; i < sizeof(pub); ++i) pub[i] = 0;
    }
    om::mc::contactsUpsert(s, pub, name, 1, 1, 100, 100, 0, 0);
}

// ---- selection: enter, move, leave -------------------------------------------------------------------------
void test_selection_enter_and_move() {
    CHECK_EQ(om::ui::chatSelEnter(0), -1);   // nothing to select: the box does not open
    CHECK_EQ(om::ui::chatSelEnter(1), 0);
    CHECK_EQ(om::ui::chatSelEnter(7), 6);    // the NEWEST message

    // Up from the newest walks back one message per press.
    om::ui::ChatSelMove m = om::ui::chatSelMove(6, -1, 7);
    CHECK(m.active);
    CHECK_EQ(m.index, 5);
    // Up at the oldest stops there (there is no history above it).
    m = om::ui::chatSelMove(0, -1, 7);
    CHECK(m.active);
    CHECK_EQ(m.index, 0);
    // Down from the oldest moves towards the newest...
    m = om::ui::chatSelMove(0, 1, 7);
    CHECK(m.active);
    CHECK_EQ(m.index, 1);
    // ...and Down PAST the newest leaves the box (the ticket's second way out).
    m = om::ui::chatSelMove(6, 1, 7);
    CHECK_EQ(m.active, false);
    CHECK_EQ(m.index, -1);

    // A selection index outside the list is clamped into it first, never trusted.
    m = om::ui::chatSelMove(99, -1, 3);
    CHECK(m.active);
    CHECK_EQ(m.index, 1);
    // No messages at all: nothing to move in, and the box stays closed.
    m = om::ui::chatSelMove(-1, -1, 0);
    CHECK_EQ(m.active, false);
}

void test_selection_stays_on_the_message_by_seq() {
    // The ids the rings hand out. While the box is up a NEW message arrives at the end.
    const uint32_t before[4] = {11, 12, 13, 14};
    const uint32_t after[5]  = {11, 12, 13, 14, 15};

    CHECK_EQ(om::ui::chatSelIndexForSeq(before, 4, 13), 2);
    CHECK_EQ(om::ui::chatSelIndexForSeq(after, 5, 13), 2);   // same message, same place: it is older than the new one
    CHECK_EQ(om::ui::chatSelIndexForSeq(after, 5, 15), 4);

    // The box on the NEWEST message: after an arrival it is no longer the newest — and that is right, the frame
    // stays on the line it was put on, not on "the newest".
    CHECK_EQ(om::ui::chatSelIndexForSeq(before, 4, 14), 3);
    // Gone (the ring evicted it, or another chat was opened): -1, and the caller closes the box.
    CHECK_EQ(om::ui::chatSelIndexForSeq(after, 5, 99), -1);
    CHECK_EQ(om::ui::chatSelIndexForSeq(nullptr, 5, 11), -1);
    CHECK_EQ(om::ui::chatSelIndexForSeq(after, 5, 0), -1);

    // The scroll follows the selected MESSAGE: index 3 of 5 leaves one newer message below the window.
    CHECK_EQ(om::ui::chatSelScroll(3, 5), 1);
    CHECK_EQ(om::ui::chatSelScroll(4, 5), 0);
    CHECK_EQ(om::ui::chatSelScroll(0, 5), 4);   // the oldest message must be reachable
    CHECK_EQ(om::ui::chatSelScroll(-1, 5), 0);
    CHECK_EQ(om::ui::chatSelScroll(0, 0), 0);
}

// ---- the menu, per message kind ----------------------------------------------------------------------------
void test_menu_incoming_channel() {
    om::ui::ChatMsgContext c;
    c.kind       = om::ui::ChatMsgKind::IncomingChannel;
    c.sender     = "TEST-A1";
    c.node_index = 3;
    c.node_note  = "";
    c.node_has_position = true;

    om::ui::ChatMsgRow rows[om::ui::kMaxChatMenuRows];
    const int n = om::ui::chatMsgRows(c, rows, om::ui::kMaxChatMenuRows);
    CHECK_EQ(n, 5);
    CHECK(rows[0].action == om::ui::ChatMsgAction::Reply);
    CHECK_STR(rows[0].label, "Reply");
    CHECK(rows[1].action == om::ui::ChatMsgAction::DirectMessage);
    CHECK_STR(rows[1].label, "DM TEST-A1");   // the ticket's row, carrying the sender's name
    CHECK(rows[2].action == om::ui::ChatMsgAction::Map);
    CHECK(rows[3].action == om::ui::ChatMsgAction::Node);
    CHECK(rows[4].action == om::ui::ChatMsgAction::Info);
    for (int i = 0; i < n; ++i) CHECK_EQ(rows[i].dim, false);
}

void test_menu_incoming_dm() {
    om::ui::ChatMsgContext c;
    c.kind       = om::ui::ChatMsgKind::IncomingDm;
    c.sender     = "TEST-B2";
    c.node_index = 1;
    c.in_dm      = true;
    c.node_has_position = false;

    om::ui::ChatMsgRow rows[om::ui::kMaxChatMenuRows];
    const int n = om::ui::chatMsgRows(c, rows, om::ui::kMaxChatMenuRows);
    CHECK_EQ(n, 4);   // no DM row: this IS the DM
    CHECK(rows[0].action == om::ui::ChatMsgAction::Reply);
    CHECK(rows[1].action == om::ui::ChatMsgAction::Map);
    CHECK_EQ(rows[1].dim, true);   // the node has no position
    CHECK_STR(rows[1].value, "no position");
    CHECK(rows[2].action == om::ui::ChatMsgAction::Node);
    CHECK_EQ(rows[2].dim, false);
    CHECK(rows[3].action == om::ui::ChatMsgAction::Info);
}

void test_menu_own_message() {
    om::ui::ChatMsgContext c;
    c.kind  = om::ui::ChatMsgKind::Own;
    c.sender = "TNMT (SI)";
    om::ui::ChatMsgRow rows[om::ui::kMaxChatMenuRows];
    const int n = om::ui::chatMsgRows(c, rows, om::ui::kMaxChatMenuRows);
    CHECK_EQ(n, 1);
    CHECK(rows[0].action == om::ui::ChatMsgAction::Info);   // nothing writes from our own line
    CHECK_STR(rows[0].label, "Info");
}

void test_menu_unknown_node_is_dim_with_its_reason() {
    om::ui::ChatMsgContext c;
    c.kind       = om::ui::ChatMsgKind::IncomingChannel;
    c.sender     = "SOMEBODY";
    c.node_index = -1;
    c.node_note  = "unknown node";
    c.node_has_position = false;

    om::ui::ChatMsgRow rows[om::ui::kMaxChatMenuRows];
    const int n = om::ui::chatMsgRows(c, rows, om::ui::kMaxChatMenuRows);
    CHECK_EQ(n, 5);
    CHECK_EQ(rows[0].dim, false);   // Reply always works: it is only the draft
    CHECK_EQ(rows[1].dim, true);
    CHECK_STR(rows[1].value, "unknown node");
    CHECK_EQ(rows[2].dim, true);
    CHECK_STR(rows[2].value, "unknown node");
    CHECK_EQ(rows[3].dim, true);
    CHECK_STR(rows[3].value, "unknown node");
    CHECK_EQ(rows[4].dim, false);   // Info needs no node
}

// ---- the name-to-node rule ---------------------------------------------------------------------------------
void test_node_by_name_unique_only() {
    om::mc::ContactStore s;
    const uint8_t        a[2] = {0x11, 0x22};
    const uint8_t        b[2] = {0x33, 0x44};
    addContact(s, a, 2, "TEST-A1", 0);
    addContact(s, b, 2, "TEST-B2", 40);

    om::ui::ChatNodeByName m = om::ui::chatNodeByName(s, "TEST-A1");
    CHECK_EQ(m.matches, 1);
    CHECK_EQ(m.index, 0);
    char note[40] = "";
    CHECK_STR(om::ui::chatNodeNote(m, note, sizeof(note)), "");

    // A name nobody has, and the empty name, resolve to nobody.
    m = om::ui::chatNodeByName(s, "TEST-Z9");
    CHECK_EQ(m.matches, 0);
    CHECK_EQ(m.index, -1);
    CHECK_STR(om::ui::chatNodeNote(m, note, sizeof(note)), "unknown node");
    m = om::ui::chatNodeByName(s, "");
    CHECK_EQ(m.matches, 0);
    CHECK_EQ(m.index, -1);
}

void test_node_by_name_two_matches_name_nobody() {
    om::mc::ContactStore s;
    const uint8_t        a[2] = {0x11, 0x22};
    const uint8_t        b[2] = {0x33, 0x44};
    const uint8_t        c[2] = {0x55, 0x66};
    addContact(s, a, 2, "SAME-NAME", 0);
    addContact(s, b, 2, "SAME-NAME", 50);
    addContact(s, c, 2, "OTHER", 90);

    om::ui::ChatNodeByName m = om::ui::chatNodeByName(s, "SAME-NAME");
    CHECK_EQ(m.matches, 2);
    CHECK_EQ(m.index, -1);   // two is not a near miss: picking one would be a guess
    char note[40] = "";
    CHECK_STR(om::ui::chatNodeNote(m, note, sizeof(note)), "2 nodes share this name");
}

void test_node_by_name_ignores_a_prefix_only_row() {
    om::mc::ContactStore s;
    const uint8_t        a[4] = {0x77, 0x88, 0x99, 0xAA};
    addContact(s, a, 4, "HALF-ROW", 0, false);   // bytes 4..31 zero: a placeholder, not a node
    om::ui::ChatNodeByName m = om::ui::chatNodeByName(s, "HALF-ROW");
    CHECK_EQ(m.matches, 0);
    CHECK_EQ(m.index, -1);
}

// ---- the reply prefix ---------------------------------------------------------------------------------------
void test_reply_prefix() {
    char buf[40];
    // The MeshCore apps' form: "@Name " with ONE space after the name (firmware/src/main.cpp:1175).
    CHECK_EQ(om::ui::chatReplyPrefix(buf, sizeof(buf), "TEST-A1"), 9);
    CHECK_STR(buf, "@TEST-A1 ");
    CHECK_EQ(om::ui::chatReplyPrefix(buf, sizeof(buf), ""), 0);
    CHECK_STR(buf, "");                         // nothing to address: no stray "@ "
    CHECK_EQ(om::ui::chatReplyPrefix(buf, sizeof(buf), nullptr), 0);
    // A tiny buffer is truncated with a NUL, never overrun.
    char small[5];
    CHECK_EQ(om::ui::chatReplyPrefix(small, sizeof(small), "TEST-A1"), 4);
    CHECK_STR(small, "@TES");
}

// ---- the Info sheet ----------------------------------------------------------------------------------------
void test_info_incoming_channel() {
    om::mc::ContactStore s;
    const uint8_t        rpt[2] = {0x86, 0xa8};
    addContact(s, rpt, 2, "TEST-RPT-A", 0);

    om::ui::ChatInfo in;
    in.kind          = om::ui::ChatMsgKind::IncomingChannel;
    snprintf(in.sender, sizeof(in.sender), "TEST-A1");
    snprintf(in.chat_name, sizeof(in.chat_name), "test");
    snprintf(in.sent, sizeof(in.sent), "09:12");
    snprintf(in.received, sizeof(in.received), "09:12");
    in.route      = om::mc::RouteType::Flood;
    in.hops_known = true;
    in.hops       = 2;
    in.hash_size  = 2;
    in.path_bytes = 4;
    in.path[0]    = 0x86;   // hop 1: a known repeater
    in.path[1]    = 0xa8;
    in.path[2]    = 0x01;   // hop 2: nobody we know, so its hash
    in.path[3]    = 0x02;
    in.frame_len  = 87;
    in.copies     = 2;

    char title[40], sub[48];
    om::ui::chatInfoHead(in, title, sizeof(title), sub, sizeof(sub));
    CHECK_STR(title, "TEST-A1");
    CHECK_STR(sub, "channel test");

    om::ui::InfoRow rows[om::ui::kMaxInfoRows];
    const int n = om::ui::chatInfoRows(in, s, rows, om::ui::kMaxInfoRows);
    CHECK_EQ(n, 8);   // sent, received, route, hops, 2 hops, bytes, heard (chat + sender are the head)
    CHECK_STR(rows[0].label, "Sent");
    CHECK_STR(rows[0].value, "09:12");
    CHECK_STR(rows[1].label, "Received");
    CHECK_STR(rows[1].value, "09:12");
    CHECK_STR(rows[2].label, "Route");
    CHECK_STR(rows[2].value, "flood");
    CHECK_STR(rows[3].label, "Hops");
    CHECK_STR(rows[3].value, "2 hops");
    CHECK_STR(rows[4].label, "Hop 1");
    CHECK_STR(rows[4].value, "TEST-RPT-A (86a8)");   // its hash AND its name
    CHECK_STR(rows[5].label, "Hop 2");
    CHECK_STR(rows[5].value, "0102");                // nobody matches: the hash alone
    CHECK_STR(rows[6].label, "Bytes");
    CHECK_STR(rows[6].value, "87 B");
    CHECK_STR(rows[7].label, "Heard");
    CHECK_STR(rows[7].value, "3\xC3\x97");   // S515 (Claude): copies 2 = two EXTRA copies = heard 3x
}

void test_info_route_words() {
    om::mc::ContactStore s;
    om::ui::InfoRow    rows[om::ui::kMaxInfoRows];
    om::ui::ChatInfo   in;
    in.kind       = om::ui::ChatMsgKind::IncomingDm;
    in.hops_known = true;   // 0 hops = direct, true in both encoders
    in.route      = om::mc::RouteType::ScopedFlood;

    int n = om::ui::chatInfoRows(in, s, rows, om::ui::kMaxInfoRows);
    CHECK(n >= 4);
    CHECK_STR(rows[2].value, "scoped");
    CHECK_STR(rows[3].value, "direct");

    in.route = om::mc::RouteType::Direct;
    n        = om::ui::chatInfoRows(in, s, rows, om::ui::kMaxInfoRows);
    CHECK_STR(rows[2].value, "direct");

    // Our own outgoing line: "you" as the sender and "repeated N×" (echoes of OUR frame), and 0 is a real answer.
    in.outgoing = true;
    n           = om::ui::chatInfoRows(in, s, rows, om::ui::kMaxInfoRows);
    CHECK(n >= 4);
    CHECK_STR(rows[2].value, "direct");
    CHECK_STR(rows[n - 1].label, "Repeated");
    CHECK_STR(rows[n - 1].value, "0\xC3\x97");
}

void test_info_restored_says_not_kept() {
    // A record read back from the card: the path and one wall time survive; route, bytes and copies do not.
    om::mc::ContactStore s;
    om::ui::ChatInfo     in;
    in.kind       = om::ui::ChatMsgKind::IncomingChannel;
    in.restored   = true;
    in.route      = om::mc::RouteType::Unknown;
    in.hops_known = false;
    in.frame_len  = 0;
    in.copies     = 0;
    snprintf(in.sent, sizeof(in.sent), "08:40");   // the log's wall time — stored

    om::ui::InfoRow rows[om::ui::kMaxInfoRows];
    const int      n = om::ui::chatInfoRows(in, s, rows, om::ui::kMaxInfoRows);
    CHECK_EQ(n, 6);   // sent, received, route, hops, bytes, heard
    CHECK_STR(rows[0].value, "08:40");
    CHECK_STR(rows[1].label, "Received");
    CHECK_STR(rows[1].value, "not kept after a restart");
    CHECK_STR(rows[2].value, "not kept after a restart");   // route
    CHECK_STR(rows[3].value, "not kept after a restart");   // hops
    CHECK_STR(rows[4].label, "Bytes");
    CHECK_STR(rows[4].value, "not kept after a restart");
    CHECK_STR(rows[5].value, "not kept after a restart");   // heard

    // A restored record that WAS direct keeps that fact: the record's hops byte 0 means direct in both encoders.
    in.route      = om::mc::RouteType::Direct;
    in.hops_known = true;
    const int m   = om::ui::chatInfoRows(in, s, rows, om::ui::kMaxInfoRows);
    CHECK_EQ(m, 6);
    CHECK_STR(rows[2].value, "direct");
    CHECK_STR(rows[3].value, "direct");
}

void test_info_long_path_folds_instead_of_cutting_silently() {
    // 20 hops is more than the sheet can hold: the LAST row says how many were left out, rather than the sheet
    // ending on Hop 8 and looking complete.
    om::mc::ContactStore s;
    om::ui::ChatInfo     in;
    in.kind       = om::ui::ChatMsgKind::IncomingChannel;
    in.hops_known = true;
    in.hops       = 20;
    in.hash_size  = 1;
    in.path_bytes = 20;
    for (int i = 0; i < 20; ++i) in.path[i] = (uint8_t)(0x10 + i);

    om::ui::InfoRow rows[om::ui::kMaxInfoRows];
    const int      n = om::ui::chatInfoRows(in, s, rows, om::ui::kMaxInfoRows);
    CHECK_EQ(n, om::ui::kMaxInfoRows);
    CHECK_STR(rows[n - 1].label, "");
    CHECK(rows[n - 1].value[0] == '+');   // "+N more"
}

void test_info_head_own_and_dm() {
    om::ui::ChatInfo in;
    in.outgoing = true;
    snprintf(in.sender, sizeof(in.sender), "TNMT (SI)");
    in.kind = om::ui::ChatMsgKind::Own;
    char title[40], sub[48];
    om::ui::chatInfoHead(in, title, sizeof(title), sub, sizeof(sub));
    CHECK_STR(title, "you");
    CHECK_STR(sub, "channel");   // no channel name given: the honest generic

    in.kind = om::ui::ChatMsgKind::IncomingDm;
    in.outgoing = false;
    snprintf(in.sender, sizeof(in.sender), "TEST-B2");
    om::ui::chatInfoHead(in, title, sizeof(title), sub, sizeof(sub));
    CHECK_STR(title, "TEST-B2");
    CHECK_STR(sub, "direct message");
}

}  // namespace

int main() {
    test_selection_enter_and_move();
    test_selection_stays_on_the_message_by_seq();
    test_menu_incoming_channel();
    test_menu_incoming_dm();
    test_menu_own_message();
    test_menu_unknown_node_is_dim_with_its_reason();
    test_node_by_name_unique_only();
    test_node_by_name_two_matches_name_nobody();
    test_node_by_name_ignores_a_prefix_only_row();
    test_reply_prefix();
    test_info_incoming_channel();
    test_info_route_words();
    test_info_restored_says_not_kept();
    test_info_long_path_folds_instead_of_cutting_silently();
    test_info_head_own_and_dm();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
