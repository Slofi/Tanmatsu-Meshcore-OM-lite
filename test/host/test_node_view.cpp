// SPDX-License-Identifier: MIT
// S515/EST-465 host tests: the Nodes page's ALL / RADIO / ARCHIVE view, the node menu's store rows, the
// RadioFull refusal's words and the focus re-find after a delete, and the Settings › OM Lite › NODES values.
//
// The interesting failures here are all silent on the glass: a view that re-orders the list, a count that
// disagrees with the rows, a menu that offers "Add to radio" on a node already in the radio, or a delete that
// leaves the selection one row off.
#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include "mc_store.h"
#include "node_menu.h"
#include "node_table.h"
#include "node_view.h"
#include "nodes_fresh.h"
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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,       \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

void keyFor(uint8_t out[32], uint32_t seed) {
  memset(out, 0, 32);
  for (int i = 0; i < 4; i++) out[i] = (uint8_t)((seed >> (8 * i)) & 0xFF);
  out[4]   = (uint8_t)(seed * 7 + 3);
  out[31]  = 0xA5;
}

int addNode(om::mc::ContactStore& s, uint32_t seed, const char* name, uint8_t type, uint32_t heard,
            bool favourite = false) {
  uint8_t pub[32];
  keyFor(pub, seed);
  const int i = om::mc::advertInArchive(s, om::mc::HeardToRadio::Off, pub, name, type, 0, heard, heard, 0, 0);
  if (i >= 0 && favourite) s.c[i].favourite = true;
  return i;
}

// The five-row fixture every view test uses. Store rows: 0 radio, 1 archive, 2 radio, 3 archive+favourite,
// 4 radio. The page's own (frozen) order is [3, 0, 4, 2, 1]: favourite first, then the rest by last heard.
struct Fixture {
  om::mc::ContactStore s;
  int                  frozen[5] = {3, 0, 4, 2, 1};
};

void buildFixture(Fixture& f) {
  addNode(f.s, 1, "RADIO-A", 1, 100);
  addNode(f.s, 2, "ARCH-B", 1, 90);
  addNode(f.s, 3, "RADIO-C", 1, 80);
  addNode(f.s, 4, "ARCH-D", 1, 70, true);
  addNode(f.s, 5, "RADIO-E", 1, 60);
  CHECK_EQ(f.s.count, 5);
  for (int i : {0, 2, 4}) CHECK_EQ(om::mc::addToRadio(f.s, f.s.c[i].pub), om::mc::RadioResult::Ok);
  CHECK_EQ(om::mc::radioCount(f.s), 3);
}

// ---- 1. the view: counts, order, and the frozen order kept per view ------------------------------
void testViewCountsAndOrder() {
  Fixture f;
  buildFixture(f);
  const size_t radio = om::mc::radioCount(f.s);

  CHECK_EQ(om::ui::nodeViewCount(f.s, om::ui::NodeView::All, radio), 5);
  CHECK_EQ(om::ui::nodeViewCount(f.s, om::ui::NodeView::Radio, radio), 3);
  CHECK_EQ(om::ui::nodeViewCount(f.s, om::ui::NodeView::Archive, radio), 2);

  int out[8];
  CHECK_EQ(om::ui::nodeViewFilter(f.s, f.frozen, 5, om::ui::NodeView::All, out), 5);
  for (int i = 0; i < 5; i++) CHECK_EQ(out[i], f.frozen[i]);
  CHECK_EQ(om::ui::nodeViewFilter(f.s, f.frozen, 5, om::ui::NodeView::Radio, out), 3);
  CHECK_EQ(out[0], 0);
  CHECK_EQ(out[1], 4);
  CHECK_EQ(out[2], 2);
  CHECK_EQ(om::ui::nodeViewFilter(f.s, f.frozen, 5, om::ui::NodeView::Archive, out), 2);
  CHECK_EQ(out[0], 3);
  CHECK_EQ(out[1], 1);

  // The frozen order is the SAME array in every view: changing the filter never re-sorts (a row keeps its
  // place relative to the others), and a stale row id is skipped rather than dereferenced.
  int stale[3] = {3, 99, 0};
  CHECK_EQ(om::ui::nodeViewFilter(f.s, stale, 3, om::ui::NodeView::Radio, out), 1);
  CHECK_EQ(out[0], 0);
  CHECK_EQ(om::ui::nodeViewFilter(f.s, nullptr, 3, om::ui::NodeView::All, out), 0);

  // Search on top of the view: "arch" inside the Radio view matches nothing; inside All it keeps the two.
  int shown[8];
  const int radio_rows = om::ui::nodeViewFilter(f.s, f.frozen, 5, om::ui::NodeView::Radio, out);
  CHECK_EQ(om::ui::nodeSearchFilter(&f.s, out, radio_rows, "arch", shown), 0);
  const int all_rows = om::ui::nodeViewFilter(f.s, f.frozen, 5, om::ui::NodeView::All, out);
  CHECK_EQ(om::ui::nodeSearchFilter(&f.s, out, all_rows, "arch", shown), 2);
  CHECK_EQ(shown[0], 3);
  CHECK_EQ(shown[1], 1);

  // The chips: All -> Radio -> Archive, clamped at both ends (three choices, no wrap).
  CHECK(om::ui::nodeViewCycle(om::ui::NodeView::All, 1) == om::ui::NodeView::Radio);
  CHECK(om::ui::nodeViewCycle(om::ui::NodeView::Radio, 1) == om::ui::NodeView::Archive);
  CHECK(om::ui::nodeViewCycle(om::ui::NodeView::Archive, 1) == om::ui::NodeView::Archive);
  CHECK(om::ui::nodeViewCycle(om::ui::NodeView::All, -1) == om::ui::NodeView::All);
  CHECK(om::ui::nodeViewCycle(om::ui::NodeView::Archive, -1) == om::ui::NodeView::Radio);
  CHECK(strcmp(om::ui::nodeViewText(om::ui::NodeView::All), "All") == 0);
  CHECK(strcmp(om::ui::nodeViewText(om::ui::NodeView::Radio), "Radio") == 0);
  CHECK(strcmp(om::ui::nodeViewText(om::ui::NodeView::Archive), "Archive") == 0);
  CHECK(om::ui::kNodeViewDefault == om::ui::NodeView::All);
}

// ---- 2. the menu's rows per state ----------------------------------------------------------------
void testMenuRowsPerState() {
  om::ui::NodeMenuRow rows[om::ui::kMaxNodeMenuRows];
  CHECK_EQ(om::ui::kMaxNodeMenuRows, 7);

  // A client in the ARCHIVE with a known route: DM, Info, Ping, Trace, Add to radio, Favourite, Delete.
  om::ui::NodeMenuContext c;
  c.advType = 1;
  c.hasKey = true;
  c.pathKnown = true;
  c.in_radio = false;
  c.favourite = true;
  int n = om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows);
  CHECK_EQ(n, 7);
  CHECK(rows[4].action == om::ui::NodeAction::AddToRadio);
  CHECK(strcmp(rows[4].label, "Add to radio") == 0);
  CHECK(rows[5].action == om::ui::NodeAction::Favourite);
  CHECK(strcmp(rows[5].value, "on") == 0);
  CHECK(rows[5].valueAccent);
  CHECK(rows[6].action == om::ui::NodeAction::DeleteEverywhere);
  CHECK(strcmp(rows[6].label, "Delete everywhere") == 0);
  for (int i = 0; i < n; i++) CHECK(rows[i].action != om::ui::NodeAction::MoveToArchive);

  // The same node in the RADIO: the store row flips, nothing else appears twice.
  c.in_radio = true;
  n = om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows);
  CHECK_EQ(n, 7);
  CHECK(rows[4].action == om::ui::NodeAction::MoveToArchive);
  CHECK(strcmp(rows[4].label, "Move to archive") == 0);
  for (int i = 0; i < n; i++) CHECK(rows[i].action != om::ui::NodeAction::AddToRadio);

  // A repeater without a route: no DM, no Trace, but the store row, favourite and delete are there.
  om::ui::NodeMenuContext r;
  r.advType = 2;
  r.hasKey = true;
  r.in_radio = false;
  n = om::ui::nodeMenuRows(r, rows, om::ui::kMaxNodeMenuRows);
  CHECK_EQ(n, 5);   // Info, Ping, Add to radio, Favourite, Delete
  CHECK(rows[0].action == om::ui::NodeAction::Info);
  CHECK(rows[2].action == om::ui::NodeAction::AddToRadio);
  CHECK(rows[4].action == om::ui::NodeAction::DeleteEverywhere);

  // A prefix-only row has no key: only Info, as before (no DM/Ping/Favourite/delete on a maybe-node).
  om::ui::NodeMenuContext p;
  p.advType = 1;
  p.hasKey = false;
  p.in_radio = false;
  n = om::ui::nodeMenuRows(p, rows, om::ui::kMaxNodeMenuRows);
  CHECK_EQ(n, 1);
  CHECK(rows[0].action == om::ui::NodeAction::Info);

  // Enter runs the row under the focus, and a focus outside the list runs nothing.
  CHECK(om::ui::nodeMenuActionAt(rows, n, 0) == om::ui::NodeAction::Info);
  CHECK(om::ui::nodeMenuActionAt(rows, n, 9) == om::ui::NodeAction::None);
}

// ---- 3. RadioFull refuses with the ticket's words and changes NOTHING -----------------------------
void testRadioFullPath() {
  om::mc::ContactStore s;
  for (size_t i = 0; i < om::mc::kMaxRadio; i++)
    CHECK(addNode(s, (uint32_t)(i + 1), "r", 1, (uint32_t)(1000 + i)) >= 0);
  for (size_t i = 0; i < om::mc::kMaxRadio; i++)
    CHECK_EQ(om::mc::addToRadio(s, s.c[i].pub), om::mc::RadioResult::Ok);
  CHECK_EQ(om::mc::radioCount(s), om::mc::kMaxRadio);

  const int extra = addNode(s, 90000, "NEW", 1, 5000);
  CHECK(extra >= 0);
  uint8_t pub[32];
  memcpy(pub, s.c[extra].pub, 32);
  const size_t count_before = s.count;
  const bool   fresh_before = s.c[extra].fresh;
  const uint32_t heard_before = s.c[extra].heard_at;
  const int    pos_before   = om::mc::contactsFind(s, pub);

  CHECK(om::mc::addToRadio(s, pub) == om::mc::RadioResult::RadioFull);
  CHECK_EQ(s.count, count_before);
  CHECK_EQ(om::mc::radioCount(s), om::mc::kMaxRadio);
  CHECK_EQ(om::mc::contactsFind(s, pub), pos_before);
  CHECK(!s.c[extra].in_radio);
  CHECK_EQ(s.c[extra].fresh, fresh_before);
  CHECK_EQ(s.c[extra].heard_at, heard_before);

  char text[80];
  om::ui::nodeRadioFullNotice(text, sizeof(text));
  CHECK(strcmp(text, "Radio full (300 / 300): move a node to the archive first") == 0);
  CHECK(strcmp(om::ui::nodeAddedToRadioNotice(), "Added to the radio") == 0);
  CHECK(strcmp(om::ui::nodeMovedToArchiveNotice(), "Moved to the archive") == 0);
}

// ---- 4. the confirm sentence, and the focus re-find after a delete --------------------------------
void testDeleteConfirmAndFocus() {
  char text[200];
  om::ui::nodeDeleteConfirmText(text, sizeof(text), "TEST-B2");
  CHECK(strcmp(text, "Delete TEST-B2 from the radio and the archive? Its DM history stays on the card.") == 0);
  om::ui::nodeDeleteConfirmText(text, sizeof(text), "");
  CHECK(strcmp(text, "Delete this node from the radio and the archive? Its DM history stays on the card.") == 0);

  // The sentence is TRUE: deleteEverywhere removes the row and nothing else. The DM ring is a separate store
  // (mc_store's DmRing), which this test does not touch; here we prove the row removal + the shift.
  Fixture f;
  buildFixture(f);
  int shown[8];
  const int n = om::ui::nodeViewFilter(f.s, f.frozen, 5, om::ui::NodeView::All, shown);
  CHECK_EQ(n, 5);
  // The box is on the row shown at position 1 (store row 0). Delete store row 0: rows after it shift down.
  uint8_t gone[32];
  memcpy(gone, f.s.c[0].pub, 32);
  CHECK(om::mc::deleteEverywhere(f.s, gone));
  CHECK_EQ(f.s.count, 4);

  // Rebuild the shown order the way the page does (frozen sync drops the gone row), then re-find BY KEY.
  om::ui::NodesFrozen frozen;
  om::ui::nodesFrozenBegin(frozen, f.frozen, 5);
  CHECK(om::ui::nodesFrozenSync(frozen, f.s));
  const int n2 = om::ui::nodeViewFilter(f.s, frozen.order, frozen.count, om::ui::NodeView::All, shown);
  const int new_index = om::mc::contactsFind(f.s, gone);   // -1: it is gone
  CHECK_EQ(new_index, -1);
  CHECK_EQ(om::ui::nodeMenuFocusAfterRemove(shown, n2, new_index, 1), 1);
  CHECK_EQ(om::ui::nodeMenuFocusAfterRemove(shown, n2, new_index, 4), n2 - 1);   // clamped
  CHECK_EQ(om::ui::nodeMenuFocusAfterRemove(shown, 0, new_index, 3), 0);

  // A key that is STILL there is re-found at its new position (rows shifted above it).
  uint8_t moved[32];
  memcpy(moved, f.s.c[2].pub, 32);   // store row 2 was RADIO-E
  const int idx = om::mc::contactsFind(f.s, moved);
  CHECK(idx >= 0);
  const int pos = om::ui::nodeMenuFocusFollow(shown, n2, idx);
  CHECK(pos >= 0);
  CHECK_EQ(om::ui::nodeMenuFocusAfterRemove(shown, n2, idx, 0), pos);
}

// ---- 5. Settings › OM Lite › NODES: the row values ----------------------------------------------
void testSettingsValues() {
  using om::mc::HeardToRadio;
  CHECK(strcmp(om::ui::nodeHeardToRadioValue(HeardToRadio::Off), "Off") == 0);
  CHECK(strcmp(om::ui::nodeHeardToRadioValue(HeardToRadio::Clients), "Clients") == 0);
  CHECK(strcmp(om::ui::nodeHeardToRadioValue(HeardToRadio::All), "All") == 0);
  CHECK(om::ui::nodeHeardToRadioCycle(HeardToRadio::Off) == HeardToRadio::Clients);
  CHECK(om::ui::nodeHeardToRadioCycle(HeardToRadio::Clients) == HeardToRadio::All);
  CHECK(om::ui::nodeHeardToRadioCycle(HeardToRadio::All) == HeardToRadio::Off);

  char info[96];
  om::ui::nodeStoreInfoText(info, sizeof(info), 10, 412, true);
  CHECK(strcmp(info, "Radio 10 / 300 \xc2\xb7 Archive 412 \xc2\xb7 SD ok") == 0);
  om::ui::nodeStoreInfoText(info, sizeof(info), 0, 0, false);
  CHECK(strcmp(info, "Radio 0 / 300 \xc2\xb7 Archive 0 \xc2\xb7 no SD card") == 0);

  char head[64];
  om::ui::nodeStoreCountsText(head, sizeof(head), 10, 412);
  CHECK(strcmp(head, "Radio 10 / 300 \xc2\xb7 Archive 412") == 0);
  om::ui::nodeStoreCountsText(head, sizeof(head), om::mc::kMaxRadio, 2000);
  CHECK(strcmp(head, "Radio 300 / 300 \xc2\xb7 Archive 2000") == 0);

  // The Settings list is the RADIO list: of the five fixture rows only the three in the radio are offered.
  Fixture f;
  buildFixture(f);
  int order[8];
  const int total = (int)om::mc::contactsOrder(f.s, order, om::mc::kMaxNodes);
  int radio_rows = 0;
  for (int i = 0; i < total; i++)
    if (f.s.c[order[i]].in_radio) radio_rows++;
  CHECK_EQ(total, 5);
  CHECK_EQ(radio_rows, 3);
}

}  // namespace

int main() {
  testViewCountsAndOrder();
  testMenuRowsPerState();
  testRadioFullPath();
  testDeleteConfirmAndFocus();
  testSettingsValues();
  printf("test_node_view: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
