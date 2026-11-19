// SPDX-License-Identifier: MIT
// S515/EST-466: the nodes ON the map — where a stored position lands on the screen, which of them the view can
// see, and which names fit around them.
//
// Filip, S515: "No nodes on the map?" The positions were already stored (EST-461 filled `has_position` / lat_e6 /
// lon_e6 from the verified advert, EST-463 gave them one table for the radio and the SD archive), so nothing here
// asks the radio or the card for anything: this is arithmetic over the same table the Nodes page reads.
//
// The projection is the pack's OWN slippy maths (core/map_pack.h::worldPxXForLon / worldPxYForLat), not a second
// copy of it: a node at a tile corner must land on that tile's pixel corner at every zoom, and two implementations
// of the Web-Mercator formula are exactly how that stops being true. The host test pins it against the tile maths.
//
// Nothing here touches a screen: the page asks for pixels, draws discs and plates, and the tests pin the numbers.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"

namespace om {
namespace map {

// ---------------------------------------------------------------------------------------------- projection
// A position we can draw. The advert carries micro-degrees, so the ranges are exact and cheap to check:
//   lat -90..+90, lon -180..+180, and 0,0 is refused — it is what a wiped or missing field looks like, and it
//   would put a node in the Gulf of Guinea for every node that ever lost its position.
// (MeshCore's own encoder cannot produce (0,0) on purpose: a node there would have to be at that exact spot.)
inline bool positionDrawable(bool has_position, int32_t lat_e6, int32_t lon_e6) {
    if (!has_position) return false;
    if (lat_e6 < -90000000 || lat_e6 > 90000000) return false;
    if (lon_e6 < -180000000 || lon_e6 > 180000000) return false;
    if (lat_e6 == 0 && lon_e6 == 0) return false;
    return true;
}

// The screen pixel (VIEW-LOCAL: 0..view_w-1, 0..view_h-1; the page adds the map area's origin) of a position in the
// view whose top-left world pixel is (wx, wy) at zoom z. False when the point is outside the view or not drawable —
// which is the culling rule too: a marker is drawn only when its POINT is in the view, so a disc never appears
// apparently from nowhere at the edge.
bool projectForView(int z, int64_t wx, int64_t wy, int view_w, int view_h, int32_t lat_e6, int32_t lon_e6,
                    int* out_x, int* out_y);

// ---------------------------------------------------------------------------------------------- markers
// What the page needs per marker: which store row it is, and where it goes. The draw itself is the page's (it owns
// the colours and the Screen); this only says WHICH discs exist and WHERE.
struct MapMarker {
    int node = -1;   // the contact table row
    int x    = 0;    // view-local pixel
    int y    = 0;
};

// ---------------------------------------------------------------------------------------------- labels
// Names from this zoom up (the ticket's "from zoom 13 up").
constexpr int kLabelMinZoom = 13;

// ~16 characters: long enough for "SI-LJ-Trnovo II" at a readable size, short enough that three labels cannot
// cover a quarter of the screen. UTF-8 safe (a multi-byte character is never cut in half).
constexpr int kLabelMaxChars = 16;

// The plate: 7 px right of the marker's centre, 15 px tall and centred on it, 4 px of air each side of the text.
// All of it is here so the collision test and the draw cannot disagree about one rectangle.
constexpr int kLabelGap   = 7;
constexpr int kLabelPadX  = 4;
constexpr int kLabelH     = 15;

struct LabelRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // half-open: [x0,x1) x [y0,y1)

    bool overlaps(const LabelRect& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
};

// One label the page may draw: the row, the text ALREADY clipped to kLabelMaxChars, and its plate.
struct MapLabel {
    int       node = -1;
    char      text[4 * kLabelMaxChars + 4] = {0};
    LabelRect box;
};

// A text width in pixels, from whatever face the page draws with (the host test passes a fake one). A null
// callback measures 0, so every label "fits" — the caller's bug, never a crash.
using LabelMeasure = int (*)(void* ctx, const char* utf8);

// The label text: the name clipped to kLabelMaxChars characters, with an ellipsis when it was cut. A blank name
// becomes "(no name)", the same words the Nodes list uses, because an empty plate is worse than an honest one.
const char* mapLabelText(char* out, size_t cap, const char* name);

// The nodes that may get a label, in the order the greedy takes them: FAVOURITES FIRST, then most recently heard,
// then the lower row index (a stable tie-break, so the same view always labels the same rows).
struct LabelCandidate {
    int node = -1;
    int x    = 0;
    int y    = 0;
};

// The best `cap` candidates in the view, best first. Bounded on purpose: a screen shows a handful of names, and
// 2,000 rows must not cost 2,000 slots of anything. Streams the table once, keeping the array sorted.
int collectLabelCandidates(const mc::ContactStore& s, int z, int64_t wx, int64_t wy, int view_w, int view_h,
                           LabelCandidate* out, int cap);

// Greedy placement over those candidates: each one's plate is offered, and the first plate that overlaps one
// already placed is SKIPPED (the marker is still drawn — only the name goes). A plate that would leave the view
// is skipped too, because a name clipped by the page edge reads as a rendering fault.
// Returns how many labels were written.
int placeLabels(const mc::ContactStore& s, const LabelCandidate* cand, int cand_count, int view_w, int view_h,
                LabelMeasure measure, void* ctx, MapLabel* out, int cap);

// ---------------------------------------------------------------------------------------------- the menu's Map
// The view the node menu's "Map" item opens: centred on the node at `zoom`, clamped to the zooms the map itself
// has (core/map_view.h kMinZoom..kMaxZoom = 9..17; z15 lives in the "slo" pack, z9-15). False when the node has
// no drawable position, so the caller shows its notice instead of a map somewhere odd.
bool centreOnNode(int32_t lat_e6, int32_t lon_e6, int zoom, int view_w, int view_h, int* out_z, int64_t* out_wx,
                  int64_t* out_wy);

}  // namespace map
}  // namespace om
