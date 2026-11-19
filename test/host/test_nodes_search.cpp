// SPDX-License-Identifier: MIT
// Host tests for the Nodes page's search rule (S514/EST-449). Real exit code: 0 = every check passed.
//
// These are the decisions the Nodes page runs on every key: what a typed query matches, the "N of M" the
// bar shows, how the focus moves between the bar and the list, and — the one that would cost a real
// transmission — that 'a' is the advert key ONLY while the list has focus. A wrong match is cheap; a wrong
// 'a' would put an advert on the mesh from a search field.
#include <stdio.h>
#include <string.h>

#include "mc_store.h"
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

om::mc::Contact makeContact(uint8_t seed, const char* name) {
  om::mc::Contact c;
  for (size_t i = 0; i < sizeof(c.pub); i++) c.pub[i] = (uint8_t)(seed + i * 7);
  snprintf(c.name, sizeof(c.name), "%s", name);
  c.type = 1;
  c.heard_at = 1000u + seed;
  return c;
}

// Six invented names (public-repo rule 9). Exactly TWO contain "era": TEST-Vera and RPT-Kamera.
om::mc::ContactStore makeStore6() {
  om::mc::ContactStore s;
  s.c[0] = makeContact(1, "TEST-Vera");
  s.c[1] = makeContact(40, "RPT-Jug");
  s.c[2] = makeContact(90, "RPT-Kamera");
  s.c[3] = makeContact(120, "RPT-Vzhod");
  s.c[4] = makeContact(160, "RPT-Zahod");
  s.c[5] = makeContact(200, "LOCAL-KB");
  s.count = 6;
  return s;
}

// ------------------------------------------------------------------------------ 1. what the query matches
void test_matcher() {
  printf("test  matcher: case-insensitive substring, empty query = all, UTF-8 as bytes\n");
  // Substring anywhere, not only the start.
  CHECK(om::ui::nodeSearchMatches("RPT-Kamera", "era"));
  CHECK(om::ui::nodeSearchMatches("TEST-Vera", "era"));
  CHECK(om::ui::nodeSearchMatches("RPT-Kamera", "KAM"));
  CHECK(om::ui::nodeSearchMatches("RPT-Kamera", "amera"));
  // Case is folded on BOTH sides (ASCII only, exactly the L2's rule).
  CHECK(om::ui::nodeSearchMatches("RPT-Kamera", "KAMERA"));
  CHECK(om::ui::nodeSearchMatches("rpt-kamera", "Era"));
  // A substring that is not there does not match.
  CHECK(!om::ui::nodeSearchMatches("RPT-Jug", "era"));
  CHECK(!om::ui::nodeSearchMatches("RPT-Jug", "Jugg"));
  CHECK(!om::ui::nodeSearchMatches("RPT-Jug", "jgu"));
  // Empty (and null) query = every node: "cleared" and "not searching" are the same picture.
  CHECK(om::ui::nodeSearchMatches("RPT-Jug", ""));
  CHECK(om::ui::nodeSearchMatches("RPT-Jug", nullptr));
  CHECK(om::ui::nodeSearchMatches("", ""));
  // A null name only matches when the query is empty (the caller should never pass one, but be honest).
  CHECK(!om::ui::nodeSearchMatches(nullptr, "era"));
  CHECK(om::ui::nodeSearchMatches(nullptr, ""));

  // UTF-8 names compare as their BYTES: an ASCII query matches, and the encoded letter matches itself.
  CHECK(om::ui::nodeSearchMatches("RPT-\xc5\xa1" "entvid", "ent"));           // Šentvid (š) / "ent"
  CHECK(om::ui::nodeSearchMatches("RPT-\xc5\xa1" "entvid", "\xc5\xa1"));      // lower-case š
  CHECK(om::ui::nodeSearchMatches("RPT-\xc5\xa1" "entvid", "RPT-"));
  CHECK(om::ui::nodeSearchMatches("RPT-\xc5\xa0" "entvid", "\xc5\xa0"));      // upper-case Š
  // ASCII-only fold, on purpose: Š (C5 A0) and š (C5 A1) are different bytes and do NOT fold.
  CHECK(!om::ui::nodeSearchMatches("RPT-\xc5\xa0" "entvid", "\xc5\xa1"));     // upper name, lower query
  CHECK(!om::ui::nodeSearchMatches("RPT-Sentvid", "\xc5\xa1"));               // plain s is not š
}

// ------------------------------------------------------------------------------ 2. the "N of M" text
void test_count_text() {
  printf("test  count text: \"2 of 6\", clamped to three digits, 0 of 0\n");
  char buf[32];
  om::ui::nodeSearchCountText(buf, sizeof(buf), 2, 6);
  CHECK_STR(buf, "2 of 6");
  om::ui::nodeSearchCountText(buf, sizeof(buf), 0, 0);
  CHECK_STR(buf, "0 of 0");
  om::ui::nodeSearchCountText(buf, sizeof(buf), 6, 6);
  CHECK_STR(buf, "6 of 6");
  om::ui::nodeSearchCountText(buf, sizeof(buf), -3, -7);
  CHECK_STR(buf, "0 of 0");
  om::ui::nodeSearchCountText(buf, sizeof(buf), 1234, 5000);
  CHECK_STR(buf, "999 of 999");
  // A tiny buffer still gets a NUL-terminated string, never an overrun.
  char tiny[5];
  om::ui::nodeSearchCountText(tiny, sizeof(tiny), 2, 6);
  CHECK_EQ(tiny[sizeof(tiny) - 1], 0);
}

// ------------------------------------------------------------------------------ 3. the filtered order
void test_filtered_order() {
  printf("test  filtered order: the Nodes order survives, empty query = all, 2 of 6 for \"era\"\n");
  const om::mc::ContactStore s = makeStore6();
  // The page's own order (favourites first, then newest heard) — deliberately NOT the contact order.
  const int order[6] = {2, 0, 1, 3, 4, 5};
  int       out[6]   = {0};

  int n = om::ui::nodeSearchFilter(&s, order, 6, "era", out);
  CHECK_EQ(n, 2);
  CHECK_EQ(out[0], 2);   // RPT-Kamera stays first: the filter keeps the page's order
  CHECK_EQ(out[1], 0);   // TEST-Vera second
  CHECK_STR(s.c[out[0]].name, "RPT-Kamera");
  CHECK_STR(s.c[out[1]].name, "TEST-Vera");

  // Case does not change the count.
  CHECK_EQ(om::ui::nodeSearchFilter(&s, order, 6, "ERA", out), 2);
  CHECK_EQ(om::ui::nodeSearchFilter(&s, order, 6, "RPT", out), 4);
  CHECK_EQ(om::ui::nodeSearchFilter(&s, order, 6, "zzz", out), 0);

  // Empty query keeps EVERY row, in order: the list is identical to "no search".
  n = om::ui::nodeSearchFilter(&s, order, 6, "", out);
  CHECK_EQ(n, 6);
  for (int i = 0; i < 6; i++) CHECK_EQ(out[i], order[i]);

  // Counting only (out = null) gives the same number the bar prints.
  CHECK_EQ(om::ui::nodeSearchFilter(&s, order, 6, "era", nullptr), 2);

  // An empty table reads 0 of 0.
  om::mc::ContactStore empty;
  empty.count = 0;
  CHECK_EQ(om::ui::nodeSearchFilter(&empty, order, 0, "", nullptr), 0);

  char count[32];
  const int era = om::ui::nodeSearchFilter(&s, order, 6, "era", nullptr);
  om::ui::nodeSearchCountText(count, sizeof(count), era, (int)s.count);
  CHECK_STR(count, "2 of 6");
}

// ------------------------------------------------------------------------------ 4. typing, UTF-8 safe
void test_typing() {
  printf("test  typing: one byte per event, Backspace drops the whole UTF-8 sequence\n");
  char   q[om::ui::kNodeQueryBytes] = {0};
  size_t len                        = 0;

  CHECK(om::ui::nodeSearchAppend(q, sizeof(q), &len, 'e'));
  CHECK(om::ui::nodeSearchAppend(q, sizeof(q), &len, 'r'));
  CHECK(om::ui::nodeSearchAppend(q, sizeof(q), &len, 'a'));
  CHECK_STR(q, "era");
  CHECK_EQ(len, 3);

  // "š" arrives as two key events (lead + continuation) and is deleted as ONE backspace.
  CHECK(om::ui::nodeSearchAppend(q, sizeof(q), &len, (char)0xC5));
  CHECK(om::ui::nodeSearchAppend(q, sizeof(q), &len, (char)0xA1));
  CHECK_EQ(len, 5);
  CHECK(om::ui::nodeSearchBackspace(q, &len));
  CHECK_EQ(len, 3);
  CHECK_STR(q, "era");

  CHECK(om::ui::nodeSearchBackspace(q, &len));
  CHECK_EQ(len, 2);
  CHECK_STR(q, "er");
  // Backspacing an empty field is a no-op, not a wrap-around.
  q[0] = 0;
  len  = 0;
  CHECK(!om::ui::nodeSearchBackspace(q, &len));
  CHECK_EQ(len, 0);

  // The buffer stops at cap-1 bytes and keeps its NUL; it never overruns.
  char full[4] = {0};
  size_t flen  = 0;
  CHECK(om::ui::nodeSearchAppend(full, sizeof(full), &flen, 'a'));
  CHECK(om::ui::nodeSearchAppend(full, sizeof(full), &flen, 'b'));
  CHECK(om::ui::nodeSearchAppend(full, sizeof(full), &flen, 'c'));
  CHECK(!om::ui::nodeSearchAppend(full, sizeof(full), &flen, 'd'));
  CHECK_STR(full, "abc");
  CHECK_EQ(flen, 3);
}

// ------------------------------------------------------------------------------ 5. focus moves
void test_focus_moves() {
  printf("test  focus: Up from the first row -> bar, Down from the bar -> first match, clamped at the end\n");
  bool bar = false;
  int  row = 0;

  // Up from the FIRST row leaves the list for the bar.
  CHECK(om::ui::nodesFocusUp(&bar, &row));
  CHECK(bar);
  CHECK_EQ(row, 0);
  // Up again, now in the bar: nothing happens (the bar is the top).
  CHECK(!om::ui::nodesFocusUp(&bar, &row));
  CHECK(bar);

  // Down from the bar lands on the FIRST MATCH.
  CHECK(om::ui::nodesFocusDown(&bar, &row, 2));
  CHECK(!bar);
  CHECK_EQ(row, 0);

  // Then down the filtered list, clamped at the last match (no wrap to the top).
  CHECK(om::ui::nodesFocusDown(&bar, &row, 2));
  CHECK_EQ(row, 1);
  CHECK(!om::ui::nodesFocusDown(&bar, &row, 2));
  CHECK_EQ(row, 1);

  // Up from row 1 is an ordinary move, not a jump to the bar.
  CHECK(om::ui::nodesFocusUp(&bar, &row));
  CHECK(!bar);
  CHECK_EQ(row, 0);

  // Nothing matches: Down keeps the focus in the bar (there is no row to land on).
  bar = true;
  row = 0;
  CHECK(!om::ui::nodesFocusDown(&bar, &row, 0));
  CHECK(bar);
  CHECK_EQ(row, 0);

  // An empty list with the list focused: Up still returns to the bar.
  bar = false;
  row = 0;
  CHECK(om::ui::nodesFocusUp(&bar, &row));
  CHECK(bar);
}

// ------------------------------------------------------------------------------ 6. Esc, twice
void test_esc_twice() {
  printf("test  Esc: clears a non-empty query first, the next Esc leaves the bar for the list\n");
  char   q[om::ui::kNodeQueryBytes] = "era";
  size_t len                        = 3;
  bool   bar                        = true;
  int    row                        = 1;

  // First Esc: the query goes, the focus STAYS in the bar.
  CHECK(om::ui::nodesFocusEsc(&bar, q, &len, &row) == om::ui::NodesEscResult::Cleared);
  CHECK(bar);
  CHECK_EQ(len, 0);
  CHECK_STR(q, "");
  CHECK_EQ(row, 0);

  // Second Esc: now empty, so it leaves the bar for the list.
  CHECK(om::ui::nodesFocusEsc(&bar, q, &len, &row) == om::ui::NodesEscResult::LeftBar);
  CHECK(!bar);
  CHECK_EQ(row, 0);

  // In the list, Esc is the page's back key: the caller keeps it.
  CHECK(om::ui::nodesFocusEsc(&bar, q, &len, &row) == om::ui::NodesEscResult::LeavePage);
}

// ------------------------------------------------------------------------------ 7. 'a' only on the list
void test_advert_only_on_list() {
  printf("test  'a': the advert key only while the LIST has focus; in the bar it is text\n");
  CHECK(om::ui::nodesAdvertAllowed(false));    // list focused: 'a' sends the advert
  CHECK(!om::ui::nodesAdvertAllowed(true));    // bar focused: 'a' is a typed letter
}

// ------------------------------------------------------------------------------ 8. the '/' shortcut
void test_slash_shortcut() {
  printf("test  '/': opens the search, and is not itself a character to append\n");
  CHECK(om::ui::nodesSlashOpensSearch('/'));
  CHECK(!om::ui::nodesSlashOpensSearch('a'));
  CHECK(!om::ui::nodesSlashOpensSearch(','));
}

}  // namespace

int main() {
  test_matcher();
  test_count_text();
  test_filtered_order();
  test_typing();
  test_focus_moves();
  test_esc_twice();
  test_advert_only_on_list();
  test_slash_shortcut();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
