// SPDX-License-Identifier: MIT
#include "chat_cards.h"

namespace om {
namespace mc {

CardGrid cardGrid(int x0, int y0, int x1, int y1, int gutter, int header_h, int card_h, int gap) {
    CardGrid g;
    g.x0 = x0;
    g.y0 = y0;
    g.x1 = x1;
    g.y1 = y1;
    if (gutter < 0) gutter = 0;
    if (header_h < 0) header_h = 0;
    if (card_h < 1) card_h = 1;
    if (gap < 0) gap = 0;
    g.gutter   = gutter;
    g.header_h = header_h;
    g.card_h   = card_h;
    g.gap      = gap;

    const int box_w = (x1 > x0) ? (x1 - x0) : 0;
    const int box_h = (y1 > y0) ? (y1 - y0) : 0;

    // Equal columns: any odd pixel of width stays in the gutter, so a diff of the two column widths is always 0.
    int col_w = (box_w - gutter * (kCardCols - 1)) / kCardCols;
    if (col_w < 1) col_w = 1;
    g.col_w     = col_w;
    g.col_x[0]  = x0;
    g.col_x[1]  = x0 + col_w + gutter;
    // The right edge of the box wins if rounding left a gap: the right column is flushed to x1 - col_w so the two
    // columns sit the same distance from their own edge of the box.
    if (g.col_x[kCardCols - 1] + col_w > x1 && x1 - col_w >= x0) g.col_x[kCardCols - 1] = x1 - col_w;

    g.cards_y = y0 + header_h;

    // How many cards fit under the header band. The `+ gap` lets the LAST card use the gap it would have had below
    // it, so a box that is exactly N cards tall shows N and not N-1.
    const int body_h = (box_h > header_h) ? (box_h - header_h) : 0;
    const int step   = card_h + gap;
    int       vis    = (body_h + gap) / step;
    if (vis < 1) vis = 1;   // a column always offers one card, so focus and scrolling stay defined
    g.visible = vis;
    return g;
}

CardRect cardRect(const CardGrid& g, int col, int row, int scroll) {
    CardRect r;
    if (col < 0 || col >= kCardCols) return r;   // w == 0: nothing to draw
    r.x = g.col_x[col];
    r.y = g.cards_y + (row - scroll) * (g.card_h + g.gap);
    r.w = g.col_w;
    r.h = g.card_h;
    return r;
}

namespace {

// The card this focus can actually point at, given the counts. The focused column wins; a column that lost its last
// card falls back to the first column that still has one. Returns false when no column has cards at all.
bool normalise(CardFocus& f, const int* counts, int ncols) {
    if (counts == nullptr || ncols <= 0) return false;
    if (f.col < 0 || f.col >= ncols) f.col = 0;
    if (counts[f.col] > 0) {
        if (f.row < 0) f.row = 0;
        if (f.row >= counts[f.col]) f.row = counts[f.col] - 1;
        return true;
    }
    for (int i = 0; i < ncols; i++) {
        if (counts[i] > 0) {
            f.col = i;
            f.row = 0;
            return true;
        }
    }
    return false;   // every column is empty: there is nothing to focus
}

}  // namespace

CardFocus cardMoveFocus(CardFocus from, CardMove move, const int* counts, int ncols) {
    CardFocus f = from;
    if (!normalise(f, counts, ncols)) return CardFocus{};   // all-empty grid: a defined, harmless focus

    switch (move) {
        case CardMove::Up:
            if (f.row > 0) --f.row;
            break;
        case CardMove::Down:
            if (f.row + 1 < counts[f.col]) ++f.row;
            break;
        case CardMove::Left:
        case CardMove::Right: {
            const int target = f.col + (move == CardMove::Left ? -1 : 1);
            // Out of range or EMPTY: the key does nothing, so an empty DM column never becomes focusable.
            if (target < 0 || target >= ncols || counts[target] <= 0) break;
            f.col = target;
            // Nearest row, clamped to the target column's last card.
            if (f.row >= counts[target]) f.row = counts[target] - 1;
            break;
        }
    }
    return f;
}

int cardScroll(const CardGrid& g, int count, int focused_row, int scroll) {
    const int vis = (g.visible > 0) ? g.visible : 1;
    int       max_scroll = count - vis;
    if (max_scroll < 0) max_scroll = 0;

    if (scroll < 0) scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;

    if (count <= 0) return 0;   // an empty column has nothing to scroll
    if (focused_row < 0) focused_row = 0;
    if (focused_row >= count) focused_row = count - 1;

    // The minimum move that puts the focused card fully inside the window: it first becomes visible at the top
    // (`scroll = row`) and last at the bottom (`scroll = row - vis + 1`).
    if (focused_row < scroll)
        scroll = focused_row;
    else if (focused_row >= scroll + vis)
        scroll = focused_row - vis + 1;

    if (scroll < 0) scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;
    return scroll;
}

}  // namespace mc
}  // namespace om
