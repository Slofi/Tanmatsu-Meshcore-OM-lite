// SPDX-License-Identifier: MIT
// Host tests for direct messages (S513/EST-433). The expected frames are NOT produced by this code: every vector
// below came from an independent Python oracle, quoted here in full so it can be re-run.
//
//   # PyNaCl (crypto_sign_seed_keypair, crypto_sign_ed25519_pk_to_curve25519) + cryptography (AES-128-ECB) +
//   # hashlib/hmac. X25519 from Ed25519 exactly as orlp's key_exchange.c does it: orlp's keypair stores
//   # SHA-512(seed)[:32], clamped, as the private scalar, and PyNaCl exposes that as
//   # crypto_sign_ed25519_sk_to_curve25519; the peer key goes through crypto_sign_ed25519_pk_to_curve25519 and
//   # the secret is crypto_scalarmult. (Handing the raw seed to X25519PrivateKey is wrong and disagreed with
//   # key_exchange.c — the oracle's first version did that.)
//   self = 00..1f, A = 20..3f, B = 40..5f, C = sha256("C"+i) with C.pub[0] == A.pub[0] (found by search)
//   plain  = struct.pack("<I", 1750000000) + bytes([(0<<2)|1]) + b"hello from A" + b"\x00"
//   seal   = HMAC-SHA256(secret, ct)[:2] + AES-128-ECB(secret[:16]) over the zero-padded plaintext
//   frame  = b"\x09\x40" + self.pub[0] + A.pub[0] + seal(shared(self, A))
//   ack    = sha256(plain[:5+len(text)] + A.pub)[:4] + bytes([0, 0x5A])
//
// The oracle output is pasted beside each vector; the vectors are fixed inputs, so this file fails if either the
// decoder or the two ACK builders drift from MeshCore.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
            printf("FAIL %s:%d  got \"%s\" want \"%s\"\n", __FILE__, __LINE__, (got),   \
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

// Oracle output (2026-11-18, the script above).
const char* kSelfPub = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
const char* kAPub    = "29acbae141bccaf0b22e1a94d34d0bc7361e526d0bfe12c89794bc9322966dd7";
const char* kBPub    = "2543b92ff1095511476adc8369db6ddc933665a11978dda1404ee1066ca9559d";
const char* kCPub    = "299faef125e6a61bf161b16b330cbd1d2e8e7a2aef3231f55e34e60ddf7952a2";

// frameA: flood, path_len 0x40 (2-byte hashes, count 0), dest 03, src 29, ts 1750000000, PLAIN, attempt 1.
const char* kFrameA =
    "09400329d27f2e02b4b8db86b854305ff1160486fbe161a9cb219b7ac620fc24afd38d1b8ef3";
// The same plaintext, sealed with B's key but claiming A's src_hash (the MAC must refuse it).
const char* kFrameB =
    "09400329eeba6080c208eb0ff39ff7fe0aff108fc975af191cbd8a7a45a432a9917f58cfa9ab";
// Sealed with C's key, and C's pub[0] == A's pub[0]: both are tried, C's MAC opens this one.
const char* kFrameC =
    "09400329b084bd96bab4961aa2991042f4c8703b8f927344f8915db61709fe6062758927dde1";
// CLI_DATA (txt_type 1): must be reported and NOT acknowledged.
const char* kFrameCli = "094003294d5c2f5b56ab3f3722148fb284ec1ce89261";
// dest_hash 0xfc instead of ours.
const char* kFrameNotOurs =
    "0940fc29d27f2e02b4b8db86b854305ff1160486fbe161a9cb219b7ac620fc24afd38d1b8ef3";
// Ciphertext byte 2 flipped: the MAC must refuse it.
const char* kBadMac =
    "09400329d27fd102b4b8db86b854305ff1160486fbe161a9cb219b7ac620fc24afd38d1b8ef3";
// A direct arrival (route 0x02, one 2-byte path hash ab cd) of the same message.
const char* kFrameDirect =
    "0a41abcd0329d27f2e02b4b8db86b854305ff1160486fbe161a9cb219b7ac620fc24afd38d1b8ef3";
// Oracle ACK for frameA, with random byte 0x5A.
const char* kAck = "f976f1cc005a";
// Oracle path return for frameA (flood, 2-byte hashes, count 0).
const char* kPathReturn = "21402903ce1a62aab0bfad6d34b6f03762ad85f964f0";
// Unknown route (OUT_PATH_UNKNOWN): sendAckTo floods createAck(), so the payload is only the 6 ACK bytes.
const char* kFloodAck = "0d40f976f1cc005a";
// A stored route (path_len 0x41, bytes ab cd): the DIRECT ACK copies it back before the 6 ACK bytes.
const char* kDirectAck = "0e41abcdf976f1cc005a";

meshcore::Identity g_self;
uint8_t             g_a[32], g_b[32], g_c[32];

void loadKeys() {
    uint8_t seed[32];
    for (size_t i = 0; i < sizeof(seed); i++) seed[i] = (uint8_t)i;
    CHECK(meshcore::identityFromSeed(g_self, seed));
    CHECK(fromHex(g_a, sizeof(g_a), kAPub) == 32);
    CHECK(fromHex(g_b, sizeof(g_b), kBPub) == 32);
    CHECK(fromHex(g_c, sizeof(g_c), kCPub) == 32);
    char hex[65];
    toHex(hex, g_self.pub, 32);
    CHECK_STR(hex, kSelfPub);   // the oracle's self key is the one these vectors were made for
}

// Decode a hex frame into the test's own buffer and open it against the given public keys.
bool openFrame(const char* frame_hex, const uint8_t* peers, size_t count, meshcore::DmMessage* out) {
    uint8_t      buf[256];
    const size_t n = fromHex(buf, sizeof(buf), frame_hex);
    if (n == 0) return false;
    meshcore::View v;
    if (!meshcore::decode(buf, n, &v)) return false;
    return meshcore::dmOpen(g_self, v, peers, count, 0x5A, out);
}

void testOpens() {
    meshcore::DmMessage m;
    CHECK(openFrame(kFrameA, g_a, 1, &m));
    CHECK(m.peer == 0);
    CHECK(m.timestamp == 1750000000u);
    CHECK(m.txt_type == meshcore::kTxtTypePlain);
    CHECK(m.attempt == 1);
    CHECK_STR(m.text, "hello from A");
    CHECK(m.text_len == 12);
    CHECK(m.ack_valid);
    char hex[16];
    toHex(hex, m.ack, 6);
    CHECK_STR(hex, kAck);   // the 6 ACK bytes match the oracle
}

void testWrongKeyAndMac() {
    meshcore::DmMessage m;
    // A's message, but only B is in the contact list: nothing can open it.
    CHECK(!openFrame(kFrameA, g_b, 1, &m));
    // B sealed this frame while claiming A's src_hash: A is tried, the MAC refuses it.
    CHECK(!openFrame(kFrameB, g_a, 1, &m));
    // A damaged ciphertext is refused by the MAC.
    CHECK(!openFrame(kBadMac, g_a, 1, &m));
}

void testDestHash() {
    meshcore::DmMessage m;
    CHECK(!openFrame(kFrameNotOurs, g_a, 1, &m));   // dest_hash is not our pub[0]
}

void testTwoContactsOneHashByte() {
    // C's pub[0] == A's pub[0] == 0x29. Both must be tried, in table order, and the MAC decides.
    uint8_t both[2][32];
    memcpy(both[0], g_c, 32);   // first: the wrong one for frameA
    memcpy(both[1], g_a, 32);
    meshcore::DmMessage m;
    CHECK(openFrame(kFrameA, &both[0][0], 2, &m));
    CHECK(m.peer == 1);         // C was tried first and failed; A opened it
    CHECK_STR(m.text, "hello from A");

    m = meshcore::DmMessage{};
    CHECK(openFrame(kFrameC, &both[0][0], 2, &m));
    CHECK(m.peer == 0);         // the same first byte, but C's own MAC opens C's message
    CHECK_STR(m.text, "hello from A");
}

void testNonPlainIsNotAcked() {
    meshcore::DmMessage m;
    CHECK(openFrame(kFrameCli, g_a, 1, &m));
    CHECK(m.txt_type == meshcore::kTxtTypeCliData);
    CHECK(!m.ack_valid);
    CHECK(m.ack[0] == 0 && m.ack[5] == 0);
    // S516/EST-472 item 5: a frame that OPENS as a DM but is CLI-typed is repeater traffic. The classifier is
    // what main/mc_link.cpp now checks before dmPush, so a late/unsolicited CLI answer cannot become a thread.
    CHECK(meshcore::isRepeaterCliText(m.txt_type));
    CHECK(!meshcore::isRepeaterCliText(meshcore::kTxtTypePlain));
    CHECK(meshcore::isRepeaterCliText(meshcore::kTxtTypeCliCommand));
    CHECK(meshcore::isRepeaterCliText(3));    // TXT_TYPE_CLI_COMMAND (TxtDataHelpers.h:9)
    // TXT_TYPE_SIGNED_PLAIN (TxtDataHelpers.h:8) is a person's message (room servers sign theirs): never dropped as
    // repeater traffic. S516 Claude review: the constant said 2 = CLI_COMMAND and this line said 3 = SIGNED_PLAIN.
    CHECK(!meshcore::isRepeaterCliText(meshcore::kTxtTypeSignedPlain));
    CHECK(!meshcore::isRepeaterCliText(2));
}

void testAckBuilders() {
    uint8_t      buf[256];
    meshcore::View arrived;
    const size_t n = fromHex(buf, sizeof(buf), kFrameA);
    CHECK(meshcore::decode(buf, n, &arrived));

    uint8_t secret[32];
    CHECK(meshcore::identitySharedSecret(g_self, g_a, secret));
    uint8_t ack[6];
    CHECK(fromHex(ack, sizeof(ack), kAck) == 6);

    uint8_t frame[256];
    const size_t pr = meshcore::dmBuildPathReturn(arrived, g_a[0], g_self.pub, secret, ack, frame,
                                                  sizeof(frame));
    char hex[600];
    toHex(hex, frame, pr);
    CHECK_STR(hex, kPathReturn);

    // A direct arrival while the contact's out_path is still unknown: a FLOODED createAck(), header 0x0d,
    // path_len 0x40 (2-byte hashes, count 0), payload = the 6 ACK bytes and nothing else.
    const size_t dn = fromHex(buf, sizeof(buf), kFrameDirect);
    CHECK(meshcore::decode(buf, dn, &arrived));
    const size_t fn = meshcore::dmBuildAck(nullptr, 0, ack, frame, sizeof(frame));
    toHex(hex, frame, fn);
    CHECK_STR(hex, kFloodAck);

    // Once a later slice stores the contact's route, the SAME call with the path (0x41, ab cd) is a direct ACK.
    static const uint8_t out_path[2] = {0xab, 0xcd};
    const size_t an = meshcore::dmBuildAck(out_path, 0x41, ack, frame, sizeof(frame));
    toHex(hex, frame, an);
    CHECK_STR(hex, kDirectAck);

    // A frame too small for the output is refused rather than truncated, in both forms.
    CHECK(meshcore::dmBuildAck(nullptr, 0, ack, frame, 5) == 0);
    CHECK(meshcore::dmBuildAck(out_path, 0x41, ack, frame, 5) == 0);
    CHECK(meshcore::dmBuildPathReturn(arrived, g_a[0], g_self.pub, secret, ack, frame, 8) == 0);
}

void testBounds() {
    meshcore::DmMessage m;
    // 186-byte junk TXT_MSG (the S513 advert-sized frame): correct type and dest_hash, then garbage. The
    // ciphertext is not block-aligned, so it is refused before any copy. This case is also in the ASan stage.
    uint8_t junk[186];
    junk[0] = 0x09;
    junk[1] = 0x40;
    junk[2] = g_self.pub[0];
    junk[3] = g_a[0];
    for (size_t i = 4; i < sizeof(junk); i++) junk[i] = (uint8_t)i;
    meshcore::View v;
    CHECK(meshcore::decode(junk, sizeof(junk), &v));
    CHECK(!meshcore::dmOpen(g_self, v, g_a, 1, 0x5A, &m));

    // The shortest payload MeshCore rejects: dest+src+MAC+1 cipher block is the minimum, one byte less is not.
    uint8_t tiny[2 + 19];
    tiny[0] = 0x09;
    tiny[1] = 0x40;
    tiny[2] = g_self.pub[0];
    tiny[3] = g_a[0];
    memset(&tiny[4], 0, sizeof(tiny) - 4);
    CHECK(meshcore::decode(tiny, sizeof(tiny), &v));
    CHECK(!meshcore::dmOpen(g_self, v, g_a, 1, 0x5A, &m));

    // A payload that is 20 bytes but whose ciphertext is not a whole block.
    uint8_t odd[2 + 21];
    odd[0] = 0x09;
    odd[1] = 0x40;
    odd[2] = g_self.pub[0];
    odd[3] = g_a[0];
    memset(&odd[4], 0, sizeof(odd) - 4);
    CHECK(meshcore::decode(odd, sizeof(odd), &v));
    CHECK(!meshcore::dmOpen(g_self, v, g_a, 1, 0x5A, &m));

    // A null output is not a crash.
    CHECK(!meshcore::dmOpen(g_self, v, g_a, 1, 0x5A, nullptr));
}

}  // namespace

int main() {
    loadKeys();
    testOpens();
    testWrongKeyAndMac();
    testDestHash();
    testTwoContactsOneHashByte();
    testNonPlainIsNotAcked();
    testAckBuilders();
    testBounds();
    printf("test_dm: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
