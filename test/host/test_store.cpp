// SPDX-License-Identifier: MIT
// Host tests for the store the screens read from (S502, step 4): contacts keyed by public key, a message ring
// with a drop rule, and the unread arithmetic. Real exit code: 0 = every check passed.
//
// These are the rules that would be expensive to discover on the glass: a repeat advert adding a second row, a
// ring that loses the newest instead of the oldest, or an unread count that goes to four billion because the
// ring dropped what was already read (the L2 shipped exactly that once).
#include <stdio.h>
#include <string.h>

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

#define CHECK_STR(got, want)                                                          \
  do {                                                                                \
    ++g_checks;                                                                       \
    if (strcmp((got), (want)) != 0) {                                                 \
      printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (got), (want));     \
      ++g_failures;                                                                   \
      return;                                                                         \
    }                                                                                 \
  } while (0)

void pubOf(uint8_t out[32], uint8_t fill) {
    memset(out, 0, 32);
    out[0] = fill;
    out[31] = (uint8_t)(fill ^ 0x5A);
}

void test_contact_upsert_by_key() {
    om::mc::ContactStore s;
    uint8_t             a[32], b[32];
    pubOf(a, 0x11);
    pubOf(b, 0x22);

    CHECK_EQ(om::mc::contactsUpsert(s, a, "ERA-3 (SI)", 1, 0, 1000, 5000, -71, 32), 0);
    CHECK_EQ(s.count, 1);
    CHECK_STR(s.c[0].name, "ERA-3 (SI)");
    CHECK_EQ(s.c[0].type, 1);
    CHECK_EQ(s.c[0].rssi, -71);
    CHECK_EQ(s.c[0].snr_x10, 32);

    // The same key again is the SAME node, refreshed — not a second row.
    CHECK_EQ(om::mc::contactsUpsert(s, a, "ERA-3 (SI)", 1, 4, 2000, 6000, -60, 75), 0);
    CHECK_EQ(s.count, 1);
    CHECK_EQ(s.c[0].path_len, 4);
    CHECK_EQ(s.c[0].heard_at, 6000u);
    CHECK_EQ(s.c[0].remote_ts, 2000u);
    CHECK_EQ(s.c[0].snr_x10, 75);

    CHECK_EQ(om::mc::contactsUpsert(s, b, "TNMT (SI)", 1, 0, 3000, 7000, -80, -15), 1);
    CHECK_EQ(s.count, 2);
    CHECK_EQ(om::mc::contactsFind(s, a), 0);
    CHECK_EQ(om::mc::contactsFind(s, b), 1);

    // An advert whose app_data carries no name must not erase the name we already know.
    CHECK_EQ(om::mc::contactsUpsert(s, b, "", 1, 0, 3100, 7100, -80, -15), 1);
    CHECK_STR(s.c[1].name, "TNMT (SI)");
}

void test_contact_store_limits() {
    om::mc::ContactStore s;
    uint8_t             pub[32];
    // S515/EST-463: the table is 2,000 rows now, so a one-byte seed would collide at 256. Spread the index over
    // the first two bytes instead (everything below stays byte-sized and unaffected).
    for (size_t i = 0; i < om::mc::kMaxContacts; i++) {
        pubOf(pub, (uint8_t)i);
        pub[1] = (uint8_t)(i >> 8);
        CHECK_EQ(om::mc::contactsUpsert(s, pub, "node", 1, 0, (uint32_t)i, (uint32_t)i, 0, 0), (long)i);
    }
    CHECK_EQ(s.count, om::mc::kMaxContacts);
    // Full: a NEW node is refused (and nothing is evicted), an EXISTING one still updates.
    pubOf(pub, 0xEE);
    pub[1] = 0x77;   // a key that is NOT one of the 2,000 (a one-byte seed would match row 238)
    pub[2] = 0xAA;
    CHECK_EQ(om::mc::contactsUpsert(s, pub, "late", 1, 0, 9, 9, 0, 0), -1);
    CHECK_EQ(s.count, om::mc::kMaxContacts);
    pubOf(pub, 3);
    CHECK_EQ(om::mc::contactsUpsert(s, pub, "renamed", 1, 0, 99, 99, 0, 0), 3);
    CHECK_STR(s.c[3].name, "renamed");
    // A long name is cut to fit, not overrun.
    char longname[80];
    memset(longname, 'x', sizeof(longname));
    longname[sizeof(longname) - 1] = '\0';
    pubOf(pub, 5);
    om::mc::contactsUpsert(s, pub, longname, 1, 0, 1, 1, 0, 0);
    CHECK_EQ(strlen(s.c[5].name), om::mc::kNameBytes - 1);
}

void test_contact_order_and_counts() {
    om::mc::ContactStore s;
    uint8_t             pub[32];
    // Three nodes, deliberately inserted out of order.
    pubOf(pub, 1);
    om::mc::contactsUpsert(s, pub, "old", 1, 0, 0, 100, 0, 0);
    pubOf(pub, 2);
    om::mc::contactsUpsert(s, pub, "newest", 1, 0, 0, 300, 0, 0);
    pubOf(pub, 3);
    om::mc::contactsUpsert(s, pub, "fav-old", 2, 0, 0, 50, 0, 0);
    s.c[2].favourite = true;

    static int order[om::mc::kMaxContacts];   // 2,000 ints: never a task stack frame (S515/EST-463)
    size_t n = om::mc::contactsOrder(s, order, om::mc::kMaxContacts);
    CHECK_EQ(n, 3);
    // Favourites first even when older, then newest heard.
    CHECK_EQ(order[0], 2);
    CHECK_EQ(order[1], 1);
    CHECK_EQ(order[2], 0);
    // A short buffer is honoured, not overrun.
    int two[2];
    CHECK_EQ(om::mc::contactsOrder(s, two, 2), 2);
    CHECK_EQ(two[0], 2);

    // "in the last hour" is inclusive of the boundary, and a node heard "in the future" is not counted.
    CHECK_EQ(om::mc::contactsHeardWithin(s, 1000, 700), 1);    // the node heard at 300 is exactly 700 ago
    CHECK_EQ(om::mc::contactsHeardWithin(s, 1000, 699), 0);
    CHECK_EQ(om::mc::contactsHeardWithin(s, 1000, 1000), 3);
    CHECK_EQ(om::mc::contactsHeardWithin(s, 40, 3600), 0);
}

om::mc::Message msg(uint8_t ch, const char* sender, const char* text, uint32_t local_ts) {
    om::mc::Message m;
    m.channel  = ch;
    m.outgoing = false;
    snprintf(m.sender, sizeof(m.sender), "%s", sender);
    snprintf(m.text, sizeof(m.text), "%s", text);
    m.ts       = local_ts;
    m.local_ts = local_ts;
    return m;
}

void test_message_ring_drops_the_oldest() {
    om::mc::MessageRing r;
    char               text[16];
    for (size_t i = 0; i < om::mc::kMaxMessages; i++) {
        snprintf(text, sizeof(text), "m%u", (unsigned)i);
        om::mc::messagesPush(r, msg(0, "n", text, (uint32_t)i));
    }
    CHECK_EQ(r.count, om::mc::kMaxMessages);
    CHECK_STR(om::mc::messagesAt(r, 0)->text, "m0");

    snprintf(text, sizeof(text), "last");
    om::mc::messagesPush(r, msg(0, "n", text, 999));
    CHECK_EQ(r.count, om::mc::kMaxMessages);
    CHECK_STR(om::mc::messagesAt(r, 0)->text, "m1");              // the oldest went
    CHECK_STR(om::mc::messagesAt(r, om::mc::kMaxMessages - 1)->text, "last");
    CHECK(om::mc::messagesAt(r, om::mc::kMaxMessages) == nullptr);
}

void test_per_channel_and_unread() {
    om::mc::MessageRing r;
    om::mc::ReadState   rs;
    om::mc::messagesPush(r, msg(0, "A", "one", 10));
    om::mc::messagesPush(r, msg(1, "B", "other", 11));
    om::mc::messagesPush(r, msg(0, "C", "two", 12));

    CHECK_EQ(om::mc::messagesCountForChannel(r, 0), 2);
    CHECK_EQ(om::mc::messagesCountForChannel(r, 1), 1);
    CHECK_EQ(om::mc::messagesCountForChannel(r, 5), 0);

    int idx[8];
    CHECK_EQ(om::mc::messagesForChannel(r, 0, idx, 8), 2);
    CHECK_EQ(idx[0], 0);
    CHECK_EQ(idx[1], 2);

    CHECK_EQ(om::mc::unreadForChannel(r, rs, 0), 2);   // nothing read yet
    om::mc::markChannelRead(r, rs, 0);
    CHECK_EQ(om::mc::unreadForChannel(r, rs, 0), 0);
    om::mc::messagesPush(r, msg(0, "D", "three", 13));
    CHECK_EQ(om::mc::unreadForChannel(r, rs, 0), 1);

    // The summary is the NEWEST message of that channel, even when another channel is newer overall.
    om::mc::ChannelSummary sum = om::mc::channelSummary(r, 1);
    CHECK_EQ(sum.has, true);
    CHECK_STR(sum.sender, "B");
    CHECK_STR(sum.text, "other");
    CHECK_EQ(sum.ts, 11u);
    sum = om::mc::channelSummary(r, 7);
    CHECK_EQ(sum.has, false);

    // Ring overflow must not turn "read" into a huge unread count: seen > have reads as 0.
    om::mc::MessageRing small;
    om::mc::ReadState   rs2;
    for (size_t i = 0; i < om::mc::kMaxMessages; i++) om::mc::messagesPush(small, msg(0, "n", "x", (uint32_t)i));
    om::mc::markChannelRead(small, rs2, 0);
    CHECK_EQ(rs2.seen[0], om::mc::kMaxMessages);   // ids 1..64 pushed, so the highest seen is 64
    // Now let the ring drop five of what was "seen".
    for (int i = 0; i < 5; i++) om::mc::messagesPush(small, msg(0, "n", "y", (uint32_t)(100 + i)));
    CHECK_EQ(om::mc::messagesCountForChannel(small, 0), om::mc::kMaxMessages);
    CHECK_EQ(om::mc::unreadForChannel(small, rs2, 0), 5);   // the five new ones, even though the count is flat
    om::mc::markChannelRead(small, rs2, 0);
    CHECK_EQ(om::mc::unreadForChannel(small, rs2, 0), 0);
}

void test_ages() {
    char out[16];
    const uint32_t now = 1000000;
    om::mc::formatAge(now, 0, out, sizeof(out));
    CHECK_STR(out, "-");
    om::mc::formatAge(now, now + 5, out, sizeof(out));      // a peer's clock ahead of ours
    CHECK_STR(out, "now");
    om::mc::formatAge(now, now - 59, out, sizeof(out));
    CHECK_STR(out, "now");
    om::mc::formatAge(now, now - 60, out, sizeof(out));
    CHECK_STR(out, "1m");
    om::mc::formatAge(now, now - 3599, out, sizeof(out));
    CHECK_STR(out, "59m");
    om::mc::formatAge(now, now - 3600, out, sizeof(out));
    CHECK_STR(out, "1h");
    om::mc::formatAge(now, now - 86399, out, sizeof(out));
    CHECK_STR(out, "23h");
    om::mc::formatAge(now, now - 86400, out, sizeof(out));
    CHECK_STR(out, "1d");
    om::mc::formatAge(now, now + 100, out, 4);              // a tiny buffer must not overrun
    CHECK_EQ(strlen(out) < 4, true);
}

// S513/EST-434: per-contact DM threads. The ring is shared, the threads are views, and unread is counted from
// each contact's own seen point — marking one thread read must not clear another.
void test_dm_threads_and_unread() {
    om::mc::ContactStore s;
    uint8_t             a[32], b[32];
    pubOf(a, 0x21);
    pubOf(b, 0x42);
    CHECK(om::mc::contactsUpsert(s, a, "Alpha", 1, 0, 0, 100, -70, 30) == 0);
    CHECK(om::mc::contactsUpsert(s, b, "Beta", 1, 0, 0, 90, -80, 10) == 1);

    om::mc::DmRing r;
    auto push = [&](const uint8_t* pub, const char* text, uint32_t local_ts, uint32_t log_id) {
        om::mc::DmMessage m;
        memcpy(m.peer_pub, pub, 32);
        snprintf(m.peer_name, sizeof(m.peer_name), "%s", pub == a ? "Alpha" : "Beta");
        snprintf(m.text, sizeof(m.text), "%s", text);
        m.local_ts = local_ts;
        m.log_id   = log_id;
        return om::mc::dmPush(r, m);
    };
    CHECK_EQ(push(a, "one", 10, 1), 1);
    CHECK_EQ(push(a, "two", 20, 2), 2);
    CHECK_EQ(push(b, "hello", 15, 1), 3);

    int idx[8];
    CHECK_EQ(om::mc::dmForPeer(r, a, idx, 8), 2);
    CHECK_EQ(idx[0], 0);
    CHECK_EQ(idx[1], 1);

    // A peer NOT in the contact table has no seen point yet: answer 0 rather than invent one.
    uint8_t stranger[32];
    pubOf(stranger, 0x77);
    CHECK_EQ(om::mc::dmUnreadForPeer(r, s, stranger), 0);
    CHECK_EQ(om::mc::dmUnreadForPeer(r, s, a), 2);
    CHECK_EQ(om::mc::dmUnreadForPeer(r, s, b), 1);

    // Seen points are per contact: reading Alpha leaves Beta untouched, and a later message is unread again.
    om::mc::dmMarkPeerRead(r, s, a);
    CHECK_EQ(om::mc::dmUnreadForPeer(r, s, a), 0);
    CHECK_EQ(om::mc::dmUnreadForPeer(r, s, b), 1);
    CHECK_EQ(push(a, "three", 30, 3), 4);
    CHECK_EQ(om::mc::dmUnreadForPeer(r, s, a), 1);

    // The DM column: newest thread first, with its name, preview and unread count.
    om::mc::DmSummary sums[4];
    CHECK_EQ(om::mc::dmThreadSummaries(r, s, sums, 4), 2);
    CHECK_STR(sums[0].name, "Alpha");
    CHECK_STR(sums[0].text, "three");
    CHECK_EQ(sums[0].unread, 1);
    CHECK_STR(sums[1].name, "Beta");
    CHECK_STR(sums[1].text, "hello");
    CHECK_EQ(sums[1].unread, 1);

    CHECK_EQ(om::mc::dmNewestLogId(r, a), 3);
    CHECK_EQ(om::mc::dmNewestLogId(r, b), 1);

    // Full ring: the oldest message of the whole set goes, and an evicted message cannot keep an unread alive.
    for (size_t i = 0; i < om::mc::kMaxDms; i++) push(b, "filler", 100 + (uint32_t)i, 100 + (uint32_t)i);
    CHECK_EQ(r.count, om::mc::kMaxDms);
    // The four messages before the fillers are exactly the ones evicted: the oldest retained is filler #0.
    CHECK_EQ(om::mc::dmAt(r, 0)->text[0], 'f');
    CHECK_EQ(om::mc::dmAt(r, 0)->local_ts, 100);
    CHECK_EQ(om::mc::dmAt(r, r.count - 1)->local_ts, 100 + (uint32_t)om::mc::kMaxDms - 1);
}

}  // namespace

int main() {
    test_contact_upsert_by_key();
    test_contact_store_limits();
    test_contact_order_and_counts();
    test_message_ring_drops_the_oldest();
    test_per_channel_and_unread();
    test_dm_threads_and_unread();
    test_ages();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
