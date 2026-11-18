// SPDX-License-Identifier: MIT
// Host tests for the Chat page's two-column card grid (S513/EST-430): where the cards are, where Left/Right/Up/Down
// take the focus, and how far a column scrolls. This is the code the page draws with, so a wrong rule here is a
// card drawn half outside the panel or a DM column that can be focused while it is empty.
//
// The box is the real one at 800x480 (kTmPad 18, head line at 70, hint bar at 456), so the numbers below are the
// panel's, not a made-up box: 374 px columns, a 16 px gutter, 68 px cards, four per column.
#include <stdio.h>
#include <string.h>

#include "chat_cards.h"

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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,         \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

// The page's own numbers (chat_page.h): 800x480, kTmPad 18, head line at kTmStatusH + kTmHeadH = 70, hint bar 24.
constexpr int kBoxX0 = 18, kBoxY0 = 70, kBoxX1 = 782, kBoxY1 = 456;
constexpr int kGutter = 16, kHeaderH = 30, kCardH = 68, kGap = 10;

om::mc::CardGrid pageGrid() {
    return om::mc::cardGrid(kBoxX0, kBoxY0, kBoxX1, kBoxY1, kGutter, kHeaderH, kCardH, kGap);
}

// 1. Two columns, the same width, a gutter between them, inside the page margins.
void test_two_equal_columns() {
    const om::mc::CardGrid g = pageGrid();
    CHECK_EQ(g.col_w, 374);                 // (782-18-16)/2
    CHECK_EQ(g.col_x[om::mc::kColChannels], kBoxX0);
    CHECK_EQ(g.col_x[om::mc::kColDms], kBoxX0 + 374 + kGutter);
    CHECK_EQ(g.col_x[om::mc::kColDms] - (g.col_x[om::mc::kColChannels] + g.col_w), kGutter);
    // Both columns the same width: the right column ends where the box does (the ticket's "same width").
    CHECK_EQ(g.col_x[om::mc::kColDms] + g.col_w, kBoxX1);
    CHECK_EQ(g.header_h, kHeaderH);
    CHECK_EQ(g.cards_y, kBoxY0 + kHeaderH);   // the cards start under the label band
    CHECK_EQ(g.card_h, kCardH);
    CHECK_EQ(g.gap, kGap);

    // An odd box width must not make the columns differ: the extra pixel stays in the gutter.
    const om::mc::CardGrid odd = om::mc::cardGrid(18, 70, 781, 456, kGutter, kHeaderH, kCardH, kGap);
    CHECK_EQ(odd.col_w, 373);                 // (763-16)/2 = 373
    CHECK(odd.col_x[om::mc::kColDms] + odd.col_w <= 781);
    CHECK(odd.col_x[om::mc::kColDms] - (odd.col_x[om::mc::kColChannels] + odd.col_w) >= kGutter);
}

// 2. How many cards fit: zero room still offers one, exactly one fits, and the page's box holds four.
void test_visible_count() {
    // A box with no room below the header: still one, so focus and scrolling stay defined.
    om::mc::CardGrid g = om::mc::cardGrid(0, 0, 400, kHeaderH, kGutter, kHeaderH, kCardH, kGap);
    CHECK_EQ(g.visible, 1);

    // Exactly one card under the header (no gap needed below the last card).
    g = om::mc::cardGrid(0, 0, 400, kHeaderH + kCardH, kGutter, kHeaderH, kCardH, kGap);
    CHECK_EQ(g.visible, 1);

    // One card + gap + one card.
    g = om::mc::cardGrid(0, 0, 400, kHeaderH + 2 * kCardH + kGap, kGutter, kHeaderH, kCardH, kGap);
    CHECK_EQ(g.visible, 2);

    // The real panel: four cards fit; a fifth would end under the hint bar.
    g = pageGrid();
    CHECK_EQ(g.visible, 4);
    const om::mc::CardRect fourth = om::mc::cardRect(g, om::mc::kColChannels, 3, 0);
    CHECK(fourth.y + fourth.h <= kBoxY1);
    const om::mc::CardRect fifth = om::mc::cardRect(g, om::mc::kColChannels, 4, 0);
    CHECK(fifth.y + fifth.h > kBoxY1);   // that is why it is not drawn until a scroll brings it in
}

// 3. Card rects for both columns, and what a scroll does to them.
void test_card_rects() {
    const om::mc::CardGrid g = pageGrid();

    const om::mc::CardRect r00 = om::mc::cardRect(g, om::mc::kColChannels, 0, 0);
    CHECK_EQ(r00.x, 18);
    CHECK_EQ(r00.y, 100);
    CHECK_EQ(r00.w, 374);
    CHECK_EQ(r00.h, 68);

    const om::mc::CardRect r01 = om::mc::cardRect(g, om::mc::kColChannels, 1, 0);
    CHECK_EQ(r01.y, 100 + 68 + 10);

    const om::mc::CardRect r10 = om::mc::cardRect(g, om::mc::kColDms, 0, 0);
    CHECK_EQ(r10.x, 408);
    CHECK_EQ(r10.y, 100);
    CHECK_EQ(r10.w, 374);

    // Scroll moves the whole column up by (card + gap) per row, and two columns scroll independently.
    const om::mc::CardRect s1 = om::mc::cardRect(g, om::mc::kColChannels, 4, 1);
    CHECK_EQ(s1.y, r00.y + 3 * (68 + 10));

    // A column that does not exist has no rect (w == 0), so a caller cannot draw a third column by mistake.
    const om::mc::CardRect bad = om::mc::cardRect(g, 2, 0, 0);
    CHECK_EQ(bad.w, 0);
    CHECK_EQ(bad.h, 0);
}

// 4. Up/Down inside a column: move, and stop at the ends.
void test_focus_up_down() {
    const int counts[om::mc::kCardCols] = {3, 0};
    om::mc::CardFocus   f{om::mc::kColChannels, 0};

    f = om::mc::cardMoveFocus(f, om::mc::CardMove::Up, counts, om::mc::kCardCols);
    CHECK_EQ(f.row, 0);   // already at the top: nothing moves (no wrap)

    f = om::mc::cardMoveFocus(f, om::mc::CardMove::Down, counts, om::mc::kCardCols);
    CHECK_EQ(f.row, 1);
    f = om::mc::cardMoveFocus(f, om::mc::CardMove::Down, counts, om::mc::kCardCols);
    CHECK_EQ(f.row, 2);
    f = om::mc::cardMoveFocus(f, om::mc::CardMove::Down, counts, om::mc::kCardCols);
    CHECK_EQ(f.row, 2);   // the last card: Down does nothing
    CHECK_EQ(f.col, om::mc::kColChannels);   // Up/Down never change the column
}

// 5. Left/Right switch column and land on the nearest row, clamped to the target column's last card.
void test_focus_left_right() {
    const int counts[om::mc::kCardCols] = {3, 5};

    om::mc::CardFocus f{om::mc::kColChannels, 1};
    f = om::mc::cardMoveFocus(f, om::mc::CardMove::Right, counts, om::mc::kCardCols);
    CHECK_EQ(f.col, om::mc::kColDms);
    CHECK_EQ(f.row, 1);   // nearest row
    f = om::mc::cardMoveFocus(f, om::mc::CardMove::Right, counts, om::mc::kCardCols);
    CHECK_EQ(f.col, om::mc::kColDms);   // no third column: the key does nothing
    CHECK_EQ(f.row, 1);

    // From the DM column's last card (row 4) to three channels: clamp to the channel column's last card (2).
    om::mc::CardFocus g{om::mc::kColDms, 4};
    g = om::mc::cardMoveFocus(g, om::mc::CardMove::Left, counts, om::mc::kCardCols);
    CHECK_EQ(g.col, om::mc::kColChannels);
    CHECK_EQ(g.row, 2);

    // A shorter target than the row is clamped, a longer one keeps the row.
    om::mc::CardFocus h{om::mc::kColDms, 1};
    h = om::mc::cardMoveFocus(h, om::mc::CardMove::Left, counts, om::mc::kCardCols);
    CHECK_EQ(h.row, 1);
}

// 6. An empty column cannot take focus: today's DM column stays unreachable, and a shrinking list falls back.
void test_empty_column_refuses_focus() {
    // The real case: three channels, no DMs.
    const int counts[om::mc::kCardCols] = {3, 0};
    om::mc::CardFocus f{om::mc::kColChannels, 1};
    f = om::mc::cardMoveFocus(f, om::mc::CardMove::Right, counts, om::mc::kCardCols);
    CHECK_EQ(f.col, om::mc::kColChannels);   // Right does nothing while DMs are empty
    CHECK_EQ(f.row, 1);

    // No channels either: there is nothing to focus anywhere, and the result is still defined.
    const int none[om::mc::kCardCols] = {0, 0};
    om::mc::CardFocus n = om::mc::cardMoveFocus(om::mc::CardFocus{0, 0}, om::mc::CardMove::Down, none,
                                                om::mc::kCardCols);
    CHECK_EQ(n.col, 0);
    CHECK_EQ(n.row, 0);

    // The channel list shrank under a focus that was in it: fall to the first column that still has cards.
    const int only_dms[om::mc::kCardCols] = {0, 2};
    om::mc::CardFocus s{om::mc::kColChannels, 1};
    s = om::mc::cardMoveFocus(s, om::mc::CardMove::Up, only_dms, om::mc::kCardCols);
    CHECK_EQ(s.col, om::mc::kColDms);
    CHECK_EQ(s.row, 0);
}

// 7. Scrolling: the minimum move that keeps the focused card FULLY visible, clamped both ways.
void test_scroll_keeps_focused_visible() {
    const om::mc::CardGrid g = pageGrid();   // visible == 4

    CHECK_EQ(om::mc::cardScroll(g, 10, 0, 0), 0);
    CHECK_EQ(om::mc::cardScroll(g, 10, 3, 0), 0);    // the fourth card is still on screen
    CHECK_EQ(om::mc::cardScroll(g, 10, 4, 0), 1);    // the fifth is not: move by one
    CHECK_EQ(om::mc::cardScroll(g, 10, 9, 0), 6);    // the last card: max scroll (10-4)
    CHECK_EQ(om::mc::cardScroll(g, 10, 5, 4), 4);    // coming back down: no move needed
    CHECK_EQ(om::mc::cardScroll(g, 10, 4, 6), 4);    // going back up: move just enough
    CHECK_EQ(om::mc::cardScroll(g, 3, 0, 0), 0);     // a short column has nowhere to scroll
    CHECK_EQ(om::mc::cardScroll(g, 0, 0, 0), 0);     // an empty column even less
    CHECK_EQ(om::mc::cardScroll(g, 10, 9, 99), 6);   // a stale scroll is clamped to the maximum
    CHECK_EQ(om::mc::cardScroll(g, 10, 9, -5), 6);

    // Walk the focus from top to bottom and back: after every step the focused card is fully inside the panel.
    int scroll = 0;
    for (int row = 0; row < 10; row++) {
        scroll = om::mc::cardScroll(g, 10, row, scroll);
        const om::mc::CardRect r = om::mc::cardRect(g, om::mc::kColChannels, row, scroll);
        CHECK(r.y >= g.cards_y);
        CHECK(r.y + r.h <= kBoxY1);
    }
    CHECK_EQ(scroll, 6);
    for (int row = 9; row >= 0; row--) {
        scroll = om::mc::cardScroll(g, 10, row, scroll);
        const om::mc::CardRect r = om::mc::cardRect(g, om::mc::kColChannels, row, scroll);
        CHECK(r.y >= g.cards_y);
        CHECK(r.y + r.h <= kBoxY1);
    }
    CHECK_EQ(scroll, 0);
}

}  // namespace

int main() {
    test_two_equal_columns();
    test_visible_count();
    test_card_rects();
    test_focus_up_down();
    test_focus_left_right();
    test_empty_column_refuses_focus();
    test_scroll_keeps_focused_visible();

    printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
