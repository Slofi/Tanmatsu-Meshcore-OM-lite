// SPDX-License-Identifier: MIT
// Tanmatsu page metrics: the ONE table for the 800x480 panel, device side.
//
// This is the file `tools/preview_tanmatsu.py` promised: its TM table moved here, and the preview
// tool now PARSES this header (see TM_OF_HEADER in that file) instead of keeping its own copy. Colours
// stay in the L2's `firmware/src/ui/theme.h` (parsed by both) — one palette, one metrics table.
//
// Every number is either the approved mock's `.dev-tan` rule or an L2 constant scaled for 800 px; the
// comment says which. Values are int16_t so a diff of this table against the preview's numbers is exact.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// --------------------------------------------------------------------------------------------- panel
// Native panel is 480x800 PORTRAIT; the app draws 800x480 landscape and blits rotated (see screen.cpp).
constexpr int16_t kTmW           = 800;
constexpr int16_t kTmH           = 480;
constexpr int16_t kPanelW        = 480;   // physical, portrait (stride of a panel buffer)
constexpr int16_t kPanelH        = 800;   // physical, portrait
constexpr int16_t kBlitBandH     = 40;    // portrait rows per bsp_display_blit band (proven by WadaMesh)

// --------------------------------------------------------------------------------------------- chrome
constexpr int16_t kTmStatusH     = 26;    // mock .dev-tan .status{height:26px} (L2 kStatusH is 26 at 320x240)
constexpr int16_t kTmHintH       = 24;    // mock .hints 20 px + 4 so 11 px chips sit inside it
constexpr int16_t kTmPad         = 18;    // mock .row padding 8/12 + .detail 16/18 -> 18
constexpr int16_t kTmHeadH       = 44;    // page title band under the status bar (L2 kHeadH 23 scaled)

// --------------------------------------------------------------------------------------------- lists
constexpr int16_t kTmListW       = 320;   // mock .pane .list{width:280px} + the L2's 30 px rows scaled
constexpr int16_t kTmRowH        = 64;    // L2 kRowH 30 -> 64 (2.1x, the same "expanded" step as the type)
constexpr int16_t kTmRingW       = 2;     // the focus ring: BRIEF § 5, gold. 2 px at 800
constexpr int16_t kTmRingInset   = 14;    // band/ring inset from the pane's right edge (L2 kSelPadX x2)
constexpr int16_t kTmScrollW     = 6;     // scroll track width, inside that inset (L2 kScrollTrackW x1.5)
constexpr int16_t kTmScrollMin   = 24;    // L2 kScrollMinThumbH 8 -> 24

// --------------------------------------------------------------------------------------------- types
// L2 at 320x240: SansBody 11 / SansTitle 14 / SansTab 9 / MonoMeta 10.
// mock .dev-tan: body 13 / h2 18 / kv 12 / meta 10 / status 11 / hints 9.
constexpr int16_t kTmFTitle      = 22;
constexpr int16_t kTmFRow        = 17;
constexpr int16_t kTmFBody       = 13;
constexpr int16_t kTmFMeta       = 12;
constexpr int16_t kTmFLabel      = 11;
constexpr int16_t kTmFStatus     = 12;
constexpr int16_t kTmFHint       = 11;
constexpr int16_t kTmFKv         = 12;

static_assert(kTmW == kPanelH && kTmH == kPanelW,
              "landscape 800x480 must be the portrait 480x800 rotated (screen.cpp assumes it)");
static_assert(kTmStatusH + kTmHeadH + kTmRowH < kTmH, "chrome + one row must fit the panel");

}  // namespace ui
}  // namespace om
