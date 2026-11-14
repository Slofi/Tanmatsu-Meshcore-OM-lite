// SPDX-License-Identifier: MIT
// Host tests for the SD chat history (S502): one record per message, in the L2's format, and the torn-tail rule
// that makes a power cut cost at most the last line.
//
// The format itself is the L2's (`firmware/src/history/history_log.h`), so these tests check OUR mapping onto it
// — that a message survives the round trip unchanged, that a half-written record is dropped without taking its
// predecessors with it, and that a log longer than the ring keeps the NEWEST window.
#include <stdio.h>
#include <string.h>

#include "mc_history.h"
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

om::mc::Message mk(bool outgoing, const char* sender, const char* text, uint32_t wall, int hops) {
    om::mc::Message m;
    m.outgoing = outgoing;
    snprintf(m.sender, sizeof(m.sender), "%s", sender);
    snprintf(m.text, sizeof(m.text), "%s", text);
    m.ts       = wall;
    m.local_ts = wall;
    m.snr_x10  = 32;
    m.direct   = (hops == 0);
    return m;
}

void test_round_trip() {
    om::mc::Message in = mk(true, "TNMT (SI)", "ping \xc5\xa1 \xc4\x8d \xc5\xbe", 1794607117u, 0);
    uint8_t        rec[om::mc::kTextBytes + 128];
    const int      n = om::mc::historyEncode(in, 7, rec, (int)sizeof(rec));
    CHECK_EQ(n > 0, true);

    om::mc::Message out[4];
    CHECK_EQ(om::mc::historyLoad(rec, (size_t)n, 0, out, 4), 1);
    CHECK_EQ(out[0].outgoing, true);
    CHECK_EQ(out[0].ts, 1794607117u);
    CHECK_STR(out[0].sender, "TNMT (SI)");
    CHECK_STR(out[0].text, "ping \xc5\xa1 \xc4\x8d \xc5\xbe");   // Slovenian letters survive as bytes
    CHECK_EQ(out[0].seq, 7u);
}

void test_many_records_keep_the_newest() {
    uint8_t log[2048];
    size_t  len = 0;
    for (int i = 0; i < 6; i++) {
        char text[16];
        snprintf(text, sizeof(text), "m%d", i);
        om::mc::Message m = mk(false, "ERA-3 (SI)", text, 1000u + (uint32_t)i, 1);
        const int      n  = om::mc::historyEncode(m, (uint32_t)(i + 1), log + len, (int)(sizeof(log) - len));
        CHECK_EQ(n > 0, true);
        len += (size_t)n;
    }
    om::mc::Message out[3];
    CHECK_EQ(om::mc::historyLoad(log, len, 2, out, 3), 3);
    // The newest three, oldest first, and all of them on the channel the FILE belongs to (2).
    CHECK_STR(out[0].text, "m3");
    CHECK_STR(out[1].text, "m4");
    CHECK_STR(out[2].text, "m5");
    CHECK_EQ(out[0].channel, 2);
    CHECK_EQ(out[2].outgoing, false);
}

void test_torn_tail_costs_only_the_last_record() {
    uint8_t log[1024];
    size_t  len = 0;
    for (int i = 0; i < 3; i++) {
        char text[16];
        snprintf(text, sizeof(text), "m%d", i);
        const int n = om::mc::historyEncode(mk(false, "n", text, 2000u + (uint32_t)i, 0), (uint32_t)(i + 1),
                                            log + len, (int)(sizeof(log) - len));
        CHECK_EQ(n > 0, true);
        len += (size_t)n;
    }
    const size_t full = len;
    // A power cut in the middle of the last record.
    for (size_t cut = 1; cut < 6; cut++) {
        om::mc::Message out[4];
        CHECK_EQ(om::mc::historyLoad(log, full - cut, 0, out, 4), 2);
        CHECK_STR(out[1].text, "m1");
    }
    // A corrupted byte in the MIDDLE record stops the walk there (nothing after a bad record is trusted).
    // The byte must land in the SECOND record, so the first record is measured rather than guessed.
    const int n0 = om::mc::historyEncode(mk(false, "n", "m0", 2000u, 0), 1, log, (int)sizeof(log));
    CHECK_EQ(n0 > 0, true);
    uint8_t bad[1024];
    memcpy(bad, log, full);
    bad[(size_t)n0 + 20] ^= 0xFF;
    om::mc::Message out[4];
    CHECK_EQ(om::mc::historyLoad(bad, full, 0, out, 4), 1);
    CHECK_STR(out[0].text, "m0");
}

void test_file_names_and_empty_input() {
    char name[64];
    om::mc::historyFileName(0xD7, "Test Channel", name, sizeof(name));
    CHECK_STR(name, "ch-d7-Test_Channel.log");   // sanitised: no quotes, no spaces
    om::mc::historyFileName(0x00, nullptr, name, sizeof(name));
    CHECK_EQ(strncmp(name, "ch-00-channel.log", 17), 0);

    om::mc::Message out[2];
    CHECK_EQ(om::mc::historyLoad(nullptr, 0, 0, out, 2), 0);
    CHECK_EQ(om::mc::historyLoad(reinterpret_cast<const uint8_t*>("junk"), 4, 0, out, 2), 0);
    uint8_t tiny[4];
    CHECK_EQ(om::mc::historyEncode(mk(false, "n", "x", 1, 0), 1, tiny, (int)sizeof(tiny)), 0);
}

// S503: the id a record carries is the FILE's id, and the seen point is the format's Meta record. These two
// together are what make unread survive a restart, so they are pinned here rather than trusted.
void test_log_id_is_the_files_own_id() {
    om::mc::Message in = mk(false, "ERA-3 (SI)", "hello", 1794607200u, 1);
    in.seq             = 41;   // the per-boot ring id, deliberately different from the file's id
    uint8_t rec[512];
    const int n = om::mc::historyEncode(in, 7, rec, (int)sizeof(rec));
    CHECK_EQ(n > 0, true);

    om::mc::Message out[2];
    uint32_t       max_id = 0;
    CHECK_EQ(om::mc::historyLoad(rec, (size_t)n, 0, out, 2, &max_id), 1);
    CHECK_EQ(out[0].log_id, 7u);     // the id the Meta record will point at
    CHECK_EQ(out[0].seq, 41u);       // the ring's id survives as its own field, as the L2 does it
    CHECK_EQ(max_id, 7u);            // so the next append continues at 8

    // A zero log id falls back to the ring seq, so an older caller still writes a usable record.
    const int n2 = om::mc::historyEncode(in, 0, rec, (int)sizeof(rec));
    CHECK_EQ(n2 > 0, true);
    CHECK_EQ(om::mc::historyLoad(rec, (size_t)n2, 0, out, 2, &max_id), 1);
    CHECK_EQ(out[0].log_id, 41u);
}

void test_seen_point_round_trip() {
    uint8_t log[2048];
    int     len = 0;
    for (uint32_t i = 1; i <= 3; i++) {
        const int n = om::mc::historyEncode(mk(false, "n", "m", 2000u + i, 0), i, log + len,
                                            (int)sizeof(log) - len);
        CHECK_EQ(n > 0, true);
        len += n;
    }
    om::mc::SeenPoint none = om::mc::historyReadSeen(log, (size_t)len);
    CHECK_EQ(none.found, false);   // messages only: a log from before S503 reads as "no seen point"

    const int m1 = om::mc::historyEncodeMeta(2, 1, log + len, (int)sizeof(log) - len);
    CHECK_EQ(m1 > 0, true);
    len += m1;
    om::mc::SeenPoint sp = om::mc::historyReadSeen(log, (size_t)len);
    CHECK_EQ(sp.found, true);
    CHECK_EQ(sp.seen_log_id, 2u);
    CHECK_EQ(sp.unread, 1u);

    // LAST one wins, exactly as the L2's loader applies records in file order.
    const int m2 = om::mc::historyEncodeMeta(3, 0, log + len, (int)sizeof(log) - len);
    CHECK_EQ(m2 > 0, true);
    len += m2;
    sp = om::mc::historyReadSeen(log, (size_t)len);
    CHECK_EQ(sp.seen_log_id, 3u);
    CHECK_EQ(sp.unread, 0u);

    // The Meta records do not disturb the message count or the id the next append continues from.
    om::mc::Message out[8];
    uint32_t       max_id = 0;
    CHECK_EQ(om::mc::historyLoad(log, (size_t)len, 0, out, 8, &max_id), 3);
    CHECK_EQ(max_id, 3u);

    // A torn tail takes the record it lands in, not the seen point before it.
    uint8_t cut[2048];
    memcpy(cut, log, (size_t)len);
    om::mc::SeenPoint torn = om::mc::historyReadSeen(cut, (size_t)len - 3);
    CHECK_EQ(torn.found, true);
    CHECK_EQ(torn.seen_log_id, 2u);   // the last whole Meta record
}

void test_seen_point_collapses_on_corruption() {
    uint8_t rec[64];
    const int n = om::mc::historyEncodeMeta(9, 0, rec, (int)sizeof(rec));
    CHECK_EQ(n > 0, true);
    rec[0] ^= 0xFF;   // bad magic: the record is refused, so there is no seen point at all
    const om::mc::SeenPoint sp = om::mc::historyReadSeen(rec, (size_t)n);
    CHECK_EQ(sp.found, false);
}

}  // namespace

int main() {
    test_round_trip();
    test_many_records_keep_the_newest();
    test_torn_tail_costs_only_the_last_record();
    test_file_names_and_empty_input();
    test_log_id_is_the_files_own_id();     // S503
    test_seen_point_round_trip();          // S503
    test_seen_point_collapses_on_corruption();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
