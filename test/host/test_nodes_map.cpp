// SPDX-License-Identifier: MIT
// Host tests for the NODES and MAP settings rules (S514/EST-447). Real exit code: 0 = every check passed.
//
// These are the decisions the panel and the main task run. A wrong one is expensive in a quiet way: a
// removal that compacts wrongly takes out the neighbour of the contact the user picked, a rewrite that
// keeps the removed row brings it back on the next boot, and a last-position rule that writes per keypress
// would put a flash write behind every pan.
#include <stdio.h>
#include <string.h>

#include "contact_admin.h"
#include "contact_blob.h"
#include "map_settings.h"

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

#define CHECK_STR(got, want)                                                         \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (strcmp((got), (want)) != 0) {                                                \
      printf("FAIL %s:%d  \"%s\" == \"%s\"\n", __FILE__, __LINE__, (got), (want));  \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

om::mc::Contact makeContact(uint8_t seed, const char* name, uint8_t type) {
  om::mc::Contact c;
  for (size_t i = 0; i < sizeof(c.pub); i++) c.pub[i] = (uint8_t)(seed + i * 7);
  snprintf(c.name, sizeof(c.name), "%s", name);
  c.type     = type;
  c.heard_at = 1000u + seed;
  return c;
}

om::mc::ContactStore makeStore3() {
  om::mc::ContactStore s;
  s.c[0] = makeContact(1, "Alpha", 1);
  s.c[1] = makeContact(40, "Bravo", 2);
  s.c[2] = makeContact(90, "Charlie", 1);
  s.count = 3;
  return s;
}

// -------------------------------------------------------------------------------------- 1. remove + compact
void test_contact_remove_compacts() {
  printf("test  contact removal compacts and zeroes the vacated row\n");
  om::mc::ContactStore s = makeStore3();
  const uint8_t removed_pub[32] = {s.c[1].pub[0], s.c[1].pub[1], s.c[1].pub[2]};

  CHECK(om::mc::contactRemoveAt(s, 1));
  CHECK_EQ(s.count, 2);
  CHECK_STR(s.c[0].name, "Alpha");
  CHECK_STR(s.c[1].name, "Charlie");
  CHECK(om::mc::contactsFind(s, removed_pub) < 0);
  // The vacated last row is zeroed, not a stale copy of the removed contact.
  CHECK_STR(s.c[2].name, "");

  // An out-of-range index changes NOTHING: a stale row id must remove nobody.
  CHECK(!om::mc::contactRemoveAt(s, 2));
  CHECK(!om::mc::contactRemoveAt(s, 99));
  CHECK_EQ(s.count, 2);
  CHECK_STR(s.c[0].name, "Alpha");

  // First and last are ordinary targets, like the channel table's index 0.
  CHECK(om::mc::contactRemoveAt(s, 0));
  CHECK_EQ(s.count, 1);
  CHECK_STR(s.c[0].name, "Charlie");
  CHECK(om::mc::contactRemoveAt(s, 0));
  CHECK_EQ(s.count, 0);
  CHECK(!om::mc::contactRemoveAt(s, 0));
}

// --------------------------------------------------------------------- 2. the NVS blob after one removal
void test_contact_blob_round_trip_without_removed() {
  printf("test  the mcct blob round-trips WITHOUT the removed contact\n");
  om::mc::ContactStore s = makeStore3();
  uint8_t before[om::mc::kContactBlobMaxBytes];
  const size_t before_bytes = om::mc::contactsEncode(s, before, sizeof(before));
  CHECK_EQ(before_bytes, om::mc::kContactBlobHeaderBytes + 3 * om::mc::kContactBlobRecordBytes);

  CHECK(om::mc::contactRemoveAt(s, 1));   // Bravo
  uint8_t after[om::mc::kContactBlobMaxBytes];
  const size_t after_bytes = om::mc::contactsEncode(s, after, sizeof(after));
  CHECK_EQ(after_bytes, om::mc::kContactBlobHeaderBytes + 2 * om::mc::kContactBlobRecordBytes);

  om::mc::ContactStore back;
  CHECK(om::mc::contactsDecode(after, after_bytes, back) == om::mc::ContactDecode::Ok);
  CHECK_EQ(back.count, 2);
  CHECK_STR(back.c[0].name, "Alpha");
  CHECK_STR(back.c[1].name, "Charlie");

  // Bravo's full key is not in the rewritten table (the prefix alone is not enough).
  for (size_t i = 0; i < back.count; i++) {
    CHECK(memcmp(back.c[i].pub, s.c[0].pub, 32) == 0 || memcmp(back.c[i].pub, s.c[1].pub, 32) == 0);
  }
  const uint8_t bravo[32] = {40, 47, 54, 61};
  CHECK(om::mc::contactsFind(back, bravo) < 0);

  // The OLD blob still decodes to three: the removal is a rewrite, not a format change.
  om::mc::ContactStore old;
  CHECK(om::mc::contactsDecode(before, before_bytes, old) == om::mc::ContactDecode::Ok);
  CHECK_EQ(old.count, 3);
}

// -------------------------------------------------------------------------------------- 3. clear all
void test_contact_clear_all() {
  printf("test  clear-all writes an empty, loadable blob\n");
  om::mc::ContactStore s = makeStore3();
  om::mc::contactsClearAll(s);
  CHECK_EQ(s.count, 0);
  CHECK_STR(s.c[0].name, "");
  CHECK_STR(s.c[2].name, "");
  om::mc::contactsClearAll(s);   // already empty: still a no-op
  CHECK_EQ(s.count, 0);

  uint8_t blob[om::mc::kContactBlobMaxBytes];
  const size_t bytes = om::mc::contactsEncode(s, blob, sizeof(blob));
  CHECK_EQ(bytes, om::mc::kContactBlobHeaderBytes);
  CHECK_EQ(blob[1], 0);   // the header's count

  om::mc::ContactStore back;
  back.c[0] = makeContact(7, "stale", 1);   // decode must clear it
  back.count = 1;
  CHECK(om::mc::contactsDecode(blob, bytes, back) == om::mc::ContactDecode::Ok);
  CHECK_EQ(back.count, 0);
}

// ------------------------------------------------------------------------------ 4. the new-node notice
void test_new_node_notice_default() {
  printf("test  new-node notice: absent/corrupt = On, only 0 = Off\n");
  CHECK(om::mc::newNodeNoticeFromStored(false, 0));    // never stored: the default On
  CHECK(om::mc::newNodeNoticeFromStored(false, 3));    // absent ignores the value
  CHECK(om::mc::newNodeNoticeFromStored(true, 1));
  CHECK(!om::mc::newNodeNoticeFromStored(true, 0));    // the only Off
  CHECK(om::mc::newNodeNoticeFromStored(true, 7));     // corrupt -> On, never silence
  CHECK_STR(om::mc::contactTypeLabel(1), "client");
  CHECK_STR(om::mc::contactTypeLabel(2), "repeater");
  CHECK_STR(om::mc::contactTypeLabel(3), "room");
  CHECK_STR(om::mc::contactTypeLabel(4), "sensor");
  CHECK_STR(om::mc::contactTypeLabel(0), "node");
  CHECK_STR(om::mc::contactTypeLabel(200), "node");
}

// ----------------------------------------------------------------------------------- 5. start position
void test_map_start_position_choice() {
  printf("test  map start position: Ljubljana unless a stored 1, and Enter flips it\n");
  CHECK_EQ(om::map::startPositionFromStored(0, true), om::map::kStartLjubljana);
  CHECK_EQ(om::map::startPositionFromStored(1, true), om::map::kStartLast);
  CHECK_EQ(om::map::startPositionFromStored(0, false), om::map::kStartLjubljana);
  CHECK_EQ(om::map::startPositionFromStored(1, false), om::map::kStartLjubljana);   // absent
  CHECK_EQ(om::map::startPositionFromStored(9, true), om::map::kStartLjubljana);    // corrupt

  CHECK_STR(om::map::startPositionLabel(om::map::kStartLjubljana), "Ljubljana");
  CHECK_STR(om::map::startPositionLabel(om::map::kStartLast), "Last position");
  CHECK_STR(om::map::startPositionLongLabel(om::map::kStartLjubljana), "Ljubljana (default)");

  CHECK_EQ(om::map::startPositionCycle(om::map::kStartLjubljana), om::map::kStartLast);
  CHECK_EQ(om::map::startPositionCycle(om::map::kStartLast), om::map::kStartLjubljana);
}

// --------------------------------------------------------------------------------------- 6. start zoom
void test_map_start_zoom_range() {
  printf("test  map start zoom: 12..17, default 16, 17 cycles back to 12\n");
  CHECK_EQ(om::map::startZoomFromStored(0, false), 16);
  CHECK_EQ(om::map::startZoomFromStored(11, true), 16);   // below the range is the DEFAULT, not 12
  CHECK_EQ(om::map::startZoomFromStored(18, true), 16);
  CHECK_EQ(om::map::startZoomFromStored(255, true), 16);
  CHECK_EQ(om::map::startZoomFromStored(12, true), 12);
  CHECK_EQ(om::map::startZoomFromStored(15, true), 15);
  CHECK_EQ(om::map::startZoomFromStored(17, true), 17);

  CHECK_EQ(om::map::startZoomCycle(12), 13);
  CHECK_EQ(om::map::startZoomCycle(16), 17);
  CHECK_EQ(om::map::startZoomCycle(17), 12);
  CHECK_EQ(om::map::startZoomCycle(11), 16);   // a bad value is normalised first
  CHECK_EQ(om::map::startZoomCycle(20), 16);
}

// ------------------------------------------------------------------- 7. the last-position write rule
void test_map_last_position_write_rule() {
  printf("test  last position: on leaving, and at most once a minute, never per keypress\n");
  // No change, no write, whatever else is true.
  CHECK(!om::map::lastPositionWriteDue(false, false, 1000, 0));
  CHECK(!om::map::lastPositionWriteDue(true, false, 1000, 0));

  // The first move of a boot (nothing written yet) goes at once.
  CHECK(om::map::lastPositionWriteDue(false, true, 0, 0));
  CHECK(om::map::lastPositionWriteDue(false, true, 1000, 0));

  // Then one write a minute: a pan one second later must NOT write.
  CHECK(!om::map::lastPositionWriteDue(false, true, 1001, 1000));
  CHECK(!om::map::lastPositionWriteDue(false, true, 2000, 1000));
  CHECK(!om::map::lastPositionWriteDue(false, true, 60999, 1000));
  CHECK(om::map::lastPositionWriteDue(false, true, 61000, 1000));   // exactly 60 s

  // Leaving ignores the floor, so "where it was" survives the page being closed a second after a pan.
  CHECK(om::map::lastPositionWriteDue(true, true, 1001, 1000));
  CHECK(om::map::lastPositionWriteDue(true, true, 61000, 1000));

  // A 32-bit millisecond clock wraps after ~49.7 days; the subtraction must measure the REAL gap across the
  // wrap rather than a huge negative number.
  CHECK(om::map::lastPositionWriteDue(false, true, 0x00000050u, 0xFFFFFF00u) == false);  // only 336 ms
  CHECK(om::map::lastPositionWriteDue(false, true, 0x0000FFFFu, 0xFFFF0000u));          // ~131 s: due
  CHECK(om::map::lastPositionWriteDue(false, true, 0x00000001u, 0xFFFFFFFFu) == false);  // 2 ms: not yet
}

// ------------------------------------------------------------------ 8. the saved view at a start zoom
void test_map_saved_view_conversion() {
  printf("test  a saved view keeps its CENTRE when the start zoom differs\n");
  om::map::SavedView v;
  v.valid = true;
  v.wx    = 1000;
  v.wy    = 2000;
  v.z     = 16;

  int64_t wx = -1, wy = -1;
  CHECK(om::map::startViewFromSaved(v, 16, 800, 430, &wx, &wy));
  CHECK_EQ(wx, 1000);
  CHECK_EQ(wy, 2000);

  CHECK(om::map::startViewFromSaved(v, 17, 800, 430, &wx, &wy));
  CHECK_EQ(wx, 2400);   // centre 1400 -> 2800, minus half a screen
  CHECK_EQ(wy, 4215);   // centre 2000+215 -> 4430, minus 215 (S516/EST-482: area_h = 430)

  CHECK(om::map::startViewFromSaved(v, 15, 800, 430, &wx, &wy));
  CHECK_EQ(wx, 300);
  CHECK_EQ(wy, 892);   // centre 2215 >> 1 = 1107, minus 215

  // Anything invalid answers false, so the caller can fall back to Ljubljana instead of drawing somewhere
  // arbitrary.
  om::map::SavedView bad = v;
  bad.valid             = false;
  CHECK(!om::map::startViewFromSaved(bad, 16, 800, 430, &wx, &wy));
  bad         = v;
  bad.z       = 8;
  CHECK(!om::map::startViewFromSaved(bad, 16, 800, 430, &wx, &wy));
  bad.z       = 18;
  CHECK(!om::map::startViewFromSaved(bad, 16, 800, 430, &wx, &wy));
  CHECK(!om::map::startViewFromSaved(v, 16, 0, 386, &wx, &wy));
  CHECK(!om::map::startViewFromSaved(v, 16, 800, 430, nullptr, &wy));
  CHECK(!om::map::startViewFromSaved(v, 16, 800, 430, &wx, nullptr));
}

}  // namespace

int main() {
  test_contact_remove_compacts();
  test_contact_blob_round_trip_without_removed();
  test_contact_clear_all();
  test_new_node_notice_default();
  test_map_start_position_choice();
  test_map_start_zoom_range();
  test_map_last_position_write_rule();
  test_map_saved_view_conversion();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
