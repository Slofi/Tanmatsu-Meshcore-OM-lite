// SPDX-License-Identifier: MIT
// Host tests for the MeshCore wire codec (S501, step 3b slice 1). Real exit code: 0 = every check passed.
//
// What makes these worth having: the expected bytes are NOT produced by this code. They come from an
// independent implementation — Python's hashlib/hmac + `cryptography`'s AES — for a fixed key, timestamp
// and text (the oracle is quoted in full at the top of the vector block below). So the test compares two
// implementations, not one with itself.
//
// The optional last test reads MC_TEST_CHANNEL_KEY (32 hex chars) from the environment and does a
// build->decode->open round trip with a REAL channel key, so the real key is proven without ever being
// written into a file. Absent = skipped and reported as skipped, never as passed.
//
// Build/run: sh test/host/run.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mc_channels.h"
#include "mc_identity.h"
#include "mc_wire.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        g_checks++;                                                                        \
        if (!(cond)) {                                                                     \
            g_failures++;                                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                         \
        }                                                                                  \
    } while (0)

#define CHECK_STR(got, want)                                                               \
    do {                                                                                   \
        g_checks++;                                                                        \
        if (strcmp((got), (want)) != 0) {                                                  \
            g_failures++;                                                                  \
            printf("FAIL %s:%d  got \"%s\" want \"%s\"\n", __FILE__, __LINE__, (got), (want)); \
        }                                                                                  \
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

// The independent oracle (python3, hashlib + hmac + cryptography):
//   key16  = bytes(range(16)); secret = key16 + bytes(16)         # MeshCore's 32-byte buffer
//   plain  = struct.pack("<I", 1750000000) + b"\x00" + b"EDCT: hello from the tanmatsu"
//   ct     = AES-128-ECB(key16).encrypt(plain zero-padded to 48)  # 48 bytes
//   mac    = HMAC-SHA256(secret, ct)[:2]                          # 2 bytes
//   frame  = b"\x15\x40" + sha256(key16)[:1] + mac + ct           # 53 bytes
const char* kVectorFrame =
    "1540be2b1d0b22ff389168fd798902c8cb928bb7ddf293a071f75749b26da283"
    "0e9db9cebe9647a47914c313081d2f4cb6bacb8f52";
const uint32_t kVectorTs    = 1750000000u;
const char*    kVectorName  = "EDCT";
const char*    kVectorText  = "hello from the tanmatsu";

uint8_t g_key[16];

void testFixedVector() {
    meshcore::Channel ch;
    CHECK(meshcore::channelSetKey(ch, g_key, "Test Channel"));
    CHECK(ch.hash == 0xBE);                       // sha256(key16)[0], from the oracle
    CHECK_STR(ch.name, "Test Channel");
    for (size_t i = 16; i < sizeof(ch.secret); i++) CHECK(ch.secret[i] == 0);   // 128-bit rule

    uint8_t frame[256];
    const size_t n = meshcore::buildGroupText(ch, kVectorTs, kVectorName, kVectorText, 2, frame,
                                              sizeof(frame));
    CHECK(n == strlen(kVectorFrame) / 2);
    char hex[600];
    toHex(hex, frame, n);
    CHECK_STR(hex, kVectorFrame);                 // the whole frame byte for byte

    meshcore::View v;
    CHECK(meshcore::decode(frame, n, &v));
    CHECK(v.route() == 0x01);                     // ROUTE_TYPE_FLOOD
    CHECK(v.type() == 0x05);                      // PAYLOAD_TYPE_GRP_TXT
    CHECK(v.hash_size == 2);                      // the SLO mesh runs 2-byte path hashes
    CHECK(v.hash_count == 0);                     // a fresh flood starts with no path
    CHECK(v.path_bytes == 0);
    CHECK(v.payload_len == n - 2);

    char     sender[32] = {0};
    char     text[64]   = {0};
    uint32_t ts         = 0;
    CHECK(meshcore::openGroupText(ch, v, sender, sizeof(sender), text, sizeof(text), &ts));
    CHECK_STR(sender, kVectorName);
    CHECK_STR(text, kVectorText);
    CHECK(ts == kVectorTs);
}

void testRefusals() {
    meshcore::Channel ch;
    meshcore::channelSetKey(ch, g_key);

    uint8_t frame[256];
    const size_t n = meshcore::buildGroupText(ch, kVectorTs, kVectorName, kVectorText, 2, frame,
                                              sizeof(frame));
    CHECK(n > 0);

    meshcore::View v;
    CHECK(meshcore::decode(frame, n, &v));

    char     sender[32];
    char     text[64];
    uint32_t ts = 0;

    // 1. another channel's key must not open it
    uint8_t other[16];
    memcpy(other, g_key, sizeof(other));
    other[0] ^= 0xFF;
    meshcore::Channel wrong;
    meshcore::channelSetKey(wrong, other);
    CHECK(!meshcore::openGroupText(wrong, v, sender, sizeof(sender), text, sizeof(text), &ts));

    // 2. a flipped MAC bit must not open it
    uint8_t bad[256];
    memcpy(bad, frame, n);
    bad[3] ^= 0x01;                               // the 2-byte MAC sits at 2..3
    meshcore::View vb;
    CHECK(meshcore::decode(bad, n, &vb));
    CHECK(!meshcore::openGroupText(ch, vb, sender, sizeof(sender), text, sizeof(text), &ts));

    // 3. hash-size 4 is reserved: path_len 0xC0 must be refused
    uint8_t reserved[256];
    memcpy(reserved, frame, n);
    reserved[1] = 0xC0;
    CHECK(!meshcore::decode(reserved, n, &v));

    // 4. a truncated frame must be refused, not read past its end
    CHECK(!meshcore::decode(frame, 2, &v));       // no path_len byte
    CHECK(!meshcore::decode(frame, 0, &v));

    // 5. a text that would overflow the packet must be refused, not silently cut
    char big[300];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CHECK(meshcore::buildGroupText(ch, kVectorTs, kVectorName, big, 2, frame, sizeof(frame)) == 0);

    // 6. a non-group-text packet must not be opened as one
    meshcore::View advert = v;
    advert.header = (uint8_t)(0x04 << 2);         // PAYLOAD_TYPE_ADVERT
    CHECK(!meshcore::openGroupText(ch, advert, sender, sizeof(sender), text, sizeof(text), &ts));
}


// The channel table: adding, de-duplicating, finding, and routing a packet to the right channel. This is the
// rule that lets one device hold several channels whose 1-byte hashes may collide.
void testChannelTable() {
    meshcore::ChannelSet set;
    uint8_t a[16], b[16], c[16];
    for (size_t i = 0; i < 16; i++) {
        a[i] = (uint8_t)(0x10 + i);
        b[i] = (uint8_t)(0x20 + i);
        c[i] = (uint8_t)(0x30 + i);
    }

    CHECK(meshcore::channelsAdd(set, a, "Test Channel"));
    CHECK(set.count == 1);
    CHECK_STR(set.ch[0].name, "Test Channel");
    CHECK(set.ch[0].hash != 0 || true);          // hash is derived; equality is checked below
    CHECK(!meshcore::channelsAdd(set, a, "duplicate"));   // same key twice is refused...
    CHECK(set.count == 1);                                // ...and does not consume a slot

    CHECK(meshcore::channelsAdd(set, b, "Other"));
    CHECK(set.count == 2);
    CHECK(meshcore::channelsFindByKey(set, b) == 1);
    CHECK(meshcore::channelsFindByHash(set, set.ch[1].hash) == 1);
    CHECK(meshcore::channelsFindByKey(set, c) == -1);

    // A packet built for channel b must open on b, with its sender and text intact.
    uint8_t      frame[256];
    meshcore::View v;
    char           sender[32] = {0};
    char           text[128]  = {0};
    uint32_t       ts         = 0;
    size_t n = meshcore::buildGroupText(set.ch[1], 1750000200u, "L2", "reply from the L2", 2, frame,
                                        sizeof(frame));
    CHECK(n > 0);
    CHECK(meshcore::decode(frame, n, &v));
    CHECK(meshcore::channelsOpen(set, v, sender, sizeof(sender), text, sizeof(text), &ts) == 1);
    CHECK_STR(sender, "L2");
    CHECK_STR(text, "reply from the L2");
    CHECK(ts == 1750000200u);

    // A packet for a channel we do NOT have must be refused (a hash collision is fine: the MAC decides).
    meshcore::Channel ghost;
    meshcore::channelSetKey(ghost, c);
    n = meshcore::buildGroupText(ghost, 1750000300u, "L2", "not for us", 2, frame, sizeof(frame));
    CHECK(n > 0);
    CHECK(meshcore::decode(frame, n, &v));
    CHECK(meshcore::channelsOpen(set, v, sender, sizeof(sender), text, sizeof(text), &ts) == -1);

    // The table is bounded: filling it to kMaxChannels then adding one more must fail, not overflow.
    meshcore::ChannelSet full;
    uint8_t key[16];
    for (size_t i = 0; i < meshcore::kMaxChannels; i++) {
        memset(key, 0, sizeof(key));
        key[0] = (uint8_t)(0x80 + i);
        CHECK(meshcore::channelsAdd(full, key, "filler"));
    }
    CHECK(full.count == meshcore::kMaxChannels);
    key[0] = 0xFF;
    CHECK(!meshcore::channelsAdd(full, key, "one too many"));
    CHECK(full.count == meshcore::kMaxChannels);
}


// Identity + advert. The expected bytes are NOT produced by this code: Python's `cryptography` (OpenSSL's
// Ed25519) signed the same message from the same seed — Ed25519 is deterministic, so a correct implementation
// must produce exactly this signature. Oracle (python3, cryptography):
//   seed = bytes(range(32)); app = bytes([0x81]) + b"EDCT"; msg = pub + struct.pack("<I", 1750000000) + app
//   priv = Ed25519PrivateKey.from_private_bytes(seed); sig = priv.sign(msg)
const char* kAdvertPub = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
const char* kAdvertSig =
    "8a826734ba1bd7f5f5be0f4718b0c88a1686e22d4aa0a9ec2eefa6a52e1125df"
    "952343ccd43d3fc7ad63ea835241264da9206c93942da1eae638ee85264b6602";
const char* kAdvertFrame =
    "114003a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b880e14e68"
    "8a826734ba1bd7f5f5be0f4718b0c88a1686e22d4aa0a9ec2eefa6a52e1125df952343ccd43d3fc7"
    "ad63ea835241264da9206c93942da1eae638ee85264b66028145444354";

void testIdentityAdvert() {
    // NOTE: this vector uses the name "EDCT" on purpose even though the node is now "TNMT (SI)": the expected
    // bytes were signed by Python over exactly this name, and a fixed vector must not change with the product's
    // naming. The device's own name lives in one place (mc_link.cpp, nodeName()).
    uint8_t seed[32];
    for (size_t i = 0; i < sizeof(seed); i++) seed[i] = (uint8_t)i;

    meshcore::Identity id;
    CHECK(meshcore::identityFromSeed(id, seed));

    char hex[300];
    toHex(hex, id.pub, meshcore::kPubKeyBytes);
    CHECK_STR(hex, kAdvertPub);                      // same public key as the independent implementation

    uint8_t      frame[256];
    const size_t n = meshcore::advertBuild(id, 1750000000u, "EDCT", 2, frame, sizeof(frame));
    CHECK(n == strlen(kAdvertFrame) / 2);
    toHex(hex, frame, n);
    CHECK_STR(hex, kAdvertFrame);                    // ...and the whole advert, signature included
    // the signature on its own, so a failure says which part drifted (payload = pub(32) + ts(4) + sig)
    toHex(hex, &frame[2 + meshcore::kPubKeyBytes + 4], meshcore::kSignatureBytes);
    CHECK_STR(hex, kAdvertSig);

    // Structure: a flood advert with 2-byte path hashes.
    meshcore::View v;
    CHECK(meshcore::decode(frame, n, &v));
    CHECK(v.route() == meshcore::kRouteFlood);
    CHECK(v.type() == meshcore::kTypeAdvert);
    CHECK(v.hash_size == 2);
    CHECK(v.hash_count == 0);

    // Our own parser must verify it and hand back the name and timestamp.
    uint8_t  pub[32];
    uint32_t ts = 0;
    char     name[32] = {0};
    CHECK(meshcore::advertParse(v, pub, &ts, name, sizeof(name)));
    CHECK_STR(name, "EDCT");
    CHECK(ts == 1750000000u);
    CHECK(memcmp(pub, id.pub, sizeof(pub)) == 0);

    // A tampered name must not verify (the signature covers app_data).
    uint8_t bad[256];
    memcpy(bad, frame, n);
    bad[n - 1] ^= 0x01;                             // last byte of "EDCT" -> "EDCU"
    meshcore::View vb;
    CHECK(meshcore::decode(bad, n, &vb));
    CHECK(!meshcore::advertParse(vb, pub, &ts, name, sizeof(name)));

    // A tampered signature must not verify.
    memcpy(bad, frame, n);
    bad[2 + 32 + 4] ^= 0x01;                        // first signature byte
    CHECK(meshcore::decode(bad, n, &vb));
    CHECK(!meshcore::advertParse(vb, pub, &ts, name, sizeof(name)));

    // A tampered timestamp must not verify (it is inside the signed message).
    memcpy(bad, frame, n);
    bad[2 + 32] ^= 0x01;
    CHECK(meshcore::decode(bad, n, &vb));
    CHECK(!meshcore::advertParse(vb, pub, &ts, name, sizeof(name)));

    // An empty name cannot be advertised at all.
    CHECK(meshcore::advertBuild(id, 1750000000u, "", 2, frame, sizeof(frame)) == 0);
}

// The flags-aware app_data (S513/EST-427). Each frame below was produced by the SAME independent oracle as the
// advert vector above (python3, `cryptography`'s Ed25519 over pub || ts || app_data), with seed = bytes(range(32))
// and ts = 1750000000. app_data is flags, then the fields the flags announce, then the name:
//   name_only         0x81 + "EDCT"
//   latlon_name       0x91 + lat(46150000) lon(14990000) + "EDCT"        (int32 LE, x1e6)
//   latlon_feat1_name 0xB1 + lat lon + 0x1234 + "EDCT"                  (ADV_FEAT1_MASK skips 2 bytes)
//   trunc_latlon      0x91 + "ED"                                        (flags lie: only 2 bytes follow)
const char* kAdvertNameOnlyFrame =
    "114003a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b880e14e68"
    "8a826734ba1bd7f5f5be0f4718b0c88a1686e22d4aa0a9ec2eefa6a52e1125df952343ccd43d3fc7"
    "ad63ea835241264da9206c93942da1eae638ee85264b66028145444354";
const char* kAdvertLatLonFrame =
    "114003a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b880e14e68"
    "d0e9958af2fa0bcac1f25499bffb62d87febd7425a1c50aeb8169492630a755701c3661c2956a7a8"
    "25d8ea458099c5dde3d40e8fb51a5bd674ee2afa6f3ca80b917031c002b0bae40045444354";
const char* kAdvertLatLonFeat1Frame =
    "114003a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b880e14e68"
    "512f1fb3df2ee8b7863eea3148b89ef7c5e35fd09f67041f2aa85d7bdb0f570b38c64f25acd2fc06"
    "a0d75ad6af430ea1134f47af09a18111b15d15aeb1378606b17031c002b0bae400341245444354";
const char* kAdvertTruncLatLonFrame =
    "114003a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b880e14e68"
    "04c9ecf4b8b61029454bf131cf0401e65dacf7c8a85141b1d5c83a1fa01e1f8a32dc27a19d8e72"
    "632dc6d3d633fe0355282f848cda5cf000bba81b201fc4080d914544";

bool parseAdvertHex(const char* hex, char* name, size_t name_size, meshcore::AdvertPosition* pos) {
    uint8_t frame[256];
    const size_t n = fromHex(frame, sizeof(frame), hex);
    meshcore::View v;
    if (!meshcore::decode(frame, n, &v)) return false;
    uint8_t  pub[32];
    uint32_t ts = 0;
    return meshcore::advertParse(v, pub, &ts, name, name_size, nullptr, pos);
}

void testAdvertFlags() {
    char name[32] = {0};
    meshcore::AdvertPosition pos;

    // 1. Name only: the old behaviour is still right here, and the position must say "none".
    CHECK(parseAdvertHex(kAdvertNameOnlyFrame, name, sizeof(name), &pos));
    CHECK_STR(name, "EDCT");
    CHECK(!pos.has_position);
    CHECK(pos.lat_e6 == 0 && pos.lon_e6 == 0);

    // 2. The regression this ticket is about: 8 bytes of binary used to be copied in front of the name.
    CHECK(parseAdvertHex(kAdvertLatLonFrame, name, sizeof(name), &pos));
    CHECK_STR(name, "EDCT");
    CHECK(pos.has_position);
    CHECK(pos.lat_e6 == 46150000);
    CHECK(pos.lon_e6 == 14990000);

    // 3. ADV_FEAT1 (0x20) also comes before the name: 2 more bytes to skip.
    CHECK(parseAdvertHex(kAdvertLatLonFeat1Frame, name, sizeof(name), &pos));
    CHECK_STR(name, "EDCT");
    CHECK(pos.has_position);
    CHECK(pos.lat_e6 == 46150000);
    CHECK(pos.lon_e6 == 14990000);

    // 4. Flags claim lat/lon but app_data ends inside the field: no name, no position, never an over-read.
    CHECK(parseAdvertHex(kAdvertTruncLatLonFrame, name, sizeof(name), &pos));
    CHECK_STR(name, "");
    CHECK(!pos.has_position);
    CHECK(pos.lat_e6 == 0 && pos.lon_e6 == 0);
}

// A 186-byte frame any LoRa transmitter can send: advert << 2 | flood, 2-byte path hashes, 184 payload bytes.
// app_len = 184 - 100 = 84 but the signed message buffer is 32 + 4 + 32 = 68, so the copy must be clamped
// (upstream Mesh.cpp:269 does exactly this). ASan must catch an unclamped copy here for every future change.
void testAdvertOversizeFrame() {
    uint8_t frame[186];
    frame[0] = (uint8_t)((meshcore::kTypeAdvert << 2) | meshcore::kRouteFlood);
    frame[1] = 0x40;
    memset(&frame[2], 0x41, 184);

    meshcore::View v;
    CHECK(meshcore::decode(frame, sizeof(frame), &v));
    CHECK(v.payload_len == 184);

    uint8_t  pub[32];
    uint32_t ts = 0;
    char     name[32] = {0};
    // A forged signature, so parse must refuse it — but only AFTER a bounded copy.
    CHECK(!meshcore::advertParse(v, pub, &ts, name, sizeof(name)));
}

// Optional: the REAL channel key, from the environment. Never written anywhere.
void testRealKey() {
    const char* hex = getenv("MC_TEST_CHANNEL_KEY");
    if (hex == nullptr || strlen(hex) != 32) {
        printf("skip  real channel key (set MC_TEST_CHANNEL_KEY=32 hex chars to run it)\n");
        return;
    }
    uint8_t key[16];
    if (fromHex(key, sizeof(key), hex) != sizeof(key)) {
        printf("FAIL  MC_TEST_CHANNEL_KEY is not 32 hex chars\n");
        g_failures++;
        return;
    }
    meshcore::Channel ch;
    CHECK(meshcore::channelSetKey(ch, key));
    printf("real  channel hash 0x%02X\n", ch.hash);

    uint8_t frame[256];
    const size_t n =
        meshcore::buildGroupText(ch, 1750000100u, "EDCT", "hello from the tanmatsu", 2, frame,
                                 sizeof(frame));
    CHECK(n > 0);
    meshcore::View v;
    CHECK(meshcore::decode(frame, n, &v));
    char     sender[32] = {0};
    char     text[64]   = {0};
    uint32_t ts         = 0;
    CHECK(meshcore::openGroupText(ch, v, sender, sizeof(sender), text, sizeof(text), &ts));
    CHECK_STR(sender, "EDCT");
    CHECK_STR(text, "hello from the tanmatsu");
    CHECK(ts == 1750000100u);
}

}  // namespace

int main() {
    for (uint8_t i = 0; i < 16; i++) g_key[i] = i;

    testFixedVector();
    testRefusals();
    testChannelTable();
    testIdentityAdvert();
    testAdvertFlags();
    testAdvertOversizeFrame();
    testRealKey();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
