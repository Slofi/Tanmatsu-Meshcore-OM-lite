// SPDX-License-Identifier: MIT
// The Chat page's list state as CARDS in two columns (S513/EST-430, Filip: "make channels like rectangles in two
// columns, so first column is channels, the second is DMs").
//
// Everything here is arithmetic on the box the page hands in: no theme, no font, no Screen. The page owns the
// pixels; this file owns where the rectangles are, where the focus goes, and how far a column scrolls. That split
// is why the rules can be host-tested with a 800x480 box and no device.
//
// The two columns are the model the page draws. Column 0 is Channels and always exists; column 1 is Direct messages
// and is EMPTY today (this app has no TXT_MSG receive or send yet), which is exactly the case the "an empty column
// cannot take focus" rule is for.
#pragma once

#include <stdint.h>

namespace om {
namespace mc {

// The columns, in drawing order and in Left/Right order. kCardCols is the array size the caller must give to the
// counts[] parameters below.
constexpr int kCardCols    = 2;
constexpr int kColChannels = 0;
constexpr int kColDms      = 1;

struct CardRect {
    int x = 0, y = 0, w = 0, h = 0;   // w == 0 means "nothing to draw here"
};

// The grid of the two columns inside the page's work box: x0/y0 inclusive, x1/y1 exclusive, already inside the
// page margins (the caller adds kTmPad; this file knows no theme).
struct CardGrid {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the box it was built from (kept for the clamping tests)
    int col_x[kCardCols] = {0, 0};        // left edge of each column
    int col_w   = 0;                      // the SAME width for both columns (ticket: equal columns)
    int gutter  = 0;                      // pixels between the two columns
    int header_h = 0;                     // the label band at the top of each column
    int card_h  = 0;
    int gap     = 0;                      // vertical space between two cards in a column
    int cards_y = 0;                      // top of the first card at scroll == 0
    int visible = 0;                      // cards that fit in a column at once (>= 1)
};

// Build the grid. `gutter`, `header_h`, `card_h` and `gap` are the page's numbers; an odd leftover pixel stays in
// the gutter so the two columns are exactly equal. `visible` is at least 1 (a column always offers one card, even
// in a box too short to be sensible, so focus and scrolling stay defined).
CardGrid cardGrid(int x0, int y0, int x1, int y1, int gutter, int header_h, int card_h, int gap);

// The rect of card `row` of `col` with `scroll` rows hidden above it. w == 0 for a column out of range or a row
// outside 0..count-1 is the CALLER's check (this function has no count); it returns a rect for any row, and the
// page draws only the rows the grid says are visible.
CardRect cardRect(const CardGrid& g, int col, int row, int scroll);

// ---- focus -----------------------------------------------------------------------------------------------
// Where the focus is: a column and a row inside it. `row` is only meaningful for a column that has cards.
struct CardFocus {
    int col = kColChannels;
    int row = 0;
};

enum class CardMove { Up, Down, Left, Right };

// The focus after one key press, given how many cards each column has (`counts` has `ncols` entries).
//   · Up/Down move inside the column and STOP at its ends (no wrap — the rest of the app clamps too);
//   · Left/Right switch to the neighbouring column and land on the card at the NEAREST ROW, clamped to that
//     column's last card;
//   · a column with no cards cannot take focus: while DMs are empty, Right changes nothing;
//   · if the column the focus was in becomes empty (a channel list that shrank), the focus moves to the first
//     non-empty column at row 0, and an all-empty grid returns {kColChannels, 0}.
CardFocus cardMoveFocus(CardFocus from, CardMove move, const int* counts, int ncols);

// The scroll a column needs so the focused card is FULLY visible, moving as little as possible: `scroll` in,
// `scroll` out. Clamped to 0..max(0, count - visible), so a short column has nowhere to scroll and the page never
// scrolls away from content that does not exist.
int cardScroll(const CardGrid& g, int count, int focused_row, int scroll);

}  // namespace mc
}  // namespace om
