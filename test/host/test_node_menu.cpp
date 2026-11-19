// SPDX-License-Identifier: MIT
// Host tests for the Nodes page's NODE MENU and its favourite rule (S514/EST-455). Real exit code: 0 = all pass.
//
// What these pin:
//   · the rows a node type actually gets — clients get DM, repeaters/rooms/sensors do not, and the actions that
//     Ping and Trace are offered only under the L2's conditions (EST-456), Map/Reset path are not offered;
//   · the favourite row's value/accent, the focus clamp and the action Enter would run;
//   · the favourites-first order against Filip's words: favourites by last heard (newest first), then everyone
//     else by last heard, TIES STABLE, and the EST-449 search filter keeps that order inside its matches;
//   · the focus-follows-the-node rule after a toggle, and that the flag survives the real `mcct` blob codec.
#include <stdio.h>
#include <string.h>

#include "contact_admin.h"
#include "contact_blob.h"
#include "mc_store.h"
#include "node_menu.h"
#include "nodes_search.h"

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
      printf("FAIL %s:%d  \"%s\" == \"%s\"\n", __FILE__, __LINE__, (got), (want));  \
      ++g_failures;                                                                 \
      return;                                                                       \
    }                                                                               \
  } while (0)

void pubOf(uint8_t out[32], uint8_t fill) {
  memset(out, 0, 32);
  out[0] = fill;
  out[31] = (uint8_t)(fill ^ 0xA5);
}

void addContact(om::mc::ContactStore& s, uint8_t seed, const char* name, uint8_t type, uint32_t heard,
                bool fav) {
  uint8_t pub[32];
  pubOf(pub, seed);
  const int i = om::mc::contactsUpsert(s, pub, name, type, 0, heard, heard, -70, 20);
  s.c[i].favourite = fav;
}

// ------------------------------------------------------------------------------ 1. rows per node type
void test_rows_per_type() {
  printf("test  rows: client -> DM/Info/Favourite/Remove; repeater/room/sensor -> no DM; no key -> Info\n");
  om::ui::NodeMenuRow  rows[om::ui::kMaxNodeMenuRows];
  om::ui::NodeMenuContext c;

  c.advType   = om::ui::kNodeMenuAdvChat;
  c.hasKey    = true;
  c.pathKnown = true;   // EST-456: a stored route is what makes Trace offerable
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 6);
  CHECK(rows[0].action == om::ui::NodeAction::DirectMessage);
  CHECK_STR(rows[0].label, "Direct message");
  CHECK(rows[1].action == om::ui::NodeAction::Info);
  CHECK(rows[2].action == om::ui::NodeAction::Ping);
  CHECK(rows[3].action == om::ui::NodeAction::Trace);
  CHECK(rows[4].action == om::ui::NodeAction::Favourite);
  CHECK_STR(rows[4].value, "off");
  CHECK(!rows[4].valueAccent);
  CHECK(rows[5].action == om::ui::NodeAction::RemoveContact);
  // The actions that do not exist here are never offered.
  for (int i = 0; i < 6; ++i) {
    CHECK(rows[i].action != om::ui::NodeAction::None);
  }

  // No stored route: the Trace row is ABSENT, not dimmed (EST-456).
  c.pathKnown = false;
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 5);
  for (int i = 0; i < 5; ++i) CHECK(rows[i].action != om::ui::NodeAction::Trace);
  c.pathKnown = true;

  // Favourite on: the value is "on" AND the accent flag is set (never inferred from the word).
  c.favourite = true;
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 6);
  CHECK_STR(rows[4].value, "on");
  CHECK(rows[4].valueAccent);

  // A repeater, room and sensor can each hold a key but cannot take a text DM.
  const uint8_t types[] = {om::ui::kNodeMenuAdvRepeater, om::ui::kNodeMenuAdvRoom, om::ui::kNodeMenuAdvSensor, 0};
  for (uint8_t t : types) {
    c.advType = t;
    c.favourite = false;
    CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 5);
    CHECK(rows[0].action == om::ui::NodeAction::Info);
    CHECK(rows[1].action == om::ui::NodeAction::Ping);
    CHECK(rows[2].action == om::ui::NodeAction::Trace);
    CHECK(rows[3].action == om::ui::NodeAction::Favourite);
    CHECK(rows[4].action == om::ui::NodeAction::RemoveContact);
  }

  // No full key: only Info is honest.
  c.advType = om::ui::kNodeMenuAdvChat;
  c.hasKey  = false;
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 1);
  CHECK(rows[0].action == om::ui::NodeAction::Info);

  // A max of 0/null is answered, not overrun.
  CHECK_EQ(om::ui::nodeMenuRows(c, nullptr, 4), 0);
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, 0), 0);
}

// ------------------------------------------------------------------------------ 2. focus and actions
void test_focus_and_action() {
  printf("test  focus: clamped at both ends; Enter runs the row it is on\n");
  CHECK_EQ(om::ui::nodeMenuFocusMove(0, -1, 4), 0);
  CHECK_EQ(om::ui::nodeMenuFocusMove(0, 1, 4), 1);
  CHECK_EQ(om::ui::nodeMenuFocusMove(3, 1, 4), 3);
  CHECK_EQ(om::ui::nodeMenuFocusMove(9, 0, 4), 3);
  CHECK_EQ(om::ui::nodeMenuFocusMove(-2, 0, 4), 0);
  CHECK_EQ(om::ui::nodeMenuFocusMove(0, 1, 0), 0);

  om::ui::NodeMenuRow rows[om::ui::kMaxNodeMenuRows];
  om::ui::NodeMenuContext c;
  c.advType   = om::ui::kNodeMenuAdvChat;
  c.hasKey    = true;
  c.pathKnown = true;
  const int n = om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows);
  CHECK(om::ui::nodeMenuActionAt(rows, n, 0) == om::ui::NodeAction::DirectMessage);
  CHECK(om::ui::nodeMenuActionAt(rows, n, 2) == om::ui::NodeAction::Ping);
  CHECK(om::ui::nodeMenuActionAt(rows, n, 5) == om::ui::NodeAction::RemoveContact);
  CHECK(om::ui::nodeMenuActionAt(rows, n, 6) == om::ui::NodeAction::None);
  CHECK(om::ui::nodeMenuActionAt(rows, n, -1) == om::ui::NodeAction::None);
  CHECK(om::ui::nodeMenuActionAt(nullptr, n, 0) == om::ui::NodeAction::None);
}

// ------------------------------------------------------------------------------ 3. the notice
void test_notice() {
  printf("test  notice: \"<name>: favourite\" / \"<name>: not favourite\"\n");
  char buf[64];
  om::ui::nodeFavouriteNotice(buf, sizeof(buf), "TEST-A1", true);
  CHECK_STR(buf, "TEST-A1: favourite");
  om::ui::nodeFavouriteNotice(buf, sizeof(buf), "TEST-A1", false);
  CHECK_STR(buf, "TEST-A1: not favourite");
  om::ui::nodeFavouriteNotice(buf, sizeof(buf), "", true);
  CHECK_STR(buf, "this node: favourite");
  om::ui::nodeFavouriteNotice(buf, sizeof(buf), nullptr, false);
  CHECK_STR(buf, "this node: not favourite");
}

// ------------------------------------------------------------------------------ 4. the flag and its blob
void test_favourite_flag_and_blob() {
  printf("test  favourite: only a live row changes; the flag survives the mcct encode/decode\n");
  om::mc::ContactStore s;
  addContact(s, 1, "TEST-A1", om::ui::kNodeMenuAdvChat, 100, false);
  addContact(s, 2, "TEST-B2", om::ui::kNodeMenuAdvRepeater, 200, false);
  CHECK_EQ(s.count, 2);
  CHECK(!s.c[0].favourite);

  CHECK(om::mc::contactSetFavourite(s, 0, true));
  CHECK(s.c[0].favourite);
  CHECK(!s.c[1].favourite);
  CHECK(om::mc::contactSetFavourite(s, 0, false));
  CHECK(!s.c[0].favourite);

  // A stale row must mark nobody, and the table must not move.
  CHECK(!om::mc::contactSetFavourite(s, 2, true));
  CHECK(!om::mc::contactSetFavourite(s, 99, true));
  CHECK_EQ(s.count, 2);

  // Structural change -> the SAME blob the device writes. Favourite is a byte in the record.
  CHECK(om::mc::contactSetFavourite(s, 1, true));
  uint8_t blob[om::mc::kContactBlobMaxBytes];
  const size_t len = om::mc::contactsEncode(s, blob, sizeof(blob));
  CHECK(len > 0);
  om::mc::ContactStore back;
  CHECK(om::mc::contactsDecode(blob, len, back) == om::mc::ContactDecode::Ok);
  CHECK_EQ(back.count, 2);
  CHECK(back.c[1].favourite);
  CHECK(!back.c[0].favourite);
}

// ------------------------------------------------------------------------------ 5. Filip's order rule
void test_order_rule() {
  printf("test  order: favourites by last heard, then the rest by last heard, ties stable\n");
  om::mc::ContactStore s;
  // Deliberately inserted in a non-sorted order; two ties (300 and 400).
  addContact(s, 1, "NODE-old", om::ui::kNodeMenuAdvChat, 100, false);        // store 0
  addContact(s, 2, "NODE-fav-old", om::ui::kNodeMenuAdvChat, 200, true);     // store 1
  addContact(s, 3, "NODE-new", om::ui::kNodeMenuAdvChat, 300, false);        // store 2
  addContact(s, 4, "NODE-fav-new", om::ui::kNodeMenuAdvChat, 400, true);     // store 3
  addContact(s, 5, "NODE-tie", om::ui::kNodeMenuAdvChat, 300, false);        // store 4 (tie with 2)
  addContact(s, 6, "NODE-fav-tie", om::ui::kNodeMenuAdvChat, 400, true);     // store 5 (tie with 3)

  int    order[om::mc::kMaxContacts];
  size_t n = om::mc::contactsOrder(s, order, om::mc::kMaxContacts);
  CHECK_EQ(n, 6);
  // Favourites first: 400 (store 3), 400 (store 5, the later row keeps its place), 200 (store 1).
  CHECK_EQ(order[0], 3);
  CHECK_EQ(order[1], 5);
  CHECK_EQ(order[2], 1);
  // Then everyone else: 300 (store 2), 300 (store 4), 100 (store 0).
  CHECK_EQ(order[3], 2);
  CHECK_EQ(order[4], 4);
  CHECK_EQ(order[5], 0);

  // The search filter keeps THAT order inside its matches: "tie" -> the favourite match first.
  int shown[om::mc::kMaxContacts];
  int m = om::ui::nodeSearchFilter(&s, order, (int)n, "tie", shown);
  CHECK_EQ(m, 2);
  CHECK_EQ(shown[0], 5);
  CHECK_EQ(shown[1], 4);

  // "old" -> the favourite old one first, then the plain old one.
  m = om::ui::nodeSearchFilter(&s, order, (int)n, "old", shown);
  CHECK_EQ(m, 2);
  CHECK_EQ(shown[0], 1);
  CHECK_EQ(shown[1], 0);

  // Empty query keeps the whole order exactly.
  m = om::ui::nodeSearchFilter(&s, order, (int)n, "", shown);
  CHECK_EQ(m, 6);
  for (int i = 0; i < 6; ++i) CHECK_EQ(shown[i], order[i]);
}

// ------------------------------------------------------------------------------ 6. focus follows the node
void test_focus_follows() {
  printf("test  focus follows: after a toggle the focus lands on the node's NEW row, not the old number\n");
  om::mc::ContactStore s;
  addContact(s, 1, "NODE-old", om::ui::kNodeMenuAdvChat, 100, false);        // store 0
  addContact(s, 2, "NODE-fav-old", om::ui::kNodeMenuAdvChat, 200, true);     // store 1
  addContact(s, 3, "NODE-new", om::ui::kNodeMenuAdvChat, 300, false);        // store 2
  addContact(s, 4, "NODE-fav-new", om::ui::kNodeMenuAdvChat, 400, true);     // store 3

  // Before: favourites [3,1], then [2,0]; the focused row 3 is NODE-old (store 0).
  int shown[om::mc::kMaxContacts];
  int n = om::ui::nodeSearchFilter(&s, nullptr, 0, "", shown);
  CHECK_EQ(n, 0);   // a null order cannot match anybody: the caller must pass the page's order

  int order[om::mc::kMaxContacts];
  n = (int)om::mc::contactsOrder(s, order, om::mc::kMaxContacts);
  n = om::ui::nodeSearchFilter(&s, order, n, "", shown);
  CHECK_EQ(shown[3], 0);
  CHECK_EQ(om::ui::nodeMenuFocusFollow(shown, n, 0), 3);

  // Toggle store 0 on: it joins the favourites, and as the oldest favourite it is now row 2.
  CHECK(om::mc::contactSetFavourite(s, 0, true));
  n = (int)om::mc::contactsOrder(s, order, om::mc::kMaxContacts);
  n = om::ui::nodeSearchFilter(&s, order, n, "", shown);
  CHECK_EQ(om::ui::nodeMenuFocusFollow(shown, n, 0), 2);

  // And back off: it returns to the plain group, its last row (row 4 of the list, index 3).
  CHECK(om::mc::contactSetFavourite(s, 0, false));
  n = (int)om::mc::contactsOrder(s, order, om::mc::kMaxContacts);
  n = om::ui::nodeSearchFilter(&s, order, n, "", shown);
  CHECK_EQ(om::ui::nodeMenuFocusFollow(shown, n, 0), 3);

  // A row that is not in the shown list answers -1, as does a null list.
  CHECK_EQ(om::ui::nodeMenuFocusFollow(shown, n, 99), -1);
  CHECK_EQ(om::ui::nodeMenuFocusFollow(nullptr, n, 0), -1);
}

}  // namespace

int main() {
  test_rows_per_type();
  test_focus_and_action();
  test_notice();
  test_favourite_flag_and_blob();
  test_order_rule();
  test_focus_follows();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
