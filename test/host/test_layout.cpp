// SPDX-License-Identifier: MIT
// Host tests for the chat layout (S503): how a message becomes lines, which messages a window shows, and the
// read prefix the seen point is turned into.
//
// The wrap needs a font to measure with, and a font is device data — so these tests pass a FAKE font (every
// character 10 px wide, measured by code point) through the same MeasureFn the P4 passes its real VLW font. The
// break points are then arithmetic, and arithmetic is what a test can pin. It also means the Slovenian cases
// (2- and 3-byte letters) are exercised without a font file.
#include <stdio.h>
#include <string.h>

#include "chat_layout.h"

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

// Every code point 10 px wide. Counts UTF-8 lead bytes, so a 2-byte č costs one character, as a real font would.
int fakeMeasure(void* /*ctx*/, const char* ptr, int len) {
    int n = 0;
    for (int i = 0; i < len; i++) {
        if (((unsigned char)ptr[i] & 0xC0) != 0x80) ++n;
    }
    return n * 10;
}

const char* lineStr(const char* text, const om::mc::Line& ln, char* out) {
    memcpy(out, text + ln.start, ln.len);
    out[ln.len] = '\0';
    return out;
}

void test_empty_and_single_line() {
    om::mc::Wrapped w = om::mc::wrapText("", 100, fakeMeasure, nullptr);
    CHECK_EQ(w.count, 1);
    CHECK_EQ(w.line[0].len, 0);
    CHECK_EQ(w.clipped, false);

    w = om::mc::wrapText(nullptr, 100, fakeMeasure, nullptr);
    CHECK_EQ(w.count, 1);

    const char* text = "hello";
    w = om::mc::wrapText(text, 100, fakeMeasure, nullptr);
    CHECK_EQ(w.count, 1);
    CHECK_EQ(w.line[0].len, 5);
    CHECK_EQ(w.clipped, false);

    // Exactly at the width: five characters at 10 px in 50 px must still be ONE line (the boundary is
    // inclusive — an off-by-one here would wrap every full line in half).
    w = om::mc::wrapText(text, 50, fakeMeasure, nullptr);
    CHECK_EQ(w.count, 1);
}

void test_breaks_between_words() {
    const char*   text = "aaa bbb ccc";
    om::mc::Wrapped w  = om::mc::wrapText(text, 60, fakeMeasure, nullptr);
    char          buf[32];
    CHECK_EQ(w.count, 3);
    CHECK(strcmp(lineStr(text, w.line[0], buf), "aaa") == 0);   // the space it broke at is consumed
    CHECK(strcmp(lineStr(text, w.line[1], buf), "bbb") == 0);
    CHECK(strcmp(lineStr(text, w.line[2], buf), "ccc") == 0);
    CHECK_EQ(w.clipped, false);

    // Wider: two words on the first line.
    w = om::mc::wrapText(text, 70, fakeMeasure, nullptr);
    CHECK_EQ(w.count, 2);
    CHECK(strcmp(lineStr(text, w.line[0], buf), "aaa bbb") == 0);
    CHECK(strcmp(lineStr(text, w.line[1], buf), "ccc") == 0);
}

void test_long_word_is_hard_broken() {
    const char*   text = "aaaaaaaaaa";   // no space at all
    om::mc::Wrapped w  = om::mc::wrapText(text, 40, fakeMeasure, nullptr);
    char          buf[16];
    CHECK_EQ(w.count, 3);
    CHECK(strcmp(lineStr(text, w.line[0], buf), "aaaa") == 0);
    CHECK(strcmp(lineStr(text, w.line[1], buf), "aaaa") == 0);
    CHECK(strcmp(lineStr(text, w.line[2], buf), "aa") == 0);
    CHECK_EQ(w.clipped, false);
}

void test_utf8_is_never_cut() {
    // "čšžčšž" = six 2-byte letters = 12 bytes; 3 letters fit a 30 px line.
    const char*   text = "\xc4\x8d\xc5\xa1\xc5\xbe\xc4\x8d\xc5\xa1\xc5\xbe";
    om::mc::Wrapped w  = om::mc::wrapText(text, 30, fakeMeasure, nullptr);
    CHECK_EQ(w.count, 2);
    CHECK_EQ(w.line[0].len, 6);    // three whole letters, not five bytes
    CHECK_EQ(w.line[1].len, 6);
    CHECK_EQ((int)w.line[0].start + (int)w.line[0].len, 6);
    CHECK(((unsigned char)text[w.line[1].start] & 0xC0) == 0xC0);   // starts on a lead byte
    CHECK_EQ(w.clipped, false);

    // A space inside a UTF-8 sentence: the break lands between words, and both lines are whole letters.
    const char*   mixed = "\xc5\xa1\xc5\xa1\xc5\xa1 \xc5\xa1\xc5\xa1\xc5\xa1";
    w                   = om::mc::wrapText(mixed, 50, fakeMeasure, nullptr);
    CHECK_EQ(w.count, 2);
    CHECK_EQ(w.line[0].len, 6);    // three letters, no trailing space
}

void test_clipping_stops_at_the_line_cap() {
    // Ten one-word lines: only kMaxLines are produced and the rest is reported as clipped.
    const char*   text = "a b c d e f g h i j";
    om::mc::Wrapped w  = om::mc::wrapText(text, 10, fakeMeasure, nullptr);
    CHECK_EQ(w.count, om::mc::kMaxLines);
    CHECK_EQ(w.clipped, true);
}

void test_window_all_fit() {
    const int lines[3] = {3, 3, 3};   // head + 2 body lines each (the caller passes head+body)
    om::mc::Window w   = om::mc::chatWindow(lines, 3, 22, 8, 288, 0);
    CHECK_EQ(w.first, 0);
    CHECK_EQ(w.last, 2);
    CHECK_EQ(w.hidden_above, 0);
    CHECK_EQ(w.hidden_below, 0);
    // Nothing is off screen, so there is nothing to scroll TO: Up must be a no-op, not a way to hide the newest
    // message and show an empty band (S503, found on the glass at three messages).
    CHECK_EQ(w.max_scroll, 0);

    // A scroll request beyond that is clamped away entirely.
    w = om::mc::chatWindow(lines, 3, 22, 8, 288, 2);
    CHECK_EQ(w.last, 2);
    CHECK_EQ(w.hidden_below, 0);
}

void test_window_overflows_to_the_newest() {
    int lines[20];
    for (int i = 0; i < 20; i++) lines[i] = 3;   // 3*22 + 8 = 74 px each
    om::mc::Window w = om::mc::chatWindow(lines, 20, 22, 8, 288, 0);
    CHECK_EQ(w.last, 19);                 // newest at the bottom
    CHECK_EQ(w.hidden_below, 0);
    // 288 / 74 = 3 whole blocks (222 px); the 4th would need 296 px, so 17..19 are shown and 17 are hidden.
    CHECK_EQ(w.first, 17);
    CHECK_EQ(w.hidden_above, 17);
    // Scrolling stops when the oldest message reaches the top of a full window, i.e. at 17.
    CHECK_EQ(w.max_scroll, 17);

    // All the way back: messages 0..2 fill the window and 17 newer ones sit below it.
    w = om::mc::chatWindow(lines, 20, 22, 8, 288, 17);
    CHECK_EQ(w.first, 0);
    CHECK_EQ(w.last, 2);
    CHECK_EQ(w.hidden_above, 0);
    CHECK_EQ(w.hidden_below, 17);

    // Beyond the end clamps instead of reading past the array.
    w = om::mc::chatWindow(lines, 20, 22, 8, 288, 999);
    CHECK_EQ(w.last, 2);
    CHECK_EQ(w.hidden_above, 0);
}

void test_window_shows_one_tall_message() {
    const int lines[2]     = {20, 1};
    om::mc::Window w       = om::mc::chatWindow(lines, 2, 22, 8, 100, 0);
    CHECK_EQ(w.last, 1);
    CHECK_EQ(w.first, 1);   // the newest one is always shown, even when it alone is taller than the window
    CHECK_EQ(w.hidden_above, 1);

    const int empty_window = 0;
    w = om::mc::chatWindow(&empty_window, 0, 22, 8, 288, 0);
    CHECK_EQ(w.last, -1);   // nothing to draw
    CHECK_EQ(w.max_scroll, 0);
}

void test_read_prefix() {
    const uint32_t ids[3] = {1, 2, 3};
    CHECK_EQ(om::mc::historyReadCount(ids, 3, 0), 0);    // no Meta record yet: nothing is claimed as read
    CHECK_EQ(om::mc::historyReadCount(ids, 3, 1), 1);
    CHECK_EQ(om::mc::historyReadCount(ids, 3, 2), 2);
    CHECK_EQ(om::mc::historyReadCount(ids, 3, 3), 3);
    CHECK_EQ(om::mc::historyReadCount(ids, 3, 99), 3);   // a seen point past the end reads everything

    // A message that never reached the card (id 0) is not counted, but it does not stop the walk: the ids after
    // it are still the card's own and still mean something.
    const uint32_t gap[3] = {1, 0, 3};
    CHECK_EQ(om::mc::historyReadCount(gap, 3, 1), 1);
    CHECK_EQ(om::mc::historyReadCount(gap, 3, 3), 3);
    CHECK_EQ(om::mc::historyReadCount(nullptr, 3, 3), 0);
}

}  // namespace

int main() {
    test_empty_and_single_line();
    test_breaks_between_words();
    test_long_word_is_hard_broken();
    test_utf8_is_never_cut();
    test_clipping_stops_at_the_line_cap();
    test_window_all_fit();
    test_window_overflows_to_the_newest();
    test_window_shows_one_tall_message();
    test_read_prefix();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
