// SPDX-License-Identifier: MIT
// Host tests for the per-channel flood scope (S513/EST-440). Every expected crypto value comes from an
// independent Python oracle (hashlib.sha256 + hmac.new(key, bytes([type]) + payload, hashlib.sha256)),
// quoted below; nothing here compares the code with itself:
//
//   sha256("#si")[0:16]     = 56821e4986e5eb5164b783c6bb4441fe
//   sha256("#SI")[0:16]     = 2a942fc842c85a8592a572657c284fe9
//   sha256("#europe")[0:16] = e81567c824fa51c46542c62b813da68e
//   HMAC(key("si"), 05 || 414243)                    -> 0xa609, bytes 09 a6
//   HMAC(key("si"), 05 || 000102...13)               -> 0x321d, bytes 1d 32
//   full fixed frame below -> code 0x41f2 (bytes 41 f2, little-endian as MeshCore's finalizeHMAC writes)
//
// The fixed frame is built with channel key 00..0f, ts 0x11223344, sender "TNMT (SI)", text "hello #si",
// hash_size 2 and region "si"; its transport code was also checked against the oracle over its payload.
#include <stdio.h>
#include <string.h>

#include "channel_scope.h"
#include "mc_crypto.h"
#include "mc_wire.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                 \
            return;                                                       \
        }                                                                 \
    } while (0)

#define CHECK_EQ(got, want)                                                                     \
    do {                                                                                        \
        ++g_checks;                                                                             \
        const long g_ = static_cast<long>(got);                                                 \
        const long w_ = static_cast<long>(want);                                                \
        if (g_ != w_) {                                                                         \
            printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__, #got, #want, \
                   g_, w_);                                                                     \
            ++g_failures;                                                                       \
            return;                                                                             \
        }                                                                                       \
    } while (0)

size_t fromHex(const char* hex, uint8_t* out, size_t cap) {
    size_t n = 0;
    while (hex[0] != 0 && hex[1] != 0 && n < cap) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = nib(hex[0]), lo = nib(hex[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

meshcore::Channel fixedChannel() {
    uint8_t key[16];
    for (int i = 0; i < 16; ++i) key[i] = (uint8_t)i;
    meshcore::Channel c;
    meshcore::channelSetKey(c, key, "#slovenija");
    return c;
}

// 1. The stored value -> mode map, pinned in a constexpr context like the L2's own test.
void modesAreConstexpr() {
    static_assert(meshcore::scopeModeFor(nullptr) == meshcore::ScopeMode::Default, "null = Default");
    static_assert(meshcore::scopeModeFor("") == meshcore::ScopeMode::Default, "empty = Default");
    static_assert(meshcore::scopeModeFor("!none") == meshcore::ScopeMode::None, "sentinel = None");
    static_assert(meshcore::scopeModeFor("si") == meshcore::ScopeMode::Region, "a name = Region");
    static_assert(meshcore::scopeIsNone("!none"), "exact sentinel");
    static_assert(!meshcore::scopeIsNone("!nonex"), "not a prefix match");
    CHECK(meshcore::scopeModeFor("!nonex") == meshcore::ScopeMode::Region);
}

// 2. The name rules: one '#' dropped, spaces trimmed, '$' refused, 1..30 through MeshCore's char rule,
//    case kept, and NOTHING written on a refusal.
void nameRules() {
    char out[64];
    out[0] = 'X';
    CHECK_EQ(meshcore::scopeNameCanonical("si", out, sizeof(out)), 2);
    CHECK(strcmp(out, "si") == 0);
    CHECK_EQ(meshcore::scopeNameCanonical("#si", out, sizeof(out)), 2);
    CHECK(strcmp(out, "si") == 0);   // '#si' and 'si' are one region
    CHECK_EQ(meshcore::scopeNameCanonical("  #si  ", out, sizeof(out)), 2);
    CHECK(strcmp(out, "si") == 0);
    CHECK_EQ(meshcore::scopeNameCanonical("SI", out, sizeof(out)), 2);
    CHECK(strcmp(out, "SI") == 0);   // case matters

    out[0] = 'X';
    CHECK_EQ(meshcore::scopeNameCanonical("$secret", out, sizeof(out)), -1);
    CHECK(out[0] == 0);   // refused: the buffer is empty, never a half-copied name
    CHECK_EQ(meshcore::scopeNameCanonical("si lj", out, sizeof(out)), -1);
    CHECK_EQ(meshcore::scopeNameCanonical("si.lj", out, sizeof(out)), -1);
    CHECK_EQ(meshcore::scopeNameCanonical("", out, sizeof(out)), -1);
    CHECK_EQ(meshcore::scopeNameCanonical("#", out, sizeof(out)), -1);

    const char* n30 = "abcdefghijklmnopqrstuvwxyzABCD";   // 30 chars
    const char* n31 = "abcdefghijklmnopqrstuvwxyzABCDE";  // 31 chars
    CHECK_EQ(strlen(n30), 30);
    CHECK_EQ(meshcore::scopeNameCanonical(n30, out, sizeof(out)), 30);
    CHECK_EQ(meshcore::scopeNameCanonical(n31, out, sizeof(out)), -1);
}

// 3. Resolution: channel -> node default -> unscoped; No scope beats a node default.
void resolution() {
    meshcore::ScopePlan p = meshcore::scopePlanFor("", nullptr);
    CHECK(p.mode == meshcore::ScopeMode::Default);
    CHECK(p.region[0] == 0);   // unscoped today
    CHECK(p.inheritsDefault);

    p = meshcore::scopePlanFor("", "si");
    CHECK(p.mode == meshcore::ScopeMode::Default);
    CHECK(strcmp(p.region, "si") == 0);
    CHECK(p.inheritsDefault);

    p = meshcore::scopePlanFor("!none", "si");
    CHECK(p.mode == meshcore::ScopeMode::None);
    CHECK(p.region[0] == 0);
    CHECK(!p.inheritsDefault);   // No scope wins even with a node default

    p = meshcore::scopePlanFor("#si", "europe");
    CHECK(p.mode == meshcore::ScopeMode::Region);
    CHECK(strcmp(p.region, "si") == 0);
    CHECK(!p.inheritsDefault);

    // A corrupt record is Default, which then follows the node default like an unset channel.
    p = meshcore::scopePlanFor("#no good", "europe");
    CHECK(p.mode == meshcore::ScopeMode::Default);
    CHECK(strcmp(p.region, "europe") == 0);
    CHECK(p.inheritsDefault);

    char text[32];
    meshcore::scopeHeadText(meshcore::scopePlanFor("#si", nullptr), text, sizeof(text));
    CHECK(strcmp(text, "scope #si") == 0);
    meshcore::scopeHeadText(meshcore::scopePlanFor("!none", nullptr), text, sizeof(text));
    CHECK(strcmp(text, "no scope") == 0);
    meshcore::scopeHeadText(meshcore::scopePlanFor("", nullptr), text, sizeof(text));
    CHECK(text[0] == 0);   // Default, no node default: unscoped, nothing on the head
}

// 4. The region key and the code, against the Python oracle above.
void keysAndCodes() {
    uint8_t key[16];
    CHECK(meshcore::scopeRegionKey("si", key));
    const uint8_t si[16] = {0x56, 0x82, 0x1e, 0x49, 0x86, 0xe5, 0xeb, 0x51,
                            0x64, 0xb7, 0x83, 0xc6, 0xbb, 0x44, 0x41, 0xfe};
    CHECK(memcmp(key, si, 16) == 0);
    CHECK(meshcore::scopeRegionKey("#si", key));
    CHECK(memcmp(key, si, 16) == 0);

    uint8_t keySI[16];
    CHECK(meshcore::scopeRegionKey("SI", keySI));
    const uint8_t SI[16] = {0x2a, 0x94, 0x2f, 0xc8, 0x42, 0xc8, 0x5a, 0x85,
                            0x92, 0xa5, 0x72, 0x65, 0x7c, 0x28, 0x4f, 0xe9};
    CHECK(memcmp(keySI, SI, 16) == 0);
    CHECK(memcmp(key, keySI, 16) != 0);   // "#si" != "#SI"

    CHECK(meshcore::scopeRegionKey("europe", key));
    const uint8_t eu[16] = {0xe8, 0x15, 0x67, 0xc8, 0x24, 0xfa, 0x51, 0xc4,
                            0x65, 0x42, 0xc6, 0x2b, 0x81, 0x3d, 0xa6, 0x8e};
    CHECK(memcmp(key, eu, 16) == 0);

    uint16_t code = 0;
    const uint8_t abc[3] = {0x41, 0x42, 0x43};
    CHECK(meshcore::scopeTransportCode(si, 5, abc, sizeof(abc), &code));
    CHECK_EQ(code, 0xa609);
    const uint8_t seq[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    CHECK(meshcore::scopeTransportCode(si, 5, seq, sizeof(seq), &code));
    CHECK_EQ(code, 0x321d);
    CHECK(meshcore::scopeTransportCode(keySI, 5, abc, sizeof(abc), &code));
    CHECK_EQ(code, 0xe9e3);   // the oracle's "#SI" over the same payload: a different code
}

// 5. The frame: the exact bytes MeshCore's writeTo() order produces, and the decoder reads it back.
void scopedFrame() {
    const meshcore::Channel ch = fixedChannel();
    const meshcore::ScopePlan plan = meshcore::scopePlanFor("si", nullptr);
    uint8_t frame[256];
    const size_t n = meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2, plan,
                                                      nullptr, frame, sizeof(frame));
    const char* wantHex =
        "1441f2000040be3ee280e49b05f4d7e35af9bc23c6bb67ccced6f833808796e167e1c6ab89b2dc1a31";
    uint8_t want[128];
    const size_t wantLen = fromHex(wantHex, want, sizeof(want));
    CHECK_EQ(wantLen, 41);
    CHECK_EQ(n, wantLen);
    CHECK(memcmp(frame, want, n) == 0);   // header, codes, path_len, payload - all of it

    CHECK_EQ(frame[0] & 0x03, meshcore::kRouteTransportFlood);   // route 0, not plain flood
    CHECK_EQ((frame[0] >> 2) & 0x0F, meshcore::kTypeGrpTxt);
    CHECK_EQ(frame[1], 0x41);   // code 0x41f2 little-endian, the oracle's value
    CHECK_EQ(frame[2], 0xf2);
    CHECK_EQ(frame[3], 0x00);   // codes[1] = 0: no home region
    CHECK_EQ(frame[4], 0x00);
    CHECK_EQ(frame[5], 0x40);   // path_len: 2-byte hashes, count 0

    meshcore::View v;
    CHECK(meshcore::decode(frame, n, &v));
    CHECK_EQ(v.route(), meshcore::kRouteTransportFlood);
    CHECK(v.transport != nullptr);
    CHECK_EQ(v.transport[0], 0x41);
    CHECK_EQ(v.transport[1], 0xf2);
    CHECK_EQ(v.path_len, 0x40);
    char sender[32], text[64];
    uint32_t ts = 0;
    CHECK(meshcore::openGroupText(ch, v, sender, sizeof(sender), text, sizeof(text), &ts));
    CHECK(strcmp(sender, "TNMT (SI)") == 0);
    CHECK(strcmp(text, "hello #si") == 0);
    CHECK_EQ(ts, 0x11223344u);

    // "#si" and "si" are one stored region: the same frame.
    uint8_t frame2[256];
    const size_t n2 = meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2,
                                                       meshcore::scopePlanFor("#si", nullptr), nullptr, frame2,
                                                       sizeof(frame2));
    CHECK_EQ(n2, n);
    CHECK(memcmp(frame, frame2, n) == 0);

    // "#SI" is a DIFFERENT region: same shape, different code and bytes.
    uint8_t frameSI[256];
    const size_t nSI = meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2,
                                                        meshcore::scopePlanFor("#SI", nullptr), nullptr, frameSI,
                                                        sizeof(frameSI));
    CHECK_EQ(nSI, n);
    CHECK(memcmp(frame + 1, frameSI + 1, 4) != 0);   // the two transport codes differ
    CHECK(memcmp(frame, frameSI, n) != 0);
}

// 6. No scope and Default build a PLAIN flood: route 1, no transport codes, byte-identical to the
//    existing builder. Default also follows a node default when one is given.
void plainFlood() {
    const meshcore::Channel ch = fixedChannel();
    uint8_t plain[256];
    const size_t plainN = meshcore::buildGroupText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2, plain,
                                                   sizeof(plain));
    CHECK(plainN > 0);

    uint8_t frame[256];
    size_t n = meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2,
                                                meshcore::scopePlanFor("!none", nullptr), nullptr, frame,
                                                sizeof(frame));
    CHECK_EQ(n, plainN);
    CHECK(memcmp(frame, plain, plainN) == 0);
    CHECK_EQ(frame[0] & 0x03, meshcore::kRouteFlood);   // route 1: no codes on the wire
    CHECK_EQ((frame[0] >> 2) & 0x0F, meshcore::kTypeGrpTxt);
    CHECK_EQ(frame[1], 0x40);   // path_len follows the header directly
    meshcore::View v;
    CHECK(meshcore::decode(frame, n, &v));
    CHECK(v.transport == nullptr);

    // Default with no node default: unscoped today.
    n = meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2,
                                         meshcore::scopePlanFor("", nullptr), nullptr, frame, sizeof(frame));
    CHECK_EQ(n, plainN);
    CHECK(memcmp(frame, plain, plainN) == 0);

    // Default falls through to the node default: a region there DOES scope the send...
    n = meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2,
                                         meshcore::scopePlanFor("", "si"), nullptr, frame, sizeof(frame));
    CHECK_EQ(frame[0] & 0x03, meshcore::kRouteTransportFlood);
    CHECK(n > plainN);
    // ...but No scope still refuses it.
    n = meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2,
                                         meshcore::scopePlanFor("!none", "si"), nullptr, frame, sizeof(frame));
    CHECK_EQ(n, plainN);
    CHECK(memcmp(frame, plain, plainN) == 0);

    // A node default whose stored key is all zeroes sends unscoped (the L2's TransportKey::isNull rule).
    uint8_t zeroKey[16] = {0};
    n = meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2,
                                         meshcore::scopePlanFor("", "si"), zeroKey, frame, sizeof(frame));
    CHECK_EQ(n, plainN);
    CHECK(memcmp(frame, plain, plainN) == 0);

    // Not enough room: refused, not truncated.
    uint8_t small[8];
    CHECK_EQ(meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 2,
                                              meshcore::scopePlanFor("si", nullptr), nullptr, small,
                                              sizeof(small)),
             0);
    CHECK_EQ(meshcore::buildScopedChannelText(ch, 0x11223344u, "TNMT (SI)", "hello #si", 4,
                                              meshcore::scopePlanFor("si", nullptr), nullptr, small,
                                              sizeof(small)),
             0);
}

}  // namespace

int main() {
    modesAreConstexpr();
    nameRules();
    resolution();
    keysAndCodes();
    scopedFrame();
    plainFlood();
    if (g_failures == 0) {
        printf("test_channel_scope: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    printf("test_channel_scope: %d checks, %d FAILURES\n", g_checks, g_failures);
    return 1;
}
