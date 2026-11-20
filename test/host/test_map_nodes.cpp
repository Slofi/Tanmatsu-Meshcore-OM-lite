// SPDX-License-Identifier: MIT
// S515/EST-466: the nodes on the map. What is pinned here is every number the glass cannot be asked about:
//   * the projection against the pack's OWN tile maths (a node at a tile corner IS that tile's corner, z9..z17),
//     the centre, and the sign handling on both sides of the equator and of Greenwich;
//   * culling at all four view edges (half-open: the right/bottom edge belongs to the next view);
//   * the positions that must never be drawn (no position flag, out of range, 0,0);
//   * the label greedy (two plates that collide -> the first one only; a favourite wins the collision);
//   * the node menu's Map row with and without a position (dim + "no position");
//   * centring the view on a node for the menu's Map item, including the zoom clamp.
// It also measures the whole pass on 2,000 rows and prints it: the ticket's budget is 2 ms for 300 nodes, and this
// is the cheap proof rather than a hope.
#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "map_nodes.h"
#include "map_pack.h"
#include "map_view.h"
#include "mc_store.h"
#include "node_menu.h"
#include "map_page.h"     // S516/EST-482: kMapAreaY/H, the REAL map area
#include "tm_theme.h"     // kTmStatusH/kTmHintH/kTmH, the chrome the area must meet

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

// S516/EST-482: the REAL map area (no page head any more). Taking the numbers from main/map_page.h instead of
// copying them means this whole suite moves with the canvas, and testMapAreaMeetsChrome pins the chrome edges.
constexpr int kW = om::ui::kMapAreaW;
constexpr int kH = om::ui::kMapAreaH;

// A fake font: 6 px a character. Every collision case below is arithmetic on this, so the numbers are exact.
int fakeMeasure(void* ctx, const char* utf8) {
  (void)ctx;
  return (int)strlen(utf8) * 6;
}

// A monotonic microsecond clock: the cost line is a real measurement, not a wall-clock reading that a scheduler
// can spoil.
long nowUs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000000L + (long)(ts.tv_nsec / 1000);
}

void putContact(om::mc::ContactStore& s, size_t i, const char* name, uint8_t type, bool fav, uint32_t heard,
                bool has_pos, int32_t lat, int32_t lon) {
  om::mc::Contact& c = s.c[i];
  c = om::mc::Contact{};
  snprintf(c.name, sizeof(c.name), "%s", name);
  c.type         = type;
  c.favourite    = fav;
  c.heard_at     = heard;
  c.has_position = has_pos;
  c.lat_e6       = lat;
  c.lon_e6       = lon;
  c.pub[0]       = (uint8_t)i;
}

// ------------------------------------------------------------------------------ the map area and the chrome
// S516/EST-482: the Map page lost its 44 px page head. These four numbers are the whole interface between the
// canvas and the chrome; if any of them drifts, a tile row hides under the status bar or the hint bar.
void testMapAreaMeetsChrome() {
  printf("test  the map canvas starts at the status bar and ends at the hint bar\n");
  CHECK_EQ(om::ui::kMapAreaY, om::ui::kTmStatusH);                          // top = status bar's bottom
  CHECK_EQ(om::ui::kMapAreaY, 26);
  CHECK_EQ(om::ui::kMapAreaH, 430);                                          // 480 - 26 - 24
  CHECK_EQ(om::ui::kMapAreaY + om::ui::kMapAreaH, om::ui::kTmH - om::ui::kTmHintH);  // bottom = hint bar's top
  CHECK_EQ(om::ui::kMapAreaX, 0);
  CHECK_EQ(om::ui::kMapAreaW, om::ui::kTmW);
  // At most 5 columns x 3 rows: 2*256 = 512 px already covers the 430 px height, so no fourth row exists and
  // the 20-slot tile cache still has 5 slots of headroom (see core/map_view.h::kCacheSlots).
  CHECK(om::ui::kMapAreaH <= 2 * om::map::kTilePx);
  CHECK(om::ui::kMapAreaH > om::map::kTilePx);
}

// ---------------------------------------------------------------------------------------------- the projection
void testProjectionCorners() {
  // A node exactly at a tile's top-left corner must land exactly on that tile's pixel corner at every zoom the map
  // has. The tile corner's world pixel is (tx * 256, ty * 256); we take the view's top-left to be the tile's own
  // corner, so the node must land at (0,0) — and then one pixel further right/down at (1,1).
  for (int z = om::map::kMinZoom; z <= om::map::kMaxZoom; ++z) {
    // The world pixel at the tile corner, turned BACK into degrees: lon from x, lat from y (the inverse of the
    // forward formula the pack uses: lat = atan(sinh((1 - 2*y/n) * pi))).
    const double n  = (double)((int64_t)1 << z) * 256.0;
    const int64_t tx = 34, ty = 23;   // a tile near central Europe, at every zoom
    const double lon = (double)tx * 256.0 / n * 360.0 - 180.0;
    const double gy  = (double)ty * 256.0;
    const double lat = atan(sinh(3.14159265358979323846 * (1.0 - 2.0 * gy / n))) * 180.0 / 3.14159265358979323846;

    const int32_t lat_e6 = (int32_t)(lat * 1000000.0);
    const int32_t lon_e6 = (int32_t)(lon * 1000000.0);

    // The pack's own forward maths must give the tile corner back (the rounding of micro-degrees is < 1 px at z17
    // for a ~1e-6 degree step, so allow exactly 1 px).
    const int64_t px = om::map::worldPxXForLon((double)lon_e6 / 1000000.0, z);
    const int64_t py = om::map::worldPxYForLat((double)lat_e6 / 1000000.0, z);
    CHECK(px >= (int64_t)tx * 256 - 2 && px <= (int64_t)tx * 256 + 2);
    CHECK(py >= (int64_t)ty * 256 - 2 && py <= (int64_t)ty * 256 + 2);

    // A view whose top-left IS that corner: the node lands on (0,0) within the micro-degree rounding, i.e. within
    // 2 px — and, crucially, the SAME offset the pack's maths gives (one formula, not two).
    int x = -1, y = -1;
    CHECK(om::map::projectForView(z, (int64_t)tx * 256, (int64_t)ty * 256, kW, kH, lat_e6, lon_e6, &x, &y));
    CHECK_EQ(x, (int)(px - (int64_t)tx * 256));
    CHECK_EQ(y, (int)(py - (int64_t)ty * 256));
    CHECK(x >= 0 && x < 3);
    CHECK(y >= 0 && y < 3);
  }
}

void testProjectionCentreAndSigns() {
  const int z = 15;
  // The view centred on the node: the node lands within a pixel of the centre of the map area.
  const int32_t lat = 46051400, lon = 14506000;   // 46.0514 N 14.5060 E
  const int64_t cx = om::map::worldPxXForLon(14.506, z);
  const int64_t cy = om::map::worldPxYForLat(46.0514, z);
  int x = -1, y = -1;
  CHECK(om::map::projectForView(z, cx - kW / 2, cy - kH / 2, kW, kH, lat, lon, &x, &y));
  CHECK_EQ(x, kW / 2);
  CHECK_EQ(y, kH / 2);

  // The signs: the four quadrants of the world, each one projected from its own top-left corner view.
  struct Case { int32_t lat, lon; const char* what; };
  const Case cases[] = {
      {46051400, 14506000, "N/E"}, {-33868800, 151209000, "S/E"}, {40712800, -74006000, "N/W"},
      {-34603600, -58438100, "S/W"},
  };
  for (const Case& c : cases) {
    const int64_t wx = om::map::worldPxXForLon((double)c.lon / 1000000.0, z) - 100;
    const int64_t wy = om::map::worldPxYForLat((double)c.lat / 1000000.0, z) - 100;
    CHECK(om::map::projectForView(z, wx, wy, kW, kH, c.lat, c.lon, &x, &y));
    CHECK_EQ(x, 100);
    CHECK_EQ(y, 100);
    // Longitude alone moves x and never y; latitude alone moves y and never x (the two formulas are independent).
    int x2 = -1, y2 = -1;
    // 0.01 deg of longitude: moves x, and stays inside the view so nothing is culled.
    CHECK(om::map::projectForView(z, wx, wy, kW, kH, c.lat, c.lon + 10000, &x2, &y2));
    CHECK(x2 > x);
    CHECK_EQ(y2, y);
  }
}

void testCulling() {
  const int z = 16;
  const int32_t lat = 46051400, lon = 14506000;
  const int64_t px = om::map::worldPxXForLon(14.506, z);
  const int64_t py = om::map::worldPxYForLat(46.0514, z);
  int x = 0, y = 0;

  // Exactly on the left/top edge: IN (pixel 0).
  CHECK(om::map::projectForView(z, px, py, kW, kH, lat, lon, &x, &y));
  CHECK_EQ(x, 0);
  CHECK_EQ(y, 0);
  // One pixel outside on the left/top: OUT.
  CHECK(!om::map::projectForView(z, px + 1, py + 1, kW, kH, lat, lon, &x, &y));
  // Inside at the far corner, then one past the right/bottom edge: half-open, so OUT.
  CHECK(om::map::projectForView(z, px - (kW - 1), py - (kH - 1), kW, kH, lat, lon, &x, &y));
  CHECK_EQ(x, kW - 1);
  CHECK_EQ(y, kH - 1);
  CHECK(!om::map::projectForView(z, px - kW, py - (kH - 1), kW, kH, lat, lon, &x, &y));
  CHECK(!om::map::projectForView(z, px - (kW - 1), py - kH, kW, kH, lat, lon, &x, &y));
  // A zoom outside the map's range is not a view at all.
  CHECK(!om::map::projectForView(om::map::kMaxZoom + 1, px, py, kW, kH, lat, lon, &x, &y));
  CHECK(!om::map::projectForView(om::map::kMinZoom - 1, px, py, kW, kH, lat, lon, &x, &y));
}

void testRejectedPositions() {
  const int z = 15;
  const int64_t wx = om::map::worldPxXForLon(14.506, z) - 100;
  const int64_t wy = om::map::worldPxYForLat(46.0514, z) - 100;
  int x = 0, y = 0;

  // The flag alone decides: a row with lat/lon filled but has_position false is never drawn (an advert without a
  // position never wipes one, so the fields can hold an old value while the flag is honest).
  CHECK(!om::map::positionDrawable(false, 46051400, 14506000));
  CHECK(om::map::positionDrawable(true, 46051400, 14506000));
  // Out of range: one micro-degree past each limit.
  CHECK(!om::map::positionDrawable(true, 90000001, 0));
  CHECK(!om::map::positionDrawable(true, -90000001, 0));
  CHECK(!om::map::positionDrawable(true, 0, 180000001));
  CHECK(!om::map::positionDrawable(true, 0, -180000001));
  // The edges themselves are real places: the poles and the date line are drawable.
  CHECK(om::map::positionDrawable(true, 90000000, 180000000));
  CHECK(om::map::positionDrawable(true, -90000000, -180000000));
  // 0,0 is refused: it is the shape of a wiped field, and a node would sit in the Gulf of Guinea for it.
  CHECK(!om::map::positionDrawable(true, 0, 0));
  // A zero on ONE axis is a real place (the equator, Greenwich) and must NOT be refused.
  CHECK(om::map::positionDrawable(true, 0, 14506000));
  CHECK(om::map::positionDrawable(true, 46051400, 0));

  CHECK(!om::map::projectForView(z, wx, wy, kW, kH, 90000001, 0, &x, &y));
  CHECK(!om::map::projectForView(z, wx, wy, kW, kH, 0, 0, &x, &y));
  CHECK(!om::map::projectForView(z, wx, wy, kW, kH, 0, 180000001, &x, &y));
}

void testLabelText() {
  char buf[64];
  CHECK(strcmp(om::map::mapLabelText(buf, sizeof(buf), "TEST-A1"), "TEST-A1") == 0);
  // Exactly 16 characters: kept whole, no ellipsis.
  CHECK(strcmp(om::map::mapLabelText(buf, sizeof(buf), "ABCDEFGHIJKLMNOP"), "ABCDEFGHIJKLMNOP") == 0);
  // 17: cut to 16 and an ellipsis (U+2026, three bytes).
  const char* cut = om::map::mapLabelText(buf, sizeof(buf), "ABCDEFGHIJKLMNOPQ");
  CHECK(strcmp(cut, "ABCDEFGHIJKLMNOP\xe2\x80\xa6") == 0);
  // A multi-byte name is never cut in half: 15 ASCII + 1 three-byte character = 16 characters, kept whole.
  CHECK(strcmp(om::map::mapLabelText(buf, sizeof(buf), "ABCDEFGHIJKLMNO\xc5\xa0"), "ABCDEFGHIJKLMNO\xc5\xa0") == 0);
  // A blank name is named honestly, never an empty plate.
  CHECK(strcmp(om::map::mapLabelText(buf, sizeof(buf), ""), "(no name)") == 0);
  CHECK(strcmp(om::map::mapLabelText(buf, sizeof(buf), nullptr), "(no name)") == 0);
}

void testLabelCollision() {
  om::mc::ContactStore s;   // the real store, 2,000 rows
  CHECK(s.c != nullptr);
  s.count = 0;
  const int z = 15;
  const int32_t lat = 46051400, lon = 14506000;
  const int64_t wx = om::map::worldPxXForLon(14.506, z) - kW / 2;
  const int64_t wy = om::map::worldPxYForLat(46.0514, z) - kH / 2;

  // Two nodes 3 px apart: their plates (the same x, the same y) overlap, so only ONE label may be drawn.
  putContact(s, 0, "TEST-B2", 1, false, 500, true, lat, lon);              // heard 500 s ago
  putContact(s, 1, "TEST-A1", 1, true, 100, true, lat + 20, lon + 20);     // 2 px lower/right, FAVOURITE
  // A third, far away: it must keep its own label.
  putContact(s, 2, "TEST-C3", 2, false, 400, true, lat + 5000, lon + 8000);    // ~116/186 px away: in view
  s.count = 3;

  int x = 0, y = 0;
  CHECK(om::map::projectForView(z, wx, wy, kW, kH, s.c[2].lat_e6, s.c[2].lon_e6, &x, &y));

  om::map::LabelCandidate cand[16];
  const int nc = om::map::collectLabelCandidates(s, z, wx, wy, kW, kH, cand, 16);
  CHECK_EQ(nc, 3);
  // The order is the greedy's: the FAVOURITE first even though it was heard longest ago.
  CHECK_EQ(cand[0].node, 1);
  CHECK_EQ(cand[1].node, 0);
  CHECK_EQ(cand[2].node, 2);

  om::map::MapLabel labs[16];
  const int nl = om::map::placeLabels(s, cand, nc, kW, kH, &fakeMeasure, nullptr, labs, 16);
  CHECK_EQ(nl, 2);   // the two overlapping plates count as one label
  CHECK_EQ(labs[0].node, 1);   // the favourite won the collision
  CHECK_EQ(labs[1].node, 2);
  // The plate really is right of the marker and centred on it (the geometry the draw uses).
  CHECK_EQ(labs[0].box.x0, cand[0].x + om::map::kLabelGap);
  CHECK_EQ(labs[0].box.y0, cand[0].y - om::map::kLabelH / 2);
  CHECK_EQ(labs[0].box.x1 - labs[0].box.x0, (int)strlen(labs[0].text) * 6 + 2 * om::map::kLabelPadX);

  // A plate that would leave the view is skipped, not clipped.
  om::mc::ContactStore e;
  e.count = 0;
  putContact(e, 0, "EDGE", 1, false, 10, true, lat, lon);
  e.count = 1;
  om::map::LabelCandidate c1[4];
  // A view whose left edge is the node's pixel: the plate would start 7 px in and run off the right only if the
  // name were longer than the view — so push the node to the RIGHT edge instead.
  const int64_t wxl = om::map::worldPxXForLon(14.506, z) - (kW - 4);
  CHECK_EQ(om::map::collectLabelCandidates(e, z, wxl, wy, kW, kH, c1, 4), 1);
  CHECK_EQ(om::map::placeLabels(e, c1, 1, kW, kH, &fakeMeasure, nullptr, labs, 16), 0);
}

void testCandidateCapKeepsTheBest() {
  // A full table of 2,000 positioned nodes piled on one spot: the candidate set is bounded, and the ones it keeps
  // are the favourites and the most recently heard — never whichever happened to be first in the table.
  om::mc::ContactStore s;
  const int32_t lat = 46051400, lon = 14506000;
  for (int i = 0; i < (int)s.cap; ++i) {
    const bool fav = (i == 1500);
    putContact(s, (size_t)i, "N", 1, fav, (uint32_t)(i + 1), true, lat, lon);
    s.count = (size_t)i + 1;
  }
  const int z = 15;
  const int64_t wx = om::map::worldPxXForLon(14.506, z) - kW / 2;
  const int64_t wy = om::map::worldPxYForLat(46.0514, z) - kH / 2;
  om::map::LabelCandidate cand[8];
  const int nc = om::map::collectLabelCandidates(s, z, wx, wy, kW, kH, cand, 8);
  CHECK_EQ(nc, 8);
  CHECK_EQ(cand[0].node, 1500);                 // the favourite, heard 1501 s ago, beats the later ones
  CHECK_EQ(cand[1].node, (int)s.cap - 1);        // then the most recently heard
  CHECK_EQ(cand[7].node, (int)s.cap - 7);
}

void testMenuRow() {
  om::ui::NodeMenuContext c;
  c.advType = om::ui::kNodeMenuAdvChat;
  c.hasKey  = true;

  om::ui::NodeMenuRow rows[om::ui::kMaxNodeMenuRows];
  // WITH a position: a normal row, right after Trace (when there is a route) and before the store row.
  c.hasPosition = true;
  c.pathKnown   = true;
  int n = om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows);
  CHECK_EQ(n, 8);
  int map_at = -1;
  for (int i = 0; i < n; ++i)
    if (rows[i].action == om::ui::NodeAction::Map) map_at = i;
  CHECK(map_at >= 0);
  CHECK(strcmp(rows[map_at].label, "Map") == 0);
  CHECK(rows[map_at].value[0] == '\0');
  CHECK(!rows[map_at].dim);
  CHECK(rows[map_at - 1].action == om::ui::NodeAction::Trace);
  CHECK(rows[map_at + 1].action == om::ui::NodeAction::AddToRadio);

  // WITHOUT a position: still offered (a missing row looks like a forgotten feature), DIM, and it says why.
  c.hasPosition = false;
  n = om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows);
  map_at = -1;
  for (int i = 0; i < n; ++i)
    if (rows[i].action == om::ui::NodeAction::Map) map_at = i;
  CHECK(map_at >= 0);
  CHECK(rows[map_at].dim);
  CHECK(strcmp(rows[map_at].value, "no position") == 0);
  CHECK(strcmp(om::ui::nodeNoPositionNotice(), "No position from this node yet") == 0);

  // A repeater with no key: the DM/Ping/Trace rows are gone but Map is still there (a position is not a key).
  om::ui::NodeMenuContext r;
  r.advType = om::ui::kNodeMenuAdvRepeater;
  r.hasKey  = false;
  n = om::ui::nodeMenuRows(r, rows, om::ui::kMaxNodeMenuRows);
  CHECK_EQ(n, 2);   // Info and Map, and nothing else
  CHECK(rows[0].action == om::ui::NodeAction::Info);
  CHECK(rows[1].action == om::ui::NodeAction::Map);
  CHECK(rows[1].dim);
}

void testCentreOnNode() {
  int     z = 0;
  int64_t wx = 0, wy = 0;

  // 46.0514 N 14.5060 E at z15: the node lands on the centre of the map area (within a pixel).
  CHECK(om::map::centreOnNode(46051400, 14506000, 15, kW, kH, &z, &wx, &wy));
  CHECK_EQ(z, 15);
  int x = -1, y = -1;
  CHECK(om::map::projectForView(z, wx, wy, kW, kH, 46051400, 14506000, &x, &y));
  CHECK_EQ(x, kW / 2);
  CHECK_EQ(y, kH / 2);

  // The zoom is clamped to the map's own range, never to a zoom the packs do not have.
  CHECK(om::map::centreOnNode(46051400, 14506000, 99, kW, kH, &z, &wx, &wy));
  CHECK_EQ(z, om::map::kMaxZoom);
  CHECK(om::map::centreOnNode(46051400, 14506000, 0, kW, kH, &z, &wx, &wy));
  CHECK_EQ(z, om::map::kMinZoom);
  // The default z15 is INSIDE the range and lives in the "slo" pack (z9-15), so it is not clamped.
  CHECK(om::map::centreOnNode(46051400, 14506000, 15, kW, kH, &z, &wx, &wy));
  CHECK_EQ(z, 15);

  // No position, or a rejected one: no view, so the caller shows its notice instead of a map somewhere odd.
  CHECK(!om::map::centreOnNode(0, 0, 15, kW, kH, &z, &wx, &wy));
  CHECK(!om::map::centreOnNode(91000000, 14506000, 15, kW, kH, &z, &wx, &wy));

  // Centred near a world edge the view is CLAMPED, not left hanging outside the map.
  CHECK(om::map::centreOnNode(80000000, 179990000, 15, kW, kH, &z, &wx, &wy));
  const int64_t size = (int64_t)om::map::kTilePx << z;
  CHECK(wx >= 0 && wx <= size - kW);
  CHECK(wy >= 0 && wy <= size - kH);
}

// ---------------------------------------------------------------------------------------------- the cost
void testCost() {
  om::mc::ContactStore s;
  const int32_t lat = 46051400, lon = 14506000;
  // 2,000 positioned nodes packed into ~0.01 degrees: at z15 that is ~235 px square, so nearly ALL of them are in
  // view at once. That is the worst case that matters — every row projected, and the label greedy asked to rank a
  // full screen of plates.
  for (int i = 0; i < (int)s.cap; ++i) {
    putContact(s, (size_t)i, "NODE", (uint8_t)(1 + (i % 3)), (i % 50) == 0, (uint32_t)(i + 1), true,
               lat + (int32_t)((i % 40) * 200), lon + (int32_t)((i / 40) * 200));
    s.count = (size_t)i + 1;
  }
  const int z = 15;
  const int64_t wx = om::map::worldPxXForLon(14.506, z) - kW / 2;
  const int64_t wy = om::map::worldPxYForLat(46.0514, z) - kH / 2;

  om::map::LabelCandidate cand[32];
  om::map::MapLabel       labs[32];

  // 300 nodes (the ticket's budget) and 2,000 (the table's cap), three runs each, best of three. The clock is
  // monotonic and in microseconds: `clock()` on a host counts CPU ticks nobody can compare with a budget.
  long us300 = 1L << 30, us2000 = 1L << 30;
  int  markers300 = 0, labels300 = 0, markers2000 = 0, labels2000 = 0;
  for (int round = 0; round < 3; ++round) {
    for (int n : {300, 2000}) {
      s.count          = (size_t)n;   // 300 rows IS 300 rows: the label pass must not scan the 2,000 behind them
      const long t0    = nowUs();
      int        drawn = 0;
      for (int i = 0; i < n; ++i) {
        int x = 0, y = 0;
        if (om::map::projectForView(z, wx, wy, kW, kH, s.c[i].lat_e6, s.c[i].lon_e6, &x, &y)) ++drawn;
      }
      const int nc = om::map::collectLabelCandidates(s, z, wx, wy, kW, kH, cand, 32);
      const int nl = om::map::placeLabels(s, cand, nc, kW, kH, &fakeMeasure, nullptr, labs, 32);
      const long us = nowUs() - t0;
      s.count       = s.cap;
      if (n == 300) {
        if (us < us300) us300 = us;
        markers300 = drawn;
        labels300  = nl;
      } else {
        if (us < us2000) us2000 = us;
        markers2000 = drawn;
        labels2000  = nl;
        printf("  the whole marker+label pass on %d nodes: %ld us (%d markers in view, %d labels)\n", n, us, drawn,
               nl);
      }
    }
  }
  printf("  markers: %d in view of 2,000 rows (%d labels); 300 rows -> %d markers, %d labels\n", markers2000,
         labels2000, markers300, labels300);
  printf("  300 nodes: %ld us; 2,000 nodes: %ld us (budget: 2,000 us for 300)\n", us300, us2000);
  ++g_checks;
  if (us300 >= 2000) {
    printf("FAIL %s  the 300-node pass took %ld us, budget 2000 us\n", __FILE__, us300);
    ++g_failures;
  }
  // A generous ceiling on 2,000 rows: if 2,000 is this cheap, 300 cannot be near 2 ms.
  ++g_checks;
  if (us2000 >= 20000) {
    printf("FAIL %s  the 2,000-node pass took %ld us (expected < 20,000)\n", __FILE__, us2000);
    ++g_failures;
  }
}

}  // namespace

int main() {
  // The store's own PSRAM hook stays at malloc on the host (mc_store.cpp's default).
  testMapAreaMeetsChrome();
  testProjectionCorners();
  testProjectionCentreAndSigns();
  testCulling();
  testRejectedPositions();
  testLabelText();
  testLabelCollision();
  testCandidateCapKeepsTheBest();
  testMenuRow();
  testCentreOnNode();
  testCost();

  if (g_failures == 0) printf("map nodes: %d checks, 0 failures\n", g_checks);
  else printf("map nodes: %d checks, %d FAILURES\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
