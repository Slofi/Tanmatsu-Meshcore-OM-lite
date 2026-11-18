// SPDX-License-Identifier: MIT
// Host tests for DM SEND (S513/EST-436). The expected bytes are NOT produced by this code: the two frames and the
// ACK crc below are the SAME independent Python oracle vectors test_dm.cpp already pins (test_dm.cpp:70-110), so
// this file compares a second implementation against the first oracle rather than against itself:
//
//   self = 00..1f, A = 29acba…, plain = struct.pack("<I", 1750000000) + bytes([(0<<2)|1]) + b"hello from A"
//   frame (flood)  = b"\x09\x40" + self.pub[0] + A.pub[0] + seal(shared(self, A))
//   frame (direct) = b"\x0a\x41\xab\xcd" + self.pub[0] + A.pub[0] + the SAME seal
//   ack            = sha256(plain + A.pub)[:4]
//
// So: composing TO A must reproduce the oracle's frame byte for byte, and the expected ACK must be the crc the
// oracle's recipient sends for it. A PATH round trip through dmBuildPathReturn proves the parser's framing and
// bounds; the retry rule is exercised at exact instants.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dm_send.h"
#include "mc_crypto.h"
#include "mc_dm.h"
#include "mc_identity.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

#define CHECK_STR(got, want)                                                          \
    do {                                                                              \
        g_checks++;                                                                   \
        if (strcmp((got), (want)) != 0) {                                             \
            g_failures++;                                                             \
            printf("FAIL %s:%d  got \"%s\" want \"%s\"\n", __FILE__, __LINE__, (got), \
                   (want));                                                           \
        }                                                                             \
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

void toHex(char* out, const uint8_t* in, size_t n) {
    static const char* d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 0x0F];
    }
    out[2 * n] = '\0';
}

// The oracle (test_dm.cpp:70-110).
const char* kSelfPub = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
const char* kAPub    = "29acbae141bccaf0b22e1a94d34d0bc7361e526d0bfe12c89794bc9322966dd7";
const char* kFrameA  = "09400329d27f2e02b4b8db86b854305ff1160486fbe161a9cb219b7ac620fc24afd38d1b8ef3";
const char* kFrameDirect = "0a41abcd0329d27f2e02b4b8db86b854305ff1160486fbe161a9cb219b7ac620fc24afd38d1b8ef3";
const char* kAck  = "f976f1cc005a";   // 4-byte crc + attempt byte + random
const char* kText = "hello from A";
constexpr uint32_t kTs = 1750000000;

meshcore::Identity g_self;
uint8_t             g_a[32];

void loadKeys() {
    uint8_t seed[32];
    for (size_t i = 0; i < sizeof(seed); i++) seed[i] = (uint8_t)i;
    CHECK(meshcore::identityFromSeed(g_self, seed));
    CHECK(fromHex(g_a, sizeof(g_a), kAPub) == 32);
    char hex[65];
    toHex(hex, g_self.pub, 32);
    CHECK_STR(hex, kSelfPub);
}

// 1. Compose a flood DM to A with attempt 1. The oracle's kFrameA is the SAME message arriving FROM A, so the
// frame we build must be its MIRROR: the sealed bytes are identical (same plaintext, same shared secret), while
// createDatagram puts OUR recipient's hash first (Mesh.cpp:500-503): dest A = 0x29, src self = 0x03.
void testComposeFlood() {
    uint8_t secret[32];
    CHECK(meshcore::identitySharedSecret(g_self, g_a, secret));

    meshcore::DmComposed c;
    CHECK(meshcore::dmCompose(g_self, g_a, secret, kTs, 1, kText, nullptr, 0, &c));
    CHECK(c.flood);
    CHECK(c.frame[0] == 0x09);   // TXT_MSG << 2 | FLOOD
    CHECK(c.frame[1] == 0x40);   // 2-byte path hashes, count 0
    CHECK(c.frame[2] == g_a[0]);         // dest hash = THEIR pub[0]
    CHECK(c.frame[3] == g_self.pub[0]);  // src hash  = OURS

    // The cryptographic half is the oracle's, byte for byte (kFrameA bytes 4.. are the seal).
    uint8_t oracle[256];
    const size_t on = fromHex(oracle, sizeof(oracle), kFrameA);
    CHECK(c.frame_len == on);
    CHECK(memcmp(&c.frame[6], &oracle[6], c.frame_len - 6) == 0);

    // expected_ack = sha256(ts | attempt | text || OUR pub)[:4] — the rule's INPUTS are the oracle's plaintext
    // (kFrameA decrypts to exactly these 5+11 bytes), and the key is the SENDER's, as BaseChatMesh.cpp:461-462.
    uint8_t plain[5 + 12];
    memset(plain, 0, sizeof(plain));
    plain[0] = (uint8_t)(kTs & 0xFF);
    plain[1] = (uint8_t)((kTs >> 8) & 0xFF);
    plain[2] = (uint8_t)((kTs >> 16) & 0xFF);
    plain[3] = (uint8_t)((kTs >> 24) & 0xFF);
    plain[4] = 1;   // attempt 1, PLAIN
    memcpy(&plain[5], kText, 12);
    uint8_t buf[5 + 12 + 32];
    memcpy(buf, plain, sizeof(plain));
    memcpy(buf + sizeof(plain), g_self.pub, 32);
    uint8_t full[32];
    CHECK(meshcore::sha256(buf, sizeof(buf), full));
    CHECK(memcmp(c.expected_ack, full, 4) == 0);

    // A different attempt hashes differently (the attempt is inside the ACK), but the frame length is the same.
    meshcore::DmComposed c0;
    CHECK(meshcore::dmCompose(g_self, g_a, secret, kTs, 0, kText, nullptr, 0, &c0));
    CHECK(memcmp(c0.expected_ack, c.expected_ack, 4) != 0);
    CHECK(c0.frame_len == c.frame_len);
}

// 2. A stored route: the SAME call becomes a DIRECT frame along it, mirroring the oracle's direct arrival.
void testComposeDirect() {
    uint8_t secret[32];
    CHECK(meshcore::identitySharedSecret(g_self, g_a, secret));
    static const uint8_t path[2] = {0xab, 0xcd};

    meshcore::DmComposed c;
    CHECK(meshcore::dmCompose(g_self, g_a, secret, kTs, 1, kText, path, 0x41, &c));
    CHECK(!c.flood);
    uint8_t oracle[256];
    const size_t on = fromHex(oracle, sizeof(oracle), kFrameDirect);
    CHECK(c.frame_len == on);
    CHECK(c.frame[0] == 0x0a);   // TXT_MSG << 2 | DIRECT
    CHECK(c.frame[1] == 0x41);
    CHECK(memcmp(&c.frame[2], path, 2) == 0);
    CHECK(memcmp(&c.frame[6], &oracle[6], c.frame_len - 6) == 0);

    // A bad path_len is refused, never truncated into a frame.
    CHECK(!meshcore::dmCompose(g_self, g_a, secret, kTs, 1, kText, path, 0xC1, &c));   // hash_size 4: reserved
    CHECK(!meshcore::dmCompose(g_self, g_a, secret, kTs, 1, kText, path, 0x7F, &c));   // 63 hops x 2 > MAX_PATH

    // ZERO HOPS IS A ROUTE (EST-439): the peer is a direct neighbour, so a path COUNT of 0 with the hash-size bits
    // kept is a DIRECT frame, not a flood. MeshCore's sendMessage() only treats OUT_PATH_UNKNOWN as "no route"
    // (BaseChatMesh.cpp:481-486); count 0 goes down sendDirect() exactly this way.
    meshcore::DmComposed cz;
    CHECK(meshcore::dmCompose(g_self, g_a, secret, kTs, 1, kText, path, 0x40, &cz));
    CHECK(!cz.flood);
    CHECK(cz.frame[0] == 0x0a);   // TXT_MSG << 2 | DIRECT
    CHECK(cz.frame[1] == 0x40);   // 2-byte path hashes, count 0
    CHECK(cz.frame[2] == g_a[0]);
    CHECK(cz.frame[3] == g_self.pub[0]);
    CHECK(cz.frame_len == on - 2);                       // the oracle's direct frame without its two path bytes
    CHECK(memcmp(&cz.frame[4], &oracle[6], cz.frame_len - 4) == 0);   // the same seal, byte for byte

    // One-byte path hashes with count 0 (0x00) are just as direct.
    meshcore::DmComposed cz1;
    CHECK(meshcore::dmCompose(g_self, g_a, secret, kTs, 1, kText, path, 0x00, &cz1));
    CHECK(!cz1.flood);
    CHECK(cz1.frame[1] == 0x00);
    CHECK(cz1.frame_len == cz.frame_len);

    // OUT_PATH_UNKNOWN (0xFF) is the ONLY "no route": a non-null pointer with 0xFF still floods.
    meshcore::DmComposed cu;
    CHECK(meshcore::dmCompose(g_self, g_a, secret, kTs, 1, kText, path, meshcore::kDmPathUnknown, &cu));
    CHECK(cu.flood);
    CHECK(cu.frame[1] == 0x40);
    // Too long a text is refused (BaseChatMesh.cpp:453).
    char big[meshcore::kMaxText + 2];
    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1] = '\0';
    CHECK(!meshcore::dmCompose(g_self, g_a, secret, kTs, 0, big, nullptr, 0, &c));
}

// 3. A PATH with an ACK: the stored route and the delivered mark. Built from a peer identity we can derive
// (seed 0x40..5f) so the packet really arrives FROM that peer, then parsed as the receiver would.
void testPathRecv() {
    uint8_t pseed[32];
    for (size_t i = 0; i < sizeof(pseed); i++) pseed[i] = (uint8_t)(0x40 + i);
    meshcore::Identity peer;
    CHECK(meshcore::identityFromSeed(peer, pseed));

    uint8_t raw[256];
    const size_t n = fromHex(raw, sizeof(raw), kFrameDirect);
    meshcore::View arrived;
    CHECK(meshcore::decode(raw, n, &arrived));   // carries path ab cd

    uint8_t secret[32];
    CHECK(meshcore::identitySharedSecret(g_self, peer.pub, secret));
    uint8_t ack[6];
    CHECK(fromHex(ack, sizeof(ack), kAck) == 6);

    // The peer's path return to us: peer.pub is the sender, our hash is the destination, the extra is the ACK.
    uint8_t path_frame[256];
    const size_t pn = meshcore::dmBuildPathReturn(arrived, g_self.pub[0], peer.pub, secret, ack, path_frame,
                                                  sizeof(path_frame));
    CHECK(pn > 0);
    meshcore::View pv;
    CHECK(meshcore::decode(path_frame, pn, &pv));

    meshcore::DmPath p;
    CHECK(meshcore::dmOpenPath(g_self, pv, peer.pub, 1, &p));
    CHECK(p.peer == 0);
    CHECK(p.out_path_len == 0x41);
    CHECK(p.out_path[0] == 0xab && p.out_path[1] == 0xcd);
    CHECK(p.extra_type == meshcore::kTypeAck);
    CHECK(p.extra_len >= 6);
    CHECK(memcmp(p.extra, ack, 6) == 0);

    // A truncated PATH never yields that route+ACK — every possible cut, and the decoder may refuse first.
    for (size_t cut = 1; cut < pn; cut++) {
        uint8_t bad[256];
        memcpy(bad, path_frame, pn - cut);
        meshcore::View bv;
        if (!meshcore::decode(bad, pn - cut, &bv)) continue;
        meshcore::DmPath bp;
        const bool opened = meshcore::dmOpenPath(g_self, bv, peer.pub, 1, &bp);
        CHECK(!(opened && bp.out_path_len == 0x41 && bp.extra_len >= 6 && memcmp(bp.extra, ack, 6) == 0));
    }

    // Not ours / not a PATH: refused.
    uint8_t not_ours[256];
    memcpy(not_ours, path_frame, pn);
    not_ours[2] = (uint8_t)(g_self.pub[0] ^ 0xFF);
    meshcore::View nv;
    CHECK(meshcore::decode(not_ours, pn, &nv));
    meshcore::DmPath np;
    CHECK(!meshcore::dmOpenPath(g_self, nv, peer.pub, 1, &np));
    meshcore::View dv;
    CHECK(meshcore::decode(raw, n, &dv));   // a TXT_MSG, not a PATH
    CHECK(!meshcore::dmOpenPath(g_self, dv, peer.pub, 1, &np));
}

// 4. The pending table: the right ACK marks delivered, a wrong one does nothing.
void testPending() {
    meshcore::DmPendingTable t;
    uint8_t ack[6];
    CHECK(fromHex(ack, sizeof(ack), kAck) == 6);

    CHECK(meshcore::dmPendingBegin(t, g_a, 77, ack, 5000, false, 1000));

    uint8_t wrong[4] = {0xde, 0xad, 0xbe, 0xef};
    uint32_t id = 0;
    CHECK(!meshcore::dmPendingAck(t, wrong, &id));
    CHECK(id == 0);

    CHECK(meshcore::dmPendingAck(t, ack, &id));
    CHECK(id == 77);
    // The same ACK twice: the slot is cleared, so the second one matches nothing (MyMesh.cpp:431).
    CHECK(!meshcore::dmPendingAck(t, ack, &id));

    // The oldest is reused when the table is full, so a new send is never lost.
    for (uint32_t i = 0; i < meshcore::kDmPendingSlots; i++) {
        uint8_t a[4] = {(uint8_t)i, 1, 2, 3};
        CHECK(meshcore::dmPendingBegin(t, g_a, 100 + i, a, 1000u * (i + 1), false, 0));
    }
    uint8_t fresh[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    CHECK(meshcore::dmPendingBegin(t, g_a, 999, fresh, 5000, false, 0));
    CHECK(meshcore::dmPendingAck(t, fresh, &id));
    CHECK(id == 999);
}

// 5. The retry rule at exact instants: 3 attempts, the last is a flood, a failed direct clears the path.
void testRetry() {
    meshcore::DmPendingTable t;
    uint8_t ack[4] = {1, 2, 3, 4};
    CHECK(meshcore::dmPendingBegin(t, g_a, 5, ack, 3000, false, 1000));

    meshcore::DmTimeout d = meshcore::dmPendingTimeout(t, 3999);
    CHECK(d.action == meshcore::DmTimeoutAction::None);
    d = meshcore::dmPendingTimeout(t, 4000);   // attempt 0 timed out
    CHECK(d.action == meshcore::DmTimeoutAction::Retry);
    CHECK(d.attempt == 1);
    CHECK(d.msg_id == 5);
    CHECK(!d.flood);                            // a flood attempt did not learn a path: attempt 1 stays a flood
                                                // (flood == false means it may use a stored path)
    CHECK(!d.clear_path);

    // The resend's ACK differs; rearm with it and a fresh deadline.
    uint8_t ack1[4] = {5, 6, 7, 8};
    CHECK(meshcore::dmPendingRearm(t, 5, ack1, 3000, true, 5000));   // attempt 1 went DIRECT along a stored path
    CHECK(meshcore::dmPendingTimeout(t, 7999).action == meshcore::DmTimeoutAction::None);
    d = meshcore::dmPendingTimeout(t, 8000);
    CHECK(d.action == meshcore::DmTimeoutAction::Retry);
    CHECK(d.attempt == 2);       // the LAST attempt
    CHECK(d.flood);              // ... is a flood even if a path is stored
    CHECK(d.clear_path);         // the direct attempt failed: forget the route

    uint8_t ack2[4] = {9, 10, 11, 12};
    CHECK(meshcore::dmPendingRearm(t, 5, ack2, 3000, false, 9000));
    d = meshcore::dmPendingTimeout(t, 12000);
    CHECK(d.action == meshcore::DmTimeoutAction::GiveUp);
    CHECK(d.msg_id == 5);
    // And the slot is gone: nothing left to time out.
    CHECK(meshcore::dmPendingTimeout(t, 20000).action == meshcore::DmTimeoutAction::None);
}

// 6. The companion's timeout arithmetic (MyMesh.cpp:888-895, constants :108-111), and the airtime those rules
//    are fed. The Tanmatsu's lora component exposes no getEstAirtimeFor, so dmAirtimeMillis() is the SX1262
//    formula at kSloPlan's numbers (SF8, BW 62.5 kHz, CR 4/8, preamble 32): 4.096 ms a symbol.
void testTimeoutMath() {
    // 0 B: 36.25 preamble symbols + 16 payload symbols = 214.016 ms -> 215; 20 B: +6 blocks -> 378;
    // 45 B (a DM frame with a 24-character text): +12 blocks -> 575.
    CHECK(meshcore::dmAirtimeMillis(0) == 215);
    CHECK(meshcore::dmAirtimeMillis(20) == 378);
    CHECK(meshcore::dmAirtimeMillis(45) == 575);
    CHECK(meshcore::dmAirtimeMillis(46) >= meshcore::dmAirtimeMillis(45));
    CHECK(meshcore::dmFloodTimeoutMillis(0) == 500);
    CHECK(meshcore::dmFloodTimeoutMillis(100) == 2100);   // 500 + 16*100
    CHECK(meshcore::dmDirectTimeoutMillis(0, 0x41) == 1000);   // 500 + 250*(1+1)
    CHECK(meshcore::dmDirectTimeoutMillis(100, 0x41) == 2200); // 500 + (600+250)*2
    CHECK(meshcore::dmDirectTimeoutMillis(100, 0x42) == 3050); // 500 + 850*3
}

// 6b. S513/EST-439: an entry whose frame never reached the radio is dropped, and a retry that could not be queued
//     is postponed WITHOUT spending the attempt (only the deadline moves).
void testPendingDropAndDelay() {
    meshcore::DmPendingTable t;
    uint8_t ack[4] = {1, 2, 3, 4};
    CHECK(meshcore::dmPendingBegin(t, g_a, 42, ack, 2000, false, 1000));
    uint32_t id = 0;

    // Not queued: delay to 1500, then the SAME attempt 0 times out at 1500 (not attempt 1).
    CHECK(meshcore::dmPendingDelay(t, 42, 500, 1000));
    CHECK(meshcore::dmPendingTimeout(t, 1499).action == meshcore::DmTimeoutAction::None);
    meshcore::DmTimeout d = meshcore::dmPendingTimeout(t, 1500);
    CHECK(d.action == meshcore::DmTimeoutAction::Retry);
    CHECK(d.attempt == 1);

    CHECK(!meshcore::dmPendingDelay(t, 999, 500, 1000));   // an unknown message changes nothing
    CHECK(meshcore::dmPendingDrop(t, 42));
    CHECK(!meshcore::dmPendingAck(t, ack, &id));           // gone: its ACK can no longer match
    CHECK(!meshcore::dmPendingDrop(t, 42));
}

// 7. The plain ACK packet: only an ACK header with at least 4 payload bytes.
void testAckPayload() {
    uint8_t raw[32];
    // Our own flooded createAck (0d40 + 6 ACK bytes) — the frame EST-434 builds.
    const size_t n = fromHex(raw, sizeof(raw), "0d40f976f1cc005a");
    meshcore::View v;
    CHECK(meshcore::decode(raw, n, &v));
    const uint8_t* p = meshcore::dmAckPayload(v);
    CHECK(p != nullptr);
    uint8_t want[6];
    CHECK(fromHex(want, sizeof(want), kAck) == 6);
    CHECK(p != nullptr && memcmp(p, want, 4) == 0);

    // 4 bytes exactly, as Mesh.cpp reads it.
    const size_t n4 = fromHex(raw, sizeof(raw), "0d40f976f1cc");
    CHECK(meshcore::decode(raw, n4, &v));
    CHECK(meshcore::dmAckPayload(v) != nullptr);

    // Three bytes: incomplete, refused.
    const size_t n3 = fromHex(raw, sizeof(raw), "0d40f976f1");
    CHECK(meshcore::decode(raw, n3, &v));
    CHECK(meshcore::dmAckPayload(v) == nullptr);
}

}  // namespace

int main() {
    loadKeys();
    testComposeFlood();
    testComposeDirect();
    testPathRecv();
    testPending();
    testRetry();
    testTimeoutMath();
    testPendingDropAndDelay();
    testAckPayload();
    printf("test_dm_send: %d checks, %d FAILURES\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
