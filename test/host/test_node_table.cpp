// SPDX-License-Identifier: MIT
// Host tests for S515/EST-463 items 1 and 2: ONE node table at 2,000 rows, the `in_radio` mark, and the four
// rules Filip decided (nothing goes into the radio automatically; a full radio refuses).
//
// The checks that matter are the ones a wrong rule would make invisible on the glass: an advert quietly filling
// the radio, a full radio pushing an older node out, a "remove" that also loses the node, or an order that
// changes because the table grew.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mc_store.h"
#include "node_table.h"

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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,       \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

void keyFor(uint8_t out[32], uint32_t seed) {
  memset(out, 0, 32);
  for (int i = 0; i < 4; i++) out[i] = (uint8_t)((seed >> (8 * i)) & 0xFF);
  out[4] = (uint8_t)(seed * 7 + 3);
  out[31] = 0xA5;   // a full key: never 4 bytes + zeros (contactsDropIncomplete's rule)
}

int addNode(om::mc::ContactStore& s, uint32_t seed, uint8_t type, uint32_t heard) {
  uint8_t pub[32];
  keyFor(pub, seed);
  return om::mc::advertInArchive(s, om::mc::HeardToRadio::Off, pub, "node", type, 0, heard, heard, 0, 0);
}

// ---- 1. capacity and the one table ---------------------------------------------------------------
void testCapacity() {
  CHECK_EQ(om::mc::kMaxNodes, 2000);
  CHECK_EQ(om::mc::kMaxRadio, 300);
  CHECK_EQ(om::mc::kMaxContacts, om::mc::kMaxNodes);   // the compatibility alias the pages use

  om::mc::ContactStore s;
  CHECK(s.c != nullptr);
  CHECK_EQ(s.cap, om::mc::kMaxNodes);
  CHECK_EQ(s.count, 0);
  // The table is ONE heap/PSRAM allocation, not an inline array: sizeof must not grow with kMaxNodes.
  CHECK(sizeof(om::mc::ContactStore) < 64);
  CHECK_EQ(om::mc::contactsTableBytes(), om::mc::kMaxNodes * sizeof(om::mc::Contact));
}

// ---- 2. the radio mark and Filip's rules ---------------------------------------------------------
void testAdvertNeverAutoAddsWithOff() {
  om::mc::ContactStore s;
  uint8_t pub[32];
  keyFor(pub, 1);
  const int i = om::mc::advertInArchive(s, om::mc::HeardToRadio::Off, pub, "a", (uint8_t)om::mc::NodeType::Chat,
                                       0, 100, 100, -90, 30);
  CHECK_EQ(i, 0);
  CHECK_EQ(s.count, 1);
  CHECK(s.c[0].fresh);        // a new row is the new-node dot
  CHECK(!s.c[0].in_radio);    // and it is NOT in the radio
  CHECK_EQ(om::mc::radioCount(s), 0);
}

void testClientsAddsOnlyType1() {
  om::mc::ContactStore s;
  uint8_t pub[32];
  uint8_t client[32], rptr[32], room[32];
  keyFor(client, 2);
  keyFor(rptr, 3);
  keyFor(room, 4);
  om::mc::advertInArchive(s, om::mc::HeardToRadio::Clients, client, "client", 1, 0, 10, 10, 0, 0);
  om::mc::advertInArchive(s, om::mc::HeardToRadio::Clients, rptr, "rptr", 2, 0, 10, 10, 0, 0);
  om::mc::advertInArchive(s, om::mc::HeardToRadio::Clients, room, "room", 3, 0, 10, 10, 0, 0);
  CHECK_EQ(s.count, 3);
  CHECK(s.c[om::mc::contactsFind(s, client)].in_radio);    // type 1 = a client
  CHECK(!s.c[om::mc::contactsFind(s, rptr)].in_radio);     // a repeater is not a client
  CHECK(!s.c[om::mc::contactsFind(s, room)].in_radio);     // nor a room
  CHECK_EQ(om::mc::radioCount(s), 1);

  // All takes every type; a REFRESH never changes the mark either way.
  om::mc::ContactStore all;
  keyFor(pub, 5);
  om::mc::advertInArchive(all, om::mc::HeardToRadio::All, pub, "rptr", 2, 0, 10, 10, 0, 0);
  CHECK(all.c[0].in_radio);
  om::mc::advertInArchive(all, om::mc::HeardToRadio::Off, pub, "rptr", 2, 0, 20, 20, 0, 0);
  CHECK(all.c[0].in_radio);   // an advert with the setting Off must not EVICT a chosen node either
  CHECK_EQ(om::mc::radioCount(all), 1);
}

void testRadioFullRefusesAndChangesNothing() {
  om::mc::ContactStore s;
  for (size_t i = 0; i < om::mc::kMaxRadio; i++)
    CHECK(addNode(s, (uint32_t)(i + 1), 1, 1000) >= 0);
  for (size_t i = 0; i < om::mc::kMaxRadio; i++) CHECK_EQ(om::mc::addToRadio(s, s.c[i].pub), om::mc::RadioResult::Ok);
  CHECK_EQ(om::mc::radioCount(s), om::mc::kMaxRadio);

  // The 301st: refused, and NOTHING moved — the oldest row is still in, the newcomer still out, order untouched.
  const int extra = addNode(s, 5000, 1, 5000);
  CHECK(extra >= 0);
  const uint8_t* extra_pub = s.c[extra].pub;
  uint8_t pub_copy[32];
  memcpy(pub_copy, extra_pub, 32);
  const uint32_t heard_before = s.c[extra].heard_at;
  const bool fresh_before     = s.c[extra].fresh;
  const size_t count_before   = s.count;
  const size_t radio_before   = om::mc::radioCount(s);
  const size_t first_before   = om::mc::contactsFind(s, s.c[0].pub);

  CHECK_EQ(om::mc::addToRadio(s, pub_copy), om::mc::RadioResult::RadioFull);
  CHECK_EQ(s.count, count_before);
  CHECK_EQ(om::mc::radioCount(s), radio_before);
  CHECK_EQ(om::mc::contactsFind(s, pub_copy), extra);       // not evicted, not re-ordered
  CHECK_EQ(om::mc::contactsFind(s, s.c[0].pub), first_before);   // the oldest chosen node is still chosen
  CHECK(!s.c[extra].in_radio);
  CHECK_EQ(s.c[extra].heard_at, heard_before);
  CHECK_EQ(s.c[extra].fresh, fresh_before);

  // AlreadyIn is idempotent and does not count twice.
  CHECK_EQ(om::mc::addToRadio(s, s.c[0].pub), om::mc::RadioResult::AlreadyIn);
  CHECK_EQ(om::mc::radioCount(s), radio_before);

  uint8_t missing[32];
  keyFor(missing, 99999);
  CHECK_EQ(om::mc::addToRadio(s, missing), om::mc::RadioResult::NotFound);
}

void testRemoveKeepsInArchive() {
  om::mc::ContactStore s;
  addNode(s, 10, 1, 100);
  addNode(s, 11, 1, 200);
  CHECK_EQ(om::mc::addToRadio(s, s.c[0].pub), om::mc::RadioResult::Ok);
  CHECK_EQ(om::mc::radioCount(s), 1);

  CHECK_EQ(om::mc::removeFromRadio(s, s.c[0].pub), om::mc::RadioResult::Ok);
  CHECK_EQ(s.count, 2);            // still a node: DMs must keep working for an archive-only peer
  CHECK(!s.c[0].in_radio);
  CHECK_EQ(om::mc::radioCount(s), 0);
  CHECK_EQ(om::mc::contactsFind(s, s.c[0].pub), 0);
  CHECK_EQ(om::mc::removeFromRadio(s, s.c[0].pub), om::mc::RadioResult::AlreadyIn);
}

void testDeleteRemovesFromBoth() {
  om::mc::ContactStore s;
  addNode(s, 20, 1, 100);
  addNode(s, 21, 1, 200);
  addNode(s, 22, 1, 300);
  CHECK_EQ(om::mc::addToRadio(s, s.c[1].pub), om::mc::RadioResult::Ok);
  uint8_t gone[32];
  memcpy(gone, s.c[1].pub, 32);
  uint8_t kept_key[32];
  memcpy(kept_key, s.c[2].pub, 32);

  CHECK(om::mc::deleteEverywhere(s, gone));
  CHECK_EQ(s.count, 2);
  CHECK_EQ(om::mc::contactsFind(s, gone), -1);          // gone from the archive
  CHECK_EQ(om::mc::radioCount(s), 0);                   // and from the radio
  // Rows after it shifted down: a caller holding index 1 must re-find by key.
  CHECK_EQ(om::mc::contactsFind(s, kept_key), 1);
  CHECK(!om::mc::deleteEverywhere(s, gone));
}

void testFavouriteIndependent() {
  om::mc::ContactStore s;
  addNode(s, 30, 2, 100);
  CHECK(om::mc::setFavourite(s, s.c[0].pub, true));
  CHECK(s.c[0].favourite);
  CHECK(!s.c[0].in_radio);   // a favourite is not a radio node
  CHECK_EQ(om::mc::radioCount(s), 0);
  om::mc::addToRadio(s, s.c[0].pub);
  CHECK(s.c[0].favourite);   // and the mark does not clear it
}

// ---- 3. ordering at 2,000 rows, and the timing printed ------------------------------------------
void testOrderingAt2000() {
  om::mc::ContactStore s;
  // Deterministic pseudo-random heard times, some favourites, some fresh, so all three groups are populated.
  uint32_t x = 12345;
  for (size_t i = 0; i < om::mc::kMaxNodes; i++) {
    x = x * 1103515245u + 12345u;
    const uint32_t heard = 1u + (x >> 8) % 100000u;
    const int idx = addNode(s, (uint32_t)(i + 1), 1, heard);
    CHECK(idx >= 0);
    s.c[idx].fresh = (i % 53 == 0);   // every INSERT is fresh; here most rows are already looked at
    if (i % 97 == 0) s.c[idx].favourite = true;
  }
  CHECK_EQ(s.count, om::mc::kMaxNodes);

  // The buffer the page owns (static in main.cpp's Nodes path, never on a task stack).
  static int order[om::mc::kMaxNodes];
  const clock_t t0 = clock();
  const size_t n = om::mc::contactsOrder(s, order, om::mc::kMaxNodes);
  const double ms = 1000.0 * (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
  printf("order: %u rows sorted in %.2f ms\n", (unsigned)n, ms);
  CHECK_EQ(n, om::mc::kMaxNodes);

  // The rule v58 had: favourites, then the still-fresh rows, then the rest, each newest heard first.
  int group = -1;
  uint32_t prev_heard = 0xFFFFFFFFu;
  bool seen[3] = {false, false, false};
  int  group_of[om::mc::kMaxNodes];
  for (size_t i = 0; i < n; i++) {
    const om::mc::Contact& c = s.c[order[i]];
    const int g = c.favourite ? 0 : (c.fresh ? 1 : 2);
    group_of[i] = g;
    CHECK(g >= group);                     // groups never go backwards
    if (g != group) {
      group      = g;
      prev_heard = 0xFFFFFFFFu;
    }
    CHECK(c.heard_at <= prev_heard);       // descending heard_at inside a group (ties keep store order)
    prev_heard = c.heard_at;
    seen[g] = true;
  }
  int cnt[3] = {0, 0, 0};
  for (size_t i = 0; i < n; i++) cnt[group_of[i]]++;
  printf("groups: %d favourite, %d new, %d rest\n", cnt[0], cnt[1], cnt[2]);
  CHECK(seen[0] && seen[1] && seen[2]);
  // Stability: two rows with the SAME heard_at in the same group keep store order.
  for (size_t i = 1; i < n; i++) {
    if (group_of[i] != group_of[i - 1]) continue;
    if (s.c[order[i]].heard_at != s.c[order[i - 1]].heard_at) continue;
    CHECK(order[i - 1] < order[i]);
  }
  // A short out buffer takes the true head of the same order.
  int head[2];
  CHECK_EQ(om::mc::contactsOrder(s, head, 2), 2);
  CHECK_EQ(head[0], order[0]);
  CHECK_EQ(head[1], order[1]);
}

// ---- 4. unread counts an archive-only DM peer ---------------------------------------------------
void testUnreadTotalsArchiveOnlyPeer() {
  om::mc::ContactStore s;
  uint8_t pub[32];
  keyFor(pub, 40);
  const int idx = om::mc::advertInArchive(s, om::mc::HeardToRadio::Off, pub, "peer", 1, 0, 10, 10, 0, 0);
  CHECK(idx >= 0);
  CHECK(!s.c[0].in_radio);   // archive only…

  om::mc::DmRing d;
  om::mc::DmMessage m;
  memcpy(m.peer_pub, pub, 32);
  snprintf(m.peer_name, sizeof(m.peer_name), "peer");
  snprintf(m.text, sizeof(m.text), "hello");
  om::mc::dmPush(d, m);

  om::mc::MessageRing r;
  om::mc::ReadState rs;
  uint32_t ch = 0, dm = 0;
  om::mc::unreadTotals(r, rs, 1, d, s, &ch, &dm);
  CHECK_EQ(dm, 1);   // …and still raises the dot: the old loop must not stop at the radio rows
  CHECK_EQ(ch, 0);
}

}  // namespace

int main() {
  testCapacity();
  testAdvertNeverAutoAddsWithOff();
  testClientsAddsOnlyType1();
  testRadioFullRefusesAndChangesNothing();
  testRemoveKeepsInArchive();
  testDeleteRemovesFromBoth();
  testFavouriteIndependent();
  testOrderingAt2000();
  testUnreadTotalsArchiveOnlyPeer();
  printf("%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
