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
// S515/EST-462: the L2's own record encoder, so a v57-style fake record can be built exactly as v57 wrote it.
#include "../../../firmware/src/history/history_log.h"

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

// S513/EST-434: a DM record goes through the SAME L2 format, and the peer supplied by the caller comes back with
// the message. This is the mapping under test, not the format (that is the L2's and is already exercised above).
void test_dm_record_round_trip() {
    uint8_t pub[32];
    for (size_t i = 0; i < sizeof(pub); i++) pub[i] = (uint8_t)(0x40 + i);

    om::mc::DmMessage m;
    memcpy(m.peer_pub, pub, sizeof(pub));
    snprintf(m.peer_name, sizeof(m.peer_name), "Alpha");
    m.outgoing = false;
    snprintf(m.text, sizeof(m.text), "hi there");
    m.ts       = 1750000000u;
    m.local_ts = 1750000100u;
    m.snr_x10  = 25;
    m.direct   = true;

    uint8_t   rec[512];
    const int n = om::mc::historyEncodeDm(m, 7, rec, (int)sizeof(rec));
    CHECK_EQ(n > 0, true);

    om::mc::DmMessage out[4];
    uint32_t          max_id = 0;
    const size_t      got    = om::mc::historyLoadDm(rec, (size_t)n, pub, "fallback", out, 4, &max_id);
    CHECK_EQ(got, 1u);
    CHECK_EQ(max_id, 7u);
    CHECK_EQ(out[0].log_id, 7u);
    CHECK_EQ(out[0].outgoing, false);
    CHECK(strcmp(out[0].text, "hi there") == 0);
    CHECK(memcmp(out[0].peer_pub, pub, 32) == 0);
    CHECK_EQ(out[0].ts, 1750000000u);
    CHECK_EQ(out[0].direct, true);
}

// S513/EST-439: the delivered mark, the L2's way. An outgoing message is written as Sending; the ACK appends an
// Update record naming its log id (history_log.h Type::Update / kUpdateState), and loading the log applies it in
// file order. An Update for a message that is not in the window changes nothing.
void test_dm_delivery_update() {
    uint8_t pub[32];
    for (size_t i = 0; i < sizeof(pub); i++) pub[i] = (uint8_t)(0x80 + i);

    om::mc::DmMessage m;
    memcpy(m.peer_pub, pub, sizeof(pub));
    snprintf(m.peer_name, sizeof(m.peer_name), "Beta");
    m.outgoing = true;
    m.delivery = om::ui::DmState::Sending;
    snprintf(m.text, sizeof(m.text), "did you get this");
    m.ts       = 1750000200u;
    m.local_ts = m.ts;

    uint8_t log[1024];
    int     at = om::mc::historyEncodeDm(m, 11, log, (int)sizeof(log));
    CHECK_EQ(at > 0, true);
    int n = om::mc::historyEncodeDmState(11, om::ui::DmState::Delivered, 1750000260u, log + at,
                                         (int)sizeof(log) - at);
    CHECK_EQ(n > 0, true);
    at += n;
    // An Update for a log id that is not in the file: harmless.
    n = om::mc::historyEncodeDmState(99, om::ui::DmState::Delivered, 1u, log + at, (int)sizeof(log) - at);
    CHECK_EQ(n > 0, true);
    at += n;

    om::mc::DmMessage out[4];
    uint32_t        max_id = 0;
    const size_t    got    = om::mc::historyLoadDm(log, (size_t)at, pub, "fallback", out, 4, &max_id);
    CHECK_EQ(got, 1u);
    CHECK_EQ(max_id, 11u);
    CHECK_EQ(out[0].outgoing, true);
    CHECK_EQ((int)out[0].delivery, (int)om::ui::DmState::Delivered);   // the restart keeps the ACK, not "sending"

    // The message alone still loads as Sending: the state is written with the message, so a power cut before the
    // ACK cannot invent a delivery.
    int only = om::mc::historyEncodeDm(m, 11, log, (int)sizeof(log));
    CHECK_EQ(only > 0, true);
    const size_t got2 = om::mc::historyLoadDm(log, (size_t)only, pub, "fallback", out, 4, &max_id);
    CHECK_EQ(got2, 1u);
    CHECK_EQ((int)out[0].delivery, (int)om::ui::DmState::Sending);
}

// S515/EST-462: the REAL path, not the old fake direct/flood pair. The record is the L2's own layout, so the
// path must appear in ITS fields (hops, pathHashSize, pathCount, path[]) and survive the loader unchanged.
void test_real_path_round_trip() {
    const uint8_t path[6] = {0x11, 0x22, 0x33, 0x44, 0x86, 0xa8};   // two 2-byte hashes; 86a8 is the hop nearest us

    om::mc::Message in = mk(false, "ERA-3 (SI)", "via two", 1794607300u, 0);
    in.direct     = false;
    in.hops_known = true;
    in.hops       = 3;
    in.hash_size  = 2;
    in.path_bytes = (uint8_t)sizeof(path);
    memcpy(in.path, path, sizeof(path));

    uint8_t   rec[512];
    const int n = om::mc::historyEncode(in, 5, rec, (int)sizeof(rec));
    CHECK_EQ(n > 0, true);

    // The record itself, read through the L2's decoder: hops 3, 2-byte hashes, 3 of them, the real bytes.
    om::history::RecordRef ref;
    CHECK_EQ(om::history::decodeRecordAt(rec, n, 0, &ref), n);
    om::history::Msg h;
    CHECK_EQ(om::history::decodeMsgPayload(ref.payload, (int)ref.len - om::history::kHeaderBytes, &h), true);
    CHECK_EQ(h.hops, 3);
    CHECK_EQ(h.pathHashSize, 2);
    CHECK_EQ(h.pathCount, 3);    // this build writes pathCount == hops (the v57 fake wrote 0)
    CHECK_EQ(h.pathLen, 6);
    CHECK(memcmp(h.path, path, sizeof(path)) == 0);

    om::mc::Message out[2];
    CHECK_EQ(om::mc::historyLoad(rec, (size_t)n, 0, out, 2), 1);
    CHECK_EQ(out[0].hops_known, true);
    CHECK_EQ(out[0].direct, false);
    CHECK_EQ(out[0].hops, 3);
    CHECK_EQ(out[0].hash_size, 2);
    CHECK_EQ(out[0].path_bytes, 6);
    CHECK(memcmp(out[0].path, path, sizeof(path)) == 0);
    CHECK_EQ(out[0].frame_len, 0);   // the byte count is NOT in the record: shown this boot only
    CHECK_EQ(out[0].copies, 0);
}

// S515/EST-462, the restart rule: a record written by v57 (or older) has the FAKE path fields — hops 0 or 1 with
// pathCount 0 and no bytes. hops == 0 is "direct" in both encoders; hops > 0 with pathCount 0 is UNKNOWN and must
// never be shown as "1 hop".
void test_v57_fake_record_reads_unknown_hops() {
    uint8_t payload[om::history::kMaxPayload];
    uint8_t rec[512];

    om::history::Msg h;
    h.logId = 3;
    h.seq   = 3;
    h.wall  = 1794607400u;
    h.setWallReal(true);
    h.hops         = 1;      // v57's fake "flood"
    h.pathHashSize = 1;
    h.pathCount    = 0;
    h.pathLen      = 0;
    om::history::msgSetText(h, "ERA-3 (SI)", "fake path");
    const int plen = om::history::encodeMsgPayload(h, payload, (int)sizeof(payload));
    CHECK_EQ(plen > 0, true);
    const int n = om::history::encodeRecord(om::history::Type::Msg, payload, plen, rec, (int)sizeof(rec));
    CHECK_EQ(n > 0, true);

    om::mc::Message out[2];
    CHECK_EQ(om::mc::historyLoad(rec, (size_t)n, 0, out, 2), 1);
    CHECK_EQ(out[0].hops_known, false);   // the fake "1" is not a measurement
    CHECK_EQ(out[0].hops, 0);
    CHECK_EQ(out[0].direct, false);
    CHECK_STR(out[0].text, "fake path");

    // hops == 0 (v57's direct) is true in both encoders, so it stays "direct".
    h.hops         = 0;
    h.pathHashSize = 1;
    h.pathCount    = 0;
    h.pathLen      = 0;
    const int plen0 = om::history::encodeMsgPayload(h, payload, (int)sizeof(payload));
    CHECK_EQ(plen0 > 0, true);
    const int n0 = om::history::encodeRecord(om::history::Type::Msg, payload, plen0, rec, (int)sizeof(rec));
    CHECK_EQ(n0 > 0, true);
    CHECK_EQ(om::mc::historyLoad(rec, (size_t)n0, 0, out, 2), 1);
    CHECK_EQ(out[0].hops_known, true);
    CHECK_EQ(out[0].hops, 0);
    CHECK_EQ(out[0].direct, true);
}

// The DM encoder carries the same real path (one thread, but each message has its own route).
void test_dm_real_path_round_trip() {
    uint8_t pub[32];
    for (size_t i = 0; i < sizeof(pub); i++) pub[i] = (uint8_t)(0x30 + i);
    const uint8_t path[3] = {0x01, 0x02, 0x86};   // three 1-byte hops; 0x86 is the last one

    om::mc::DmMessage m;
    memcpy(m.peer_pub, pub, sizeof(pub));
    snprintf(m.peer_name, sizeof(m.peer_name), "Alpha");
    m.outgoing    = false;
    m.hops_known  = true;
    m.hops        = 3;
    m.hash_size   = 1;
    m.path_bytes  = 3;
    memcpy(m.path, path, sizeof(path));
    m.snr_x10     = 10;
    m.ts          = 1750000300u;
    m.local_ts    = m.ts;
    snprintf(m.text, sizeof(m.text), "three hops");

    uint8_t   rec[512];
    const int n = om::mc::historyEncodeDm(m, 9, rec, (int)sizeof(rec));
    CHECK_EQ(n > 0, true);

    om::mc::DmMessage out[2];
    uint32_t          max_id = 0;
    CHECK_EQ(om::mc::historyLoadDm(rec, (size_t)n, pub, "fallback", out, 2, &max_id), 1u);
    CHECK_EQ(out[0].hops_known, true);
    CHECK_EQ(out[0].hops, 3);
    CHECK_EQ(out[0].hash_size, 1);
    CHECK_EQ(out[0].path_bytes, 3);
    CHECK(memcmp(out[0].path, path, sizeof(path)) == 0);
    CHECK_EQ(out[0].frame_len, 0);
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
    test_dm_record_round_trip();           // S513/EST-434
    test_dm_delivery_update();             // S513/EST-439
    test_real_path_round_trip();           // S515/EST-462
    test_v57_fake_record_reads_unknown_hops();
    test_dm_real_path_round_trip();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
