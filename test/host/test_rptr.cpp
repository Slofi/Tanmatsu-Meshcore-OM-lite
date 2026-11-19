// SPDX-License-Identifier: MIT
// Host tests for RPTR part 1 (S514/EST-453). The expected frames are NOT produced by this code: they came from
// an independent Python oracle (PyNaCl X25519 + cryptography AES-128-ECB + hashlib/hmac), run 2026-11-19:
//
//   self = 00..1f, A = 20..3f, secret = X25519(sk(self), pk(A))
//   seal = HMAC-SHA256(secret, ct)[:2] + AES-128-ECB(secret[:16]) over the zero-padded plaintext
//   scan        = b"\x2e\x00\x80\x04" + LE32(0x11223344) + LE32(0)
//   login flood = b"\x1d\x40" + A.pub[0] + self.pub + seal(secret, LE32(1750000000)+b"s3cret")
//   login dir   = b"\x1e\x41\xab\xcd" + A.pub[0] + self.pub + seal(...)
//   status req  = b"\x01\x40" + A.pub[0] + self.pub[0] + seal(secret, LE32(0x55667788)+b"\x01"+b"\0"*4+LE32(0xaabbccdd))
//
// The reply frames below were built by the SAME oracle from the layouts in rptr_wire.h; the parsers must
// recover every field. Then a junk pass feeds arbitrary bytes straight into the parsers (also under ASan).
#include <stdio.h>
#include <string.h>

#include "mc_identity.h"
#include "mc_wire.h"
#include "rptr_model.h"
#include "rptr_wire.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

#define CHECK_EQ(got, want)                                                        \
    do {                                                                           \
        g_checks++;                                                                \
        const long g_ = (long)(got), w_ = (long)(want);                            \
        if (g_ != w_) {                                                            \
            g_failures++;                                                          \
            printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__, \
                   #got, #want, g_, w_);                                           \
        }                                                                          \
    } while (0)

#define CHECK_STR(got, want)                                                       \
    do {                                                                           \
        g_checks++;                                                                \
        if (strcmp((got), (want)) != 0) {                                          \
            g_failures++;                                                          \
            printf("FAIL %s:%d  got \"%s\" want \"%s\"\n", __FILE__, __LINE__, (got), \
                   (want));                                                        \
        }                                                                          \
    } while (0)

size_t fromHex(uint8_t* out, size_t out_size, const char* hex) {
    const size_t n = strlen(hex) / 2;
    if (n > out_size) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        if (sscanf(&hex[2 * i], "%2x", &v) != 1) return 0;
        out[i] = (uint8_t)v;
    }
    return n;
}

const char* kSelfPub = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
const char* kAPub = "29acbae141bccaf0b22e1a94d34d0bc7361e526d0bfe12c89794bc9322966dd7";
const char* kSecret = "f6f92efb32945aff683324a1c984c5001f46aaea513f3453138d740b3a604b7d";
const char* kScan = "2e0080044433221100000000";
const char* kLoginFlood =
    "1d402903a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8f2064e35f37904c5aedb666f68099c6444a9";
const char* kLoginDirect =
    "1e41abcd2903a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8f2064e35f37904c5aedb666f68099c6444a9";
const char* kStatusReq = "01402903ab505b407f6713eb8db86bf8cfeadcb07b74";
const char* kLoginReply = "05400329e0cab8197bf8aaa60e49a47bb4270beed44e";
const char* kStatusReply =
    "054003297cfcb3e931bc94bb85959c0ea873f9f834fe45cb674dcdb0001fb219a2a66bdf655e37e91ab97cde76e657a3cfeabaff641d8bcd96ba498e6a91428cbdb55411563f";

meshcore::Identity g_self;
uint8_t g_a[32];
uint8_t g_secret[32];

void load() {
    uint8_t seed[32];
    for (size_t i = 0; i < sizeof(seed); i++) seed[i] = (uint8_t)i;
    CHECK(meshcore::identityFromSeed(g_self, seed));
    CHECK(fromHex(g_a, sizeof(g_a), kAPub) == 32);
    CHECK(meshcore::identitySharedSecret(g_self, g_a, g_secret));
}

bool frameEquals(const uint8_t* frame, size_t len, const char* hex) {
    uint8_t want[128];
    const size_t n = fromHex(want, sizeof(want), hex);
    return n == len && memcmp(frame, want, n) == 0;
}

// ------------------------------------------------------------------------------------------------- tests
void testScanFrame() {
    uint8_t frame[32];
    const size_t n = om::rptr::buildScan(0x11223344u, frame, sizeof(frame));
    CHECK_EQ(n, 12);
    CHECK(frameEquals(frame, n, kScan));
    CHECK_EQ(om::rptr::buildScan(1, frame, 11), 0);   // too small: no partial frame
}

void testLoginFrames() {
    uint8_t frame[128];
    size_t n = om::rptr::buildLogin(g_self, g_a, g_secret, 1750000000u, "s3cret", nullptr, 0, frame,
                                    sizeof(frame));
    CHECK_EQ(n, 53);
    CHECK(frameEquals(frame, n, kLoginFlood));
    const uint8_t path[2] = {0xAB, 0xCD};
    n = om::rptr::buildLogin(g_self, g_a, g_secret, 1750000000u, "s3cret", path, 0x41, frame, sizeof(frame));
    CHECK(frameEquals(frame, n, kLoginDirect));
    // An empty password never goes on the air; an over-long one is cut at 15, as upstream does.
    CHECK_EQ(om::rptr::buildLogin(g_self, g_a, g_secret, 1, "", nullptr, 0, frame, sizeof(frame)), 0);
    n = om::rptr::buildLogin(g_self, g_a, g_secret, 1, "0123456789abcdefghij", nullptr, 0, frame,
                             sizeof(frame));
    CHECK_EQ(n, 2 + 1 + 32 + 32 + 2);   // 4+15 bytes rounds to 32 bytes of ciphertext
}

void testStatusRequestFrame() {
    uint8_t frame[64];
    const size_t n = om::rptr::buildStatusRequest(g_self, g_a, g_secret, 0x55667788u, 0xAABBCCDDu, nullptr,
                                                  0, frame, sizeof(frame));
    CHECK_EQ(n, 22);
    CHECK(frameEquals(frame, n, kStatusReq));
}

void testScanAnswer() {
    // [0x90|type 2][snr in x4][tag][full key]
    uint8_t frame[2 + 6 + 32];
    meshcore::View v;
    frame[0] = 0x2E;
    frame[1] = 0x00;
    frame[2] = 0x92;
    frame[3] = 0x1B;   // 27 x4 = 6.75 dB
    frame[4] = 0x44;
    frame[5] = 0x33;
    frame[6] = 0x22;
    frame[7] = 0x11;
    memcpy(&frame[8], g_a, 32);
    CHECK(meshcore::decode(frame, sizeof(frame), &v));
    om::rptr::Found f;
    CHECK(om::rptr::parseScanAnswer(v, 0x11223344u, 0x24, &f));
    CHECK(memcmp(f.prefix, g_a, 6) == 0);
    CHECK_EQ(f.keyLen, 32);
    CHECK(memcmp(f.key, g_a, 32) == 0);
    CHECK_EQ(f.snrIn4, 27);
    CHECK_EQ(f.snrOut4, 36);
    CHECK_EQ(f.hops, 0);
    CHECK(!om::rptr::parseScanAnswer(v, 0x11223345u, 0, &f));   // another scan
    // A non-repeater answer is not ours.
    frame[2] = 0x91;
    CHECK(meshcore::decode(frame, sizeof(frame), &v));
    CHECK(!om::rptr::parseScanAnswer(v, 0x11223344u, 0, &f));
}

void testLoginReply() {
    uint8_t raw[128];
    const size_t n = fromHex(raw, sizeof(raw), kLoginReply);
    meshcore::View v;
    CHECK(meshcore::decode(raw, n, &v));
    uint8_t plain[128];
    size_t plain_len = 0;
    CHECK(om::rptr::openResponse(g_self, g_a, g_secret, v, plain, sizeof(plain), &plain_len));
    om::rptr::LoginAnswer a;
    CHECK(om::rptr::parseLoginAnswer(plain, plain_len, &a));
    CHECK(a.ok);
    CHECK(!a.legacy);
    CHECK(a.admin);
    CHECK_EQ(a.permissions, 3);
    CHECK_EQ(a.fwLevel, 7);
    CHECK_EQ(a.clock, 1750000100u);
    CHECK(a.clockValid);
    // Another secret must not open it.
    uint8_t other[32] = {0};
    CHECK(!om::rptr::openResponse(g_self, g_a, other, v, plain, sizeof(plain), &plain_len));
}

void testStatusReply() {
    uint8_t raw[256];
    const size_t n = fromHex(raw, sizeof(raw), kStatusReply);
    meshcore::View v;
    CHECK(meshcore::decode(raw, n, &v));
    uint8_t plain[256];
    size_t plain_len = 0;
    CHECK(om::rptr::openResponse(g_self, g_a, g_secret, v, plain, sizeof(plain), &plain_len));
    om::rptr::Status st;
    CHECK(om::rptr::parseStatusAnswer(plain, plain_len, &st));
    CHECK(st.valid);
    CHECK_EQ(st.battMv, 4112);
    CHECK_EQ(st.txQueue, 2);
    CHECK_EQ(st.noiseFloor, -117);
    CHECK_EQ(st.lastRssi, -83);
    CHECK_EQ(st.rxPackets, 123456u);
    CHECK_EQ(st.txPackets, 7890u);
    CHECK_EQ(st.airtimeSec, 3600u);
    CHECK_EQ(st.uptimeSec, 90061u);
    CHECK_EQ(st.sentFlood, 11u);
    CHECK_EQ(st.sentDirect, 22u);
    CHECK_EQ(st.recvFlood, 33u);
    CHECK_EQ(st.recvDirect, 44u);
    CHECK_EQ(st.errEvents, 5);
    CHECK_EQ(st.lastSnr4, 33);
    CHECK_EQ(st.directDups, 6);
    CHECK_EQ(st.floodDups, 7);
    CHECK_EQ(st.rxAirtimeSec, 7200u);
    CHECK_EQ(st.rxCrcErrors, 2u);
}

void testFoundList() {
    om::rptr::FoundList l;
    l.begin(0xAABBCCDDu, 1000);
    CHECK_EQ(l.count, 0);
    CHECK(l.listening);
    CHECK_EQ(l.addAnswer(g_a, 32, "", false, true, 12, true, 4, 0, 1100), 0);
    CHECK_EQ(l.addAnswer(g_a, 32, "TEST-RPT", true, true, 16, true, 8, 1, 1200), 0);   // same row
    CHECK_EQ(l.count, 1);
    CHECK_STR(l.rows[0].name, "TEST-RPT");
    CHECK(l.rows[0].inContacts);
    CHECK_EQ(l.rows[0].snrIn4, 16);
    CHECK_EQ(l.rows[0].hops, 1);
    // A prefix-only answer for a new key keeps only the bytes it carried.
    const uint8_t p2[6] = {9, 8, 7, 6, 5, 4};
    CHECK_EQ(l.addAnswer(p2, 6, nullptr, false, false, 0, false, 0, 2, 1300), 1);
    CHECK_EQ(l.rows[1].keyLen, 6);
    // The window closes exactly at kScanWindowMs.
    l.tick(1000 + om::rptr::kScanWindowMs - 1);
    CHECK(l.listening);
    l.tick(1000 + om::rptr::kScanWindowMs);
    CHECK(!l.listening);
    char text[32];
    l.stateText(text, sizeof(text));
    CHECK_STR(text, "2 found");
    // Full list refuses a 17th key rather than overwriting one.
    for (int i = 0; i < 20; i++) {
        uint8_t k[32] = {0};
        k[0] = (uint8_t)(100 + i);
        k[1] = 0xEE;
        l.addAnswer(k, 32, nullptr, false, false, 0, false, 0, 0, 1400);
    }
    CHECK_EQ(l.count, om::rptr::kMaxFound);
}

void testPassword() {
    char out[32];
    CHECK_EQ(om::rptr::passwordFilter("ab\tcd\x7f!", out, sizeof(out)), 5);
    CHECK_STR(out, "abcd!");
    CHECK_EQ(om::rptr::passwordFilter("0123456789abcdefgh", out, sizeof(out)), 15);
    CHECK_STR(out, "0123456789abcde");
}

void testLoginMachine() {
    om::rptr::LoginMachine m;
    m.begin(5000, 3000);
    CHECK(m.busy());
    CHECK_EQ(m.deadlineMs, 5000u + 3000u + om::rptr::kLoginExtraMs);
    CHECK(!m.tick(m.deadlineMs - 1));
    CHECK(m.tick(m.deadlineMs));
    CHECK(m.state == om::rptr::LoginState::NoAnswer);
    CHECK(!m.tick(m.deadlineMs + 100));
    om::rptr::LoginAnswer a;
    a.ok = true;
    a.admin = true;
    m.begin(100, 0);
    m.succeed(a, 350);
    CHECK(m.state == om::rptr::LoginState::Admin);
    CHECK_EQ(m.rttMs, 250);
    CHECK(m.loggedIn());
    m.reset();
    CHECK(m.state == om::rptr::LoginState::Idle);
    // A refused answer: a reply that arrived but was not a login.
    om::rptr::LoginAnswer no;
    m.begin(0, 10);
    m.succeed(no, 20);
    CHECK(m.state == om::rptr::LoginState::Refused);
    CHECK_EQ(om::rptr::requestDeadlineMs(700, 2500, 8000), 8000u);
    CHECK_EQ(om::rptr::requestDeadlineMs(7000, 2500, 8000), 9500u);
}

void testFormatting() {
    char b[32];
    om::rptr::uptimeText(b, sizeof(b), 90061);
    CHECK_STR(b, "1d 01:01");
    om::rptr::uptimeText(b, sizeof(b), 4 * 3600 + 2 * 60);
    CHECK_STR(b, "4h 02m");
    om::rptr::uptimeText(b, sizeof(b), 12 * 60 + 30);
    CHECK_STR(b, "12m 30s");
    om::rptr::batteryVoltsText(b, sizeof(b), 4112);
    CHECK_STR(b, "4.11 V");
    om::rptr::batteryVoltsText(b, sizeof(b), 0);
    CHECK_STR(b, "n/a");
    CHECK_EQ(om::rptr::batteryPercent(4112), 92);
    CHECK_EQ(om::rptr::batteryPercent(0), -1);
    om::rptr::snrText(b, sizeof(b), 33);
    CHECK_STR(b, "8.25 dB");
    om::rptr::snrText(b, sizeof(b), -7);
    CHECK_STR(b, "-1.75 dB");
}

// --------------------------------------------------------------------------------------------- junk pass
void testJunk() {
    uint32_t s = 0x12345678u;
    auto next = [&]() {
        s = s * 1664525u + 1013904223u;
        return (uint8_t)(s >> 24);
    };
    for (int round = 0; round < 4000; round++) {
        uint8_t buf[200];
        const size_t len = (size_t)(next() % sizeof(buf)) + 1;
        for (size_t i = 0; i < len; i++) buf[i] = next();
        meshcore::View v;
        if (!meshcore::decode(buf, len, &v)) continue;
        om::rptr::Found f;
        om::rptr::parseScanAnswer(v, 0x11223344u, 0, &f);
        uint8_t plain[200];
        size_t plain_len = 0;
        om::rptr::openResponse(g_self, g_a, g_secret, v, plain, sizeof(plain), &plain_len);
        om::rptr::LoginAnswer a;
        if (plain_len > 0) om::rptr::parseLoginAnswer(plain, plain_len, &a);
        om::rptr::Status st;
        if (plain_len > 0) om::rptr::parseStatusAnswer(plain, plain_len, &st);
    }
    CHECK(true);   // reaching here without a sanitizer report is the test
}

}  // namespace

int main() {
    load();
    char got[65];
    static const char* d = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        got[2 * i] = d[g_self.pub[i] >> 4];
        got[2 * i + 1] = d[g_self.pub[i] & 0xF];
    }
    got[64] = '\0';
    CHECK_STR(got, kSelfPub);
    uint8_t want_secret[32];
    fromHex(want_secret, sizeof(want_secret), kSecret);
    CHECK(memcmp(g_secret, want_secret, 32) == 0);

    testScanFrame();
    testLoginFrames();
    testStatusRequestFrame();
    testScanAnswer();
    testLoginReply();
    testStatusReply();
    testFoundList();
    testPassword();
    testLoginMachine();
    testFormatting();
    testJunk();

    printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
