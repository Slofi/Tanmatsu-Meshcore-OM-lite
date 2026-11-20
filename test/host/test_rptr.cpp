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

#include "mc_dm.h"        // EST-470: the CLI answer IS a TXT_MSG, so the DM codec opens it too
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

// ------------------------------------------------------- EST-470: the CLI path, against the SAME oracle
// Both frames and answers below are the oracle's output (PyNaCl/libsodium X25519 + cryptography AES-128-ECB +
// hmac/hashlib), built from the layouts in rptr_wire.h - never from this port. Run 2026-11-19 for the ticket:
//
//   cli flood  "set tx 20"   ts 1750000000 -> header 0x09, path_len 0x40, A.pub[0] 03.pub[0] seal(...)
//   cli direct "set tx 20"   the same with header 0x0a, path_len 0x41, path ab cd
//   answer     ts|0x04|text  header 0x09, path_len 0x40, OUR pub[0] then A.pub[0]
const char* kCliSetTx = "0940290338839611b685d258dc60576dfc796605357f";
const char* kCliSetTxDirect = "0a41abcd290338839611b685d258dc60576dfc796605357f";
const char* kCliClockSync = "09402903a351995eb891fdb374e43ebf11865a9fa8a6";
const char* kCliGetName = "09402903bf808de9101139a6d8f71d0346e23aa5b662";
const char* kAnsOk = "09400329615ebb633ed362d194685ff4be6b0790832c";
const char* kAnsName = "09400329ed9f392f77fab77b79d82e609ede29df7c3b61a9cb219b7ac620fc24afd38d1b8ef3";
const char* kAnsTx = "09400329b1680c0a7418692f8350862c824eb2575881";
const char* kAnsOn = "094003291c45d5f74d267951353aab36c8b2ab0f52be36849e1fa575e02073877097c3692687";
const char* kAnsErrClock = "094003294ad5016f6ab374dfc3088eb25e38641c2ca9869ab4adb700606adcb0f65014ef6d192f0b76db4ff0c6eccf2bf28627efd696";
const char* kAnsLat = "094003291157f214043495b323957a1fc5ca380d1cfb";
const char* kAnsLon = "094003298e75ffb7a124b3b87b78edd31d93685c90de";
const char* kAnsRepeat = "09400329f8f9413f524fe619c7dda3c22ea52336b2ca";
const char* kAnsOwner = "09400329af8c67763d8ff2d36f2341af3cfc80791b44b4d7b1aa02667c0deae34af76081ecbd";
const char* kAnsAdvertLocal = "094003294fa212d2ef6817d98ed49b0ff93be8c00cc95ef677ec6a2800ce81937635c7bc9f4a";
const char* kAnsAdvertFlood = "0940032912b57a706f75288782ac03c88615e39819021dd5576b9ed389ee0b62f53b2e39d253";
// ts|0x00|"hello": a PLAIN text from the SAME repeater. It must reach the DM code, never the CLI queue.
const char* kAnsPlain = "09400329441f5c30ec73ec1209eed5ac42f12ded7a96";

// One answer frame -> the value its row should hold, through openCliAnswer + parseCliReply + settingsApplyRead.
bool readAnswer(const char* hex, const char* read_cmd, om::rptr::Settings* s) {
    uint8_t raw[256];
    const size_t n = fromHex(raw, sizeof(raw), hex);
    if (n == 0) return false;
    meshcore::View v;
    if (!meshcore::decode(raw, n, &v)) return false;
    uint8_t plain[256];
    size_t plain_len = 0;
    if (!om::rptr::openCliAnswer(g_self, g_a, g_secret, v, plain, sizeof(plain), &plain_len)) return false;
    om::rptr::CliReply r;
    if (!om::rptr::parseCliReply(plain, plain_len, &r)) return false;
    return om::rptr::settingsApplyRead(s, read_cmd, r.text);
}

void testCliFrames() {
    const uint8_t path_bad[2] = {0x01, 0x02};
    uint8_t frame[192];
    size_t n = om::rptr::buildCli(g_self, g_a, g_secret, 1750000000u, "set tx 20", nullptr, 0, frame,
                                  sizeof(frame));
    CHECK_EQ(n, 22);
    CHECK(frameEquals(frame, n, kCliSetTx));
    // The SAME line down the route learned at login: DIRECT, one 1-byte hop (0x41), path copied verbatim.
    // A path_len that is not a legal route (hash size 4 is reserved, Packet.cpp:13-18) is refused by the
    // DIRECT branch itself: no partial frame, nothing queued.
    CHECK_EQ(om::rptr::buildCli(g_self, g_a, g_secret, 1750000000u, "set tx 20", path_bad, 0xC0, frame,
                                sizeof(frame)), 0);
    const uint8_t path[2] = {0xAB, 0xCD};
    n = om::rptr::buildCli(g_self, g_a, g_secret, 1750000000u, "set tx 20", path, 0x41, frame, sizeof(frame));
    CHECK(frameEquals(frame, n, kCliSetTxDirect));

    n = om::rptr::buildCli(g_self, g_a, g_secret, 1750000010u, "clock sync", nullptr, 0, frame, sizeof(frame));
    CHECK(frameEquals(frame, n, kCliClockSync));
    n = om::rptr::buildCli(g_self, g_a, g_secret, 1750000020u, "get name", nullptr, 0, frame, sizeof(frame));
    CHECK(frameEquals(frame, n, kCliGetName));

    // What never goes on the air: an empty line, and a line longer than MeshCore's MAX_TEXT_LEN.
    CHECK_EQ(om::rptr::buildCli(g_self, g_a, g_secret, 1, "", nullptr, 0, frame, sizeof(frame)), 0);
    char longline[200];
    memset(longline, 'x', sizeof(longline));
    longline[sizeof(longline) - 1] = '\0';
    CHECK_EQ(om::rptr::buildCli(g_self, g_a, g_secret, 1, longline, nullptr, 0, frame, sizeof(frame)), 0);
    // A buffer one byte short leaves nothing behind: no partial frame is ever queued.
    CHECK_EQ(om::rptr::buildCli(g_self, g_a, g_secret, 1, "set tx 20", nullptr, 0, frame, 21), 0);
}

// The answer is a NORMAL TXT_MSG: the DM codec opens it too (which is exactly why it would otherwise land in a
// DM thread), and only the CLI type check takes it to RPTR instead.
void testCliAnswerIsATxtMsg() {
    uint8_t raw[256];
    const size_t n = fromHex(raw, sizeof(raw), kAnsTx);
    meshcore::View v;
    CHECK(meshcore::decode(raw, n, &v));
    meshcore::DmMessage dm;
    CHECK(meshcore::dmOpen(g_self, v, g_a, 1, 0, &dm));
    CHECK_EQ(dm.txt_type, om::rptr::kTxtTypeCliData);
    CHECK_STR(dm.text, "> 20");
    CHECK_EQ(dm.timestamp, 1750000023u);

    // The ROUTING rule mc_link.cpp implements, in the same order: RPTR first, the DM code only if that refuses.
    enum class Where { Rptr, Dm, Dropped };
    auto route = [&](const char* hex) {
        uint8_t buf[256];
        const size_t len = fromHex(buf, sizeof(buf), hex);
        meshcore::View p;
        if (!meshcore::decode(buf, len, &p)) return Where::Dropped;
        uint8_t plain[256];
        size_t plain_len = 0;
        if (om::rptr::openCliAnswer(g_self, g_a, g_secret, p, plain, sizeof(plain), &plain_len)) {
            om::rptr::CliReply r;
            if (om::rptr::parseCliReply(plain, plain_len, &r)) return Where::Rptr;
        }
        meshcore::DmMessage m;
        if (meshcore::dmOpen(g_self, p, g_a, 1, 0, &m)) return Where::Dm;
        return Where::Dropped;
    };
    CHECK(route(kAnsTx) == Where::Rptr);              // a CLI answer from the login target: RPTR, never a DM
    CHECK(route(kAnsOk) == Where::Rptr);
    CHECK(route(kAnsPlain) == Where::Dm);             // a PLAIN text from that same node still reaches Chat
    CHECK(route(kCliSetTx) == Where::Dropped);        // our own CLI frame is addressed to the repeater, not to us

    // A frame addressed to somebody else, or sourced by somebody else, is not ours.
    uint8_t other[256];
    memcpy(other, raw, n);
    other[2] ^= 0xFF;                                  // dest hash: no longer our pub_key[0]
    meshcore::View w;
    CHECK(meshcore::decode(other, n, &w));
    uint8_t plain[256];
    size_t plain_len = 0;
    CHECK(!om::rptr::openCliAnswer(g_self, g_a, g_secret, w, plain, sizeof(plain), &plain_len));
    memcpy(other, raw, n);
    other[3] ^= 0xFF;                                  // src hash: no longer the login target's
    CHECK(meshcore::decode(other, n, &w));
    CHECK(!om::rptr::openCliAnswer(g_self, g_a, g_secret, w, plain, sizeof(plain), &plain_len));
    // The wrong secret never opens it (a different repeater with the same first byte cannot answer for this one).
    uint8_t wrong[32] = {0};
    CHECK(meshcore::decode(raw, n, &w));
    CHECK(!om::rptr::openCliAnswer(g_self, g_a, wrong, w, plain, sizeof(plain), &plain_len));
}

void testCliAnswersPerCommand() {
    om::rptr::Settings s;
    // The read ladder, in the order the link sends it: one value per row, Position taking two lines.
    CHECK_STR(om::rptr::setReadCmd(0), om::rptr::kCliGetName);
    CHECK_STR(om::rptr::setReadCmd(1), om::rptr::kCliGetOwner);
    CHECK_STR(om::rptr::setReadCmd(2), om::rptr::kCliGetLat);
    CHECK_STR(om::rptr::setReadCmd(3), om::rptr::kCliGetLon);
    CHECK_STR(om::rptr::setReadCmd(4), om::rptr::kCliGetRepeat);
    CHECK_STR(om::rptr::setReadCmd(5), om::rptr::kCliPowerSavingRead);
    CHECK_STR(om::rptr::setReadCmd(6), om::rptr::kCliGetTx);
    CHECK(om::rptr::setReadCmd(7) == nullptr);
    CHECK_EQ(om::rptr::setReadRow(om::rptr::kCliGetLon), (int)om::rptr::SetRow::Position);
    CHECK_EQ(om::rptr::setReadRow("how are you"), -1);

    // Nothing read yet: every row says so instead of guessing.
    char v[64];
    for (int row = 0; row < (int)om::rptr::SetRow::TxPower + 1; row++)
        CHECK(!om::rptr::settingsRowValue(s, row, v, sizeof(v)));

    // Name. `get name` answers "> <name>" (CommonCLI.cpp:715-717).
    CHECK(readAnswer(kAnsName, om::rptr::kCliGetName, &s));
    CHECK(s.nameOk);
    CHECK_STR(s.name, "RPT-Test-A");
    CHECK(om::rptr::settingsRowValue(s, (int)om::rptr::SetRow::Name, v, sizeof(v)));
    CHECK_STR(v, "RPT-Test-A");

    // Owner info, with the repeater's '|' for the newline it stores (CommonCLI.cpp:733-745).
    CHECK(readAnswer(kAnsOwner, om::rptr::kCliGetOwner, &s));
    CHECK_STR(s.owner, "ops|TestBox");

    // Position: two answers, each landing on its own half.
    CHECK(readAnswer(kAnsLat, om::rptr::kCliGetLat, &s));
    CHECK(s.latOk);
    CHECK(!s.positionOk());
    CHECK(!om::rptr::settingsRowValue(s, (int)om::rptr::SetRow::Position, v, sizeof(v)));
    CHECK(readAnswer(kAnsLon, om::rptr::kCliGetLon, &s));
    CHECK(s.positionOk());
    CHECK_EQ(s.latE6, 46250000);
    CHECK_EQ(s.lonE6, 14500000);
    CHECK(om::rptr::settingsRowValue(s, (int)om::rptr::SetRow::Position, v, sizeof(v)));
    CHECK_STR(v, "46.25000, 14.50000");

    // Repeat, and Power save (whose answer is a bare "on - After 2 minutes" on the ESP32, CommonCLI.cpp:439-451).
    CHECK(readAnswer(kAnsRepeat, om::rptr::kCliGetRepeat, &s));
    CHECK(s.repeatOk);
    CHECK(!s.repeat);
    CHECK(readAnswer(kAnsOn, om::rptr::kCliPowerSavingRead, &s));
    CHECK(s.psOk);
    CHECK(s.powerSave);
    CHECK(om::rptr::settingsRowValue(s, (int)om::rptr::SetRow::PowerSave, v, sizeof(v)));
    CHECK_STR(v, "on");

    // TX power: "> 20" (CommonRadioPrefs.cpp:137-139).
    CHECK(readAnswer(kAnsTx, om::rptr::kCliGetTx, &s));
    CHECK_EQ(s.tx, 20);
    CHECK(om::rptr::settingsRowValue(s, (int)om::rptr::SetRow::TxPower, v, sizeof(v)));
    CHECK_STR(v, "20 dBm");

    // An answer to the WRONG line never becomes a value: "> 20" is not a coordinate and not an on/off.
    CHECK(!om::rptr::settingsApplyRead(&s, om::rptr::kCliGetLat, "> 200"));   // beyond 90 degrees
    CHECK(!om::rptr::settingsApplyRead(&s, om::rptr::kCliGetRepeat, "> 20"));
    // An error line and an unknown config are not values either, and leave the row as it was.
    CHECK(!om::rptr::settingsApplyRead(&s, om::rptr::kCliGetName, "Error, bad chars"));
    CHECK(!om::rptr::settingsApplyRead(&s, om::rptr::kCliGetTx, "??: tx"));
    CHECK_STR(s.name, "RPT-Test-A");
    CHECK_EQ(s.tx, 20);
    // reset(): a fresh login starts from nothing read.
    s.reset();
    CHECK(!om::rptr::settingsRowValue(s, (int)om::rptr::SetRow::Name, v, sizeof(v)));
}

// The action rows: their answer is a NOTICE, never a value. These three frames are the oracle's.
bool answerText(const char* hex, char* out, size_t cap) {
    uint8_t raw[256];
    const size_t n = fromHex(raw, sizeof(raw), hex);
    if (n == 0) return false;
    meshcore::View v;
    if (!meshcore::decode(raw, n, &v)) return false;
    uint8_t plain[256];
    size_t plain_len = 0;
    if (!om::rptr::openCliAnswer(g_self, g_a, g_secret, v, plain, sizeof(plain), &plain_len)) return false;
    om::rptr::CliReply r;
    if (!om::rptr::parseCliReply(plain, plain_len, &r)) return false;
    snprintf(out, cap, "%s", r.text);
    return true;
}

void testCliActionAnswers() {
    char text[192];
    CHECK(answerText(kAnsAdvertLocal, text, sizeof(text)));
    CHECK_STR(text, "OK - zerohop advert sent");
    CHECK(om::rptr::cliAnswerKind(text) == om::rptr::CliAnswer::Ok);
    CHECK(answerText(kAnsAdvertFlood, text, sizeof(text)));
    CHECK_STR(text, "OK - Advert sent");
    CHECK(om::rptr::cliAnswerKind(text) == om::rptr::CliAnswer::Ok);
    // `clock sync` is refused when our clock is behind the repeater's: the page must show THAT, not "OK".
    CHECK(answerText(kAnsErrClock, text, sizeof(text)));
    CHECK_STR(text, "ERR: clock cannot go backwards");
    CHECK(om::rptr::cliAnswerKind(text) == om::rptr::CliAnswer::Error);
    // A plain "OK" (every `set` that has nothing to add) parses as a CLI answer at the wire level too.
    CHECK(answerText(kAnsOk, text, sizeof(text)));
    CHECK_STR(text, "OK");
}

void testCliAnswerKinds() {
    CHECK(om::rptr::cliAnswerKind("OK") == om::rptr::CliAnswer::Ok);
    CHECK(om::rptr::cliAnswerKind("OK - Advert sent") == om::rptr::CliAnswer::Ok);
    CHECK(om::rptr::cliAnswerKind("OK - repeat is now OFF") == om::rptr::CliAnswer::Ok);
    CHECK(om::rptr::cliAnswerKind("ok") == om::rptr::CliAnswer::Ok);
    CHECK(om::rptr::cliAnswerKind("Error, bad chars") == om::rptr::CliAnswer::Error);
    CHECK(om::rptr::cliAnswerKind("ERR: clock cannot go backwards") == om::rptr::CliAnswer::Error);
    CHECK(om::rptr::cliAnswerKind("??: powersaving") == om::rptr::CliAnswer::Unknown);
    CHECK(om::rptr::cliAnswerKind("") == om::rptr::CliAnswer::Unknown);
    CHECK(om::rptr::cliAnswerKind(nullptr) == om::rptr::CliAnswer::Unknown);
}

void testCliLines() {
    char line[192];
    // Name: the 23-byte cap applies when the position is SHARED (32 - 9), 31 when it is not.
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "ABCDEFGHIJKLMNOPQRSTUVW", true),
             (int)strlen("set name ABCDEFGHIJKLMNOPQRSTUVW"));   // 23 chars
    CHECK_STR(line, "set name ABCDEFGHIJKLMNOPQRSTUVW");
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "ABCDEFGHIJKLMNOPQRSTUVWX", true), 0);        // 24 chars
    CHECK_STR(line, "");
    CHECK(om::rptr::cliName(line, sizeof(line), "ABCDEFGHIJKLMNOPQRSTUVWXY", false) > 0);       // 25, not shared
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", false), 0);  // 32 > 31
    // The characters the repeater's own isValidName refuses, and nothing outside printable ASCII.
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a[b", true), 0);
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a]b", true), 0);
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a\\b", true), 0);
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a:b", true), 0);
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a,b", true), 0);
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a?b", true), 0);
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a*b", true), 0);
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a\tb", true), 0);
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "a\xc3\xa9", true), 0);   // UTF-8: 2 bytes, both > 0x7E
    CHECK_EQ(om::rptr::cliName(line, sizeof(line), "", true), 0);

    // Owner: empty refused; a newline becomes the repeater's '|' so the round trip is lossless.
    CHECK_EQ(om::rptr::cliOwner(line, sizeof(line), ""), 0);
    CHECK_EQ(om::rptr::cliOwner(line, sizeof(line), "ops\nTestBox"),
             (int)strlen("set owner.info ops|TestBox"));
    CHECK_STR(line, "set owner.info ops|TestBox");

    // Position: five decimals, and the two lines joined exactly as cliRadioApply joins its two.
    CHECK_EQ(om::rptr::cliLat(line, sizeof(line), 46250000), (int)strlen("set lat 46.25000"));
    CHECK_STR(line, "set lat 46.25000");
    CHECK_EQ(om::rptr::cliLon(line, sizeof(line), -14500000), (int)strlen("set lon -14.50000"));
    CHECK_STR(line, "set lon -14.50000");
    CHECK(om::rptr::cliPosition(line, sizeof(line), 46250000, 14500000) > 0);
    CHECK_STR(line, "set lat 46.25000 \xC2\xB7 set lon 14.50000");
    CHECK_EQ(om::rptr::cliLat(line, sizeof(line), 91000000), 0);     // beyond 90 degrees
    CHECK_EQ(om::rptr::cliLon(line, sizeof(line), 181000000), 0);    // beyond 180 degrees

    // + / - on TX power: clamped to 1..22, because upstream stores atoi() straight into an int8_t.
    CHECK_EQ(om::rptr::cliTx(line, sizeof(line), 0), (int)strlen("set tx 1"));
    CHECK_STR(line, "set tx 1");
    CHECK_EQ(om::rptr::cliTx(line, sizeof(line), 23), (int)strlen("set tx 22"));
    CHECK_STR(line, "set tx 22");
    CHECK_EQ(om::rptr::cliTx(line, sizeof(line), 20), (int)strlen("set tx 20"));
    CHECK_STR(line, "set tx 20");

    // Repeat and power save: the exact lines the repeater's parser matches ("powersaving" has NO "set " prefix).
    CHECK_EQ(om::rptr::cliRepeat(line, sizeof(line), false), (int)strlen("set repeat off"));
    CHECK_STR(line, "set repeat off");
    CHECK_EQ(om::rptr::cliPowerSaving(line, sizeof(line), true), (int)strlen("powersaving on"));
    CHECK_STR(line, "powersaving on");
    CHECK_EQ(om::rptr::cliPowerSaving(line, sizeof(line), false), (int)strlen("powersaving off"));
    CHECK_STR(line, "powersaving off");

    // The action rows: one line each, and a reboot that says so.
    CHECK_STR(om::rptr::setRowActionCmd((int)om::rptr::SetRow::AdvertLocal), "advert.zerohop");
    CHECK_STR(om::rptr::setRowActionCmd((int)om::rptr::SetRow::AdvertFlood), "advert");
    CHECK_STR(om::rptr::setRowActionCmd((int)om::rptr::SetRow::ClockSync), "clock sync");
    CHECK_STR(om::rptr::setRowActionCmd((int)om::rptr::SetRow::Reboot), "reboot");
    CHECK(om::rptr::setRowActionCmd((int)om::rptr::SetRow::Name) == nullptr);
}

void testCliTypedPosition() {
    int32_t lat = 0, lon = 0;
    CHECK(om::rptr::cliParseTypedPosition("46.25,14.5", &lat, &lon));
    CHECK_EQ(lat, 46250000);
    CHECK_EQ(lon, 14500000);
    CHECK(om::rptr::cliParseTypedPosition("46.25, 14.5", &lat, &lon));       // one space after the comma
    CHECK(om::rptr::cliParseTypedPosition(" -46.25 , -14.5 ", &lat, &lon));  // spaces a paste leaves behind
    CHECK_EQ(lat, -46250000);
    CHECK_EQ(lon, -14500000);
    // S516 (EST-473): more decimals than the CLI's 5-decimal form. The 7th digit is ROUNDED away, never truncated
    // (core/rptr_model.cpp:629-637), so "46.1234567" is 46.123457 and not 46.123456.
    CHECK(om::rptr::cliParseTypedPosition("46.1234567, 14.4", &lat, &lon));
    CHECK_EQ(lat, 46123457);
    CHECK_EQ(lon, 14400000);
    CHECK(!om::rptr::cliParseTypedPosition("46.25", &lat, &lon));       // no comma
    CHECK(!om::rptr::cliParseTypedPosition("46.25,", &lat, &lon));      // empty half
    CHECK(!om::rptr::cliParseTypedPosition(",14.5", &lat, &lon));
    CHECK(!om::rptr::cliParseTypedPosition("95.0,14.5", &lat, &lon));   // latitude out of range
    CHECK(!om::rptr::cliParseTypedPosition("46.25,195.0", &lat, &lon));
    CHECK(!om::rptr::cliParseTypedPosition("abc,14.5", &lat, &lon));
    CHECK(!om::rptr::cliParseTypedPosition("", &lat, &lon));
}

void testConfirmWords() {
    char text[192];
    // Every change names the exact line, and the value the row shows now (EST-470 item 3).
    CHECK(om::rptr::confirmWriteText(text, sizeof(text), "set tx 20", "22 dBm") > 0);
    CHECK_STR(text, "Send \"set tx 20\"? The repeater says 22 dBm now.");
    CHECK(om::rptr::confirmWriteText(text, sizeof(text), "powersaving on", "off") > 0);
    CHECK_STR(text, "Send \"powersaving on\"? The repeater says off now.");
    // S516 (EST-473): NO value → the line alone. An ACTION row (advert, clock sync) has no current value at all,
    // and a row not read yet has an unknown one; "The repeater says nothing now" was neither.
    CHECK(om::rptr::confirmWriteText(text, sizeof(text), "set tx 20", "") > 0);
    CHECK_STR(text, "Send \"set tx 20\"?");   // a row not read yet
    CHECK(om::rptr::confirmWriteText(text, sizeof(text), "advert", nullptr) > 0);
    CHECK_STR(text, "Send \"advert\"?");
    CHECK(om::rptr::confirmWriteText(text, sizeof(text), "clock sync", "") > 0);
    CHECK_STR(text, "Send \"clock sync\"?");
    CHECK_EQ(om::rptr::confirmWriteText(text, sizeof(text), "", "22 dBm"), 0);
    CHECK_EQ(om::rptr::confirmWriteText(text, sizeof(text), nullptr, nullptr), 0);

    // Reboot: one confirm, with the exact sentence the ticket asks for.
    CHECK(om::rptr::confirmRebootText(text, sizeof(text), "TEST-RPT-A") > 0);
    CHECK_STR(text, "Reboot TEST-RPT-A? It is off the mesh for ~10 s.");
    CHECK(om::rptr::confirmRebootText(text, sizeof(text), "") > 0);
    CHECK_STR(text, "Reboot the repeater? It is off the mesh for ~10 s.");
}

void testCliPending() {
    // ONE line in flight: nothing else may be sent while it is used.
    om::rptr::CliPending p;
    CHECK(!p.used);
    CHECK(p.canSend());
    CHECK(p.tick(0) == om::rptr::CliEvent::Idle);
    p.begin("set tx 20", (uint8_t)om::rptr::SetRow::TxPower, false, 1000, 8000);
    CHECK(p.used);
    CHECK(!p.canSend());
    CHECK_EQ(p.row, (uint8_t)om::rptr::SetRow::TxPower);
    CHECK(!p.isRead);
    CHECK_STR(p.line, "set tx 20");
    CHECK(!p.late);
    CHECK(p.tick(8999) == om::rptr::CliEvent::Waiting);
    // S516 (EST-474): the deadline does NOT release the line. It opens ONE more deadline of the same length.
    CHECK(p.tick(9000) == om::rptr::CliEvent::TimedOut);   // the deadline is inclusive, exactly 8 s after it went out
    CHECK(p.late);
    CHECK(p.used);
    CHECK(!p.canSend());
    CHECK_EQ(p.deadlineMs, 17000u);
    CHECK(p.tick(16999) == om::rptr::CliEvent::Waiting);
    CHECK(p.tick(17000) == om::rptr::CliEvent::Closed);    // the window's end is inclusive too
    CHECK(!p.used);
    CHECK(!p.late);
    CHECK(p.canSend());
    CHECK_STR(p.line, "");
    // The 32-bit wrap is a signed difference, like every other window in this tree.
    om::rptr::CliPending q;
    q.begin("get tx", (uint8_t)om::rptr::SetRow::TxPower, true, 0xFFFFFF00u, 8000);
    CHECK(q.tick(0x1000u) == om::rptr::CliEvent::Waiting);   // 0xFFFFFF00 + 8000 wraps to 0x1E40
    CHECK(q.tick(0x2000u) == om::rptr::CliEvent::TimedOut);  // past the wrapped deadline
    CHECK_EQ(q.deadlineMs, 0x3D80u);                         // 0x1E40 + 8000, one more deadline
    CHECK(q.tick(0x3000u) == om::rptr::CliEvent::Waiting);
    CHECK(q.tick(0x4000u) == om::rptr::CliEvent::Closed);
    CHECK(om::rptr::setRowIsText((int)om::rptr::SetRow::Position));
    CHECK(!om::rptr::setRowIsText((int)om::rptr::SetRow::Repeat));
    CHECK(om::rptr::setRowIsToggle((int)om::rptr::SetRow::PowerSave));
    CHECK(om::rptr::setRowIsStepper((int)om::rptr::SetRow::TxPower));
    CHECK(om::rptr::setRowIsAction((int)om::rptr::SetRow::Reboot));
    CHECK_STR(om::rptr::setRowLabel((int)om::rptr::SetRow::Owner), "Owner info");
    CHECK_STR(om::rptr::setRowLabel(99), "");
}

// S516 (EST-474): a repeater's LATE answer must not fill the wrong row. Every instant below is exact, and the
// routing is the shell's own: `settingsApplyRead(&s, p.line, reply)` is the call onCliAnswer makes with the slot
// that holds the line, so these cases pin the real code path, not a copy of it.
void testLateAnswerWindow() {
    using om::rptr::CliEvent;
    using om::rptr::CliPending;
    using om::rptr::SetRow;

    // (1) THE CASE EST-473 § 6a FOUND: "get tx" times out, its answer "> 14" arrives INSIDE the window. It fills TX
    // power, Position is untouched, and only then may the held "get lat" go out.
    {
        CliPending p;
        om::rptr::Settings s;
        p.begin("get tx", (uint8_t)SetRow::TxPower, true, 1000, 8000);
        CHECK(p.tick(9000) == CliEvent::TimedOut);
        CHECK(!p.canSend());   // "get lat" is HELD: the slot is still the timed-out line's
        CHECK(p.used);
        CHECK(p.late);
        CHECK_STR(p.line, "get tx");
        CHECK_EQ(p.row, (uint8_t)SetRow::TxPower);
        CHECK(p.isRead);
        // The slow answer, at 12000, is still inside the window -> the TIMED-OUT line's row.
        CHECK(om::rptr::settingsApplyRead(&s, p.line, "> 14"));
        CHECK(s.txOk);
        CHECK_EQ(s.tx, 14);
        CHECK(!s.latOk);   // Position untouched
        CHECK(!s.lonOk);
        CHECK(p.tick(12000) == CliEvent::Waiting);   // the window is still open after the answer arrived
        p.clear();                                    // what onCliAnswer does with the line it answered
        CHECK(p.canSend());                           // and only now ...
        p.begin("get lat", (uint8_t)SetRow::Position, true, 12000, 8000);   // ... the held line goes out
        CHECK_STR(p.line, "get lat");
        CHECK_EQ(p.sentMs, 12000u);
        CHECK_EQ(p.deadlineMs, 20000u);   // its OWN deadline starts when it is sent, never at the old window
    }
    // (1b) a late answer to a CHANGE still knows it is a change: the shell shows its notice and re-reads the row.
    {
        CliPending p;
        p.begin("set tx 20", (uint8_t)SetRow::TxPower, false, 1000, 8000);
        CHECK(p.tick(9000) == CliEvent::TimedOut);
        CHECK(!p.isRead);
        CHECK_STR(p.line, "set tx 20");
        CHECK_EQ(p.row, (uint8_t)SetRow::TxPower);
        p.clear();
    }

    // (2) the window closes with NO answer: the held line goes at the window's end and the timed-out row stays
    // "no answer" (nothing applied, still unread).
    {
        CliPending p;
        om::rptr::Settings s;
        p.begin("get tx", (uint8_t)SetRow::TxPower, true, 1000, 8000);
        CHECK(p.tick(9000) == CliEvent::TimedOut);
        CHECK(p.tick(12000) == CliEvent::Waiting);
        CHECK(p.tick(16999) == CliEvent::Waiting);
        CHECK(!p.canSend());
        CHECK(p.tick(17000) == CliEvent::Closed);   // exactly one more deadline, then release
        CHECK(!p.used);
        CHECK(p.canSend());                          // the held "get lat" may go NOW
        CHECK(!s.txOk);                              // the row keeps "no answer" (nothing was applied)
        CHECK(!s.latOk);
        CHECK_STR(p.line, "");
        p.begin("get lat", (uint8_t)SetRow::Position, true, 17000, 8000);
        CHECK_STR(p.line, "get lat");
    }

    // (3) an answer AFTER the window, while the held line is in flight, is the HELD line's (unchanged behaviour).
    {
        CliPending p;
        om::rptr::Settings s;
        p.begin("get tx", (uint8_t)SetRow::TxPower, true, 1000, 8000);
        CHECK(p.tick(9000) == CliEvent::TimedOut);
        CHECK(p.tick(17000) == CliEvent::Closed);
        p.begin("get lat", (uint8_t)SetRow::Position, true, 17000, 8000);
        CHECK_STR(p.line, "get lat");
        CHECK(om::rptr::settingsApplyRead(&s, p.line, "> 46.25000"));   // the held line's answer
        CHECK(s.latOk);
        CHECK_EQ(s.latE6, 46250000);
        CHECK(!s.txOk);   // TX's row keeps saying nothing
    }

    // (4) logout / leaving the repeater clears the window: no stale answer can land in a later session.
    {
        CliPending p;
        p.begin("get tx", (uint8_t)SetRow::TxPower, true, 1000, 8000);
        CHECK(p.tick(9000) == CliEvent::TimedOut);
        CHECK(p.late);
        p.clear();   // logout()
        CHECK(!p.used);
        CHECK(!p.late);
        CHECK(p.canSend());
        CHECK_STR(p.line, "");
        CHECK(p.tick(9000) == CliEvent::Idle);
        p.begin("get name", (uint8_t)SetRow::Name, true, 9000, 8000);   // a fresh session, a fresh deadline
        CHECK_EQ(p.deadlineMs, 17000u);
        CHECK(p.tick(17000) == CliEvent::TimedOut);
    }

    // (5) TWO timeouts in a row: one window per timed-out line, never stacked, and each keeps that line's identity.
    {
        CliPending p;
        om::rptr::Settings s;
        p.begin("get tx", (uint8_t)SetRow::TxPower, true, 1000, 8000);
        CHECK(p.tick(9000) == CliEvent::TimedOut);
        CHECK(p.tick(17000) == CliEvent::Closed);
        p.begin("get lat", (uint8_t)SetRow::Position, true, 17000, 8000);   // the held line
        CHECK(p.tick(25000) == CliEvent::TimedOut);                          // its OWN deadline
        CHECK_EQ(p.deadlineMs, 33000u);                                      // ONE estimate, never two stacked
        CHECK(p.late);
        CHECK_STR(p.line, "get lat");
        CHECK_EQ(p.row, (uint8_t)SetRow::Position);
        CHECK(om::rptr::settingsApplyRead(&s, p.line, "> 46.25000"));        // the SECOND line's own answer
        CHECK(s.latOk);
        CHECK(!s.txOk);   // the first line's row is still the untrusted one
        p.clear();
        CHECK(p.canSend());
        CHECK(p.tick(33000) == CliEvent::Idle);
    }
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
        // EST-470: the CLI path reads lengths straight off the air too (a TXT_MSG from any node may be offered to
        // openCliAnswer), and the answer's text is a C string built from those bytes.
        size_t cli_len = 0;
        if (om::rptr::openCliAnswer(g_self, g_a, g_secret, v, plain, sizeof(plain), &cli_len) && cli_len > 0) {
            om::rptr::CliReply reply;
            if (om::rptr::parseCliReply(plain, cli_len, &reply)) {
                om::rptr::Settings s2;
                (void)om::rptr::settingsApplyRead(&s2, om::rptr::kCliGetOwner, reply.text);
                (void)om::rptr::cliAnswerKind(reply.text);
            }
        }
        (void)om::rptr::parseCliReply(plain, plain_len, nullptr);
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
    testCliFrames();
    testCliAnswerIsATxtMsg();
    testCliAnswersPerCommand();
    testCliActionAnswers();
    testCliAnswerKinds();
    testCliLines();
    testCliTypedPosition();
    testConfirmWords();
    testCliPending();
    testLateAnswerWindow();
    testJunk();

    printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
