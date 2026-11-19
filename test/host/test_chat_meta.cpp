// SPDX-License-Identifier: MIT
// Host tests for the chat time row (S515/EST-462): the last-repeater resolver, the dim head-line formatter and
// the pixel box that keeps it away from the age column.
//
// Two rules here are the kind that fail silently on the glass: a hop hash that matches TWO contacts must never
// be shown as a name, and a message restored from an old (v57) record must show NO hop count — its hops byte was
// a fake 0/1, not a measurement.
#include <stdio.h>
#include <string.h>

#include "chat_meta.h"
#include "mc_store.h"

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

#define CHECK_STR(got, want)                                                          \
  do {                                                                                \
    ++g_checks;                                                                       \
    if (strcmp((got), (want)) != 0) {                                                 \
      printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (got), (want));     \
      ++g_failures;                                                                   \
      return;                                                                         \
    }                                                                                 \
  } while (0)

// A contact whose key starts with the bytes given, padded with a non-zero tail so it is a FULL key (a row whose
// bytes 4..31 are zero is a prefix-only placeholder and must never name a hop). `seed` keeps two rows with the
// same prefix DIFFERENT keys.
void addContact(om::mc::ContactStore& s, const uint8_t* head, size_t head_len, const char* name,
                uint8_t seed = 0) {
    uint8_t pub[32];
    for (size_t i = 0; i < sizeof(pub); ++i) pub[i] = (uint8_t)(0xA0 + i + seed);
    for (size_t i = 0; i < head_len && i < sizeof(pub); ++i) pub[i] = head[i];
    om::mc::contactsUpsert(s, pub, name, 2, 1, 100, 100, 0, 0);
}

void test_resolver_unique_one_byte() {
    om::mc::ContactStore s;
    const uint8_t        head[1] = {0x42};
    addContact(s, head, 1, "TEST-RPT-A");
    char out[40];
    CHECK_EQ(om::mc::pathHopLabel(s, head, 1, out, sizeof(out)), true);
    CHECK_STR(out, "TEST-RPT-A");
}

void test_resolver_unique_two_bytes() {
    om::mc::ContactStore s;
    const uint8_t        head[2] = {0x86, 0xa8};
    addContact(s, head, 2, "TEST-RPT-B");
    char out[40];
    CHECK_EQ(om::mc::pathHopLabel(s, head, 2, out, sizeof(out)), true);
    CHECK_STR(out, "TEST-RPT-B");
}

void test_resolver_unique_three_bytes() {
    om::mc::ContactStore s;
    const uint8_t        head[3] = {0x11, 0x22, 0x33};
    addContact(s, head, 3, "TEST-RPT-C");
    char out[40];
    CHECK_EQ(om::mc::pathHopLabel(s, head, 3, out, sizeof(out)), true);
    CHECK_STR(out, "TEST-RPT-C");
}

void test_resolver_two_matches_is_hex() {
    om::mc::ContactStore s;
    const uint8_t        head[2] = {0x86, 0xa8};
    addContact(s, head, 2, "TEST-RPT-B");   // same first two bytes, different tails
    addContact(s, head, 2, "TEST-RPT-D", 7);
    char out[40];
    CHECK_EQ(om::mc::pathHopLabel(s, head, 2, out, sizeof(out)), false);
    CHECK_STR(out, "86a8");                  // never a guess between two
}

void test_resolver_no_match_is_hex() {
    om::mc::ContactStore s;
    const uint8_t        head[2] = {0x86, 0xa8};
    addContact(s, head, 2, "TEST-RPT-B");
    const uint8_t miss[2] = {0x77, 0x01};
    char          out[40];
    CHECK_EQ(om::mc::pathHopLabel(s, miss, 2, out, sizeof(out)), false);
    CHECK_STR(out, "7701");
}

void test_resolver_ignores_a_prefix_only_row() {
    om::mc::ContactStore s;
    uint8_t              pub[32] = {0};
    pub[0] = 0x86;
    pub[1] = 0xa8;                            // bytes 4..31 stay zero -> an incomplete key
    om::mc::contactsUpsert(s, pub, "GHOST", 1, 0, 0, 0, 0, 0);
    const uint8_t head[2] = {0x86, 0xa8};
    char          out[40];
    CHECK_EQ(om::mc::pathHopLabel(s, head, 2, out, sizeof(out)), false);
    CHECK_STR(out, "86a8");
}

void test_formatter_direct() {
    om::mc::MsgMeta m;
    m.hops_known = true;
    m.hops       = 0;
    m.bytes      = 87;
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "direct \xC2\xB7 87 B");
}

void test_formatter_direct_without_bytes() {
    om::mc::MsgMeta m;
    m.hops_known = true;
    m.hops       = 0;
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "direct");
}

void test_formatter_n_hops_with_via_and_copies() {
    om::mc::MsgMeta m;
    m.hops_known = true;
    m.hops       = 3;
    snprintf(m.via, sizeof(m.via), "%s", "TEST-RPT-A");
    m.bytes  = 87;
    m.copies = 2;
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "3 hops via TEST-RPT-A \xC2\xB7 87 B \xC2\xB7 heard 2\xC3\x97");
}

void test_formatter_one_hop_singular() {
    om::mc::MsgMeta m;
    m.hops_known = true;
    m.hops       = 1;
    snprintf(m.via, sizeof(m.via), "%s", "86a8");
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "1 hop via 86a8");
}

void test_formatter_n_hops_without_via() {
    om::mc::MsgMeta m;
    m.hops_known = true;
    m.hops       = 3;
    m.bytes      = 87;
    m.copies     = 2;
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "3 hops \xC2\xB7 87 B \xC2\xB7 heard 2\xC3\x97");
}

void test_formatter_restored_old_record_is_empty() {
    om::mc::MsgMeta m;   // a v57 record: hops unknown, no bytes, no copies
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "");
}

void test_formatter_restored_new_record_keeps_hops_not_bytes() {
    om::mc::MsgMeta m;   // path is in the record; the byte count is not
    m.hops_known = true;
    m.hops       = 2;
    snprintf(m.via, sizeof(m.via), "%s", "TEST-RPT-B");
    m.bytes  = 0;
    m.copies = 0;
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "2 hops via TEST-RPT-B");
}

void test_formatter_one_copy_is_not_shown() {
    om::mc::MsgMeta m;
    m.hops_known = true;
    m.hops       = 0;
    m.bytes      = 87;
    m.copies     = 1;    // one copy is the line that arrived; only a second is worth the ink
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "direct \xC2\xB7 87 B");
}

void test_formatter_own_bytes_and_repeats() {
    om::mc::MsgMeta m;
    m.outgoing = true;
    m.bytes    = 87;
    m.copies   = 2;
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "87 B \xC2\xB7 repeated 2\xC3\x97");
}

void test_formatter_own_one_echo_is_not_shown() {
    om::mc::MsgMeta m;
    m.outgoing = true;
    m.bytes    = 87;
    m.copies   = 1;
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "87 B");
}

void test_formatter_own_restored_is_empty() {
    om::mc::MsgMeta m;
    m.outgoing = true;   // bytes and echoes are not in the L2 record
    char out[om::mc::kChatMetaCap];
    om::mc::chatMetaText(m, out, sizeof(out));
    CHECK_STR(out, "");
}

void test_clip_never_reaches_the_age_column() {
    // A realistic incoming line: sender at 76 px, 40 px wide, age column starting at 700.
    const int x     = om::mc::metaIncomingX(76, 40);
    const int max_w = om::mc::metaIncomingMaxW(76, 40, 700);
    CHECK_EQ(x, 76 + 40 + om::mc::kChatMetaGap);
    CHECK(max_w > 0);
    CHECK(x + max_w <= 700 - om::mc::kChatMetaGap);   // the clip's right edge stops short of the age

    // A sender so long that nothing honest fits: the formatter gets no pixels, not an overlap.
    CHECK_EQ(om::mc::metaIncomingMaxW(76, 640, 700), 0);
}

void test_clip_never_overlaps_you() {
    // Our own line: body 76..736, "you" is 30 px wide, right-aligned at 736.
    const int right = om::mc::metaOutgoingRight(736, 30);
    const int max_w = om::mc::metaOutgoingMaxW(76, 736, 30);
    CHECK(max_w > 0);
    CHECK(right + 30 + om::mc::kChatMetaGap <= 736);   // the meta ends left of "you"
    CHECK(right - max_w >= 76);                        // and never leaves the body column
}

}  // namespace

int main() {
    test_resolver_unique_one_byte();
    test_resolver_unique_two_bytes();
    test_resolver_unique_three_bytes();
    test_resolver_two_matches_is_hex();
    test_resolver_no_match_is_hex();
    test_resolver_ignores_a_prefix_only_row();
    test_formatter_direct();
    test_formatter_direct_without_bytes();
    test_formatter_n_hops_with_via_and_copies();
    test_formatter_one_hop_singular();
    test_formatter_n_hops_without_via();
    test_formatter_restored_old_record_is_empty();
    test_formatter_restored_new_record_keeps_hops_not_bytes();
    test_formatter_one_copy_is_not_shown();
    test_formatter_own_bytes_and_repeats();
    test_formatter_own_one_echo_is_not_shown();
    test_formatter_own_restored_is_empty();
    test_clip_never_reaches_the_age_column();
    test_clip_never_overlaps_you();

    printf("test_chat_meta: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
