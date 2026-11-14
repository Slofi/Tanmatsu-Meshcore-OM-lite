// SPDX-License-Identifier: MIT
#include "mc_identity.h"

#include <string.h>

#include "ed25519/ed_25519.h"

namespace meshcore {
namespace {

size_t boundedLen(const char* s, size_t max) {
    if (s == nullptr) return 0;
    size_t n = 0;
    while (n < max && s[n] != '\0') n++;
    return n;
}

}  // namespace

bool identityFromSeed(Identity& id, const uint8_t seed[kSeedBytes]) {
    if (seed == nullptr) return false;
    memset(&id, 0, sizeof(id));
    // ed25519_create_keypair(public_key, private_key, seed): private = seed || public (64 bytes).
    ed25519_create_keypair(id.pub, id.priv, seed);
    return true;
}

bool identitySign(const Identity& id, const uint8_t* msg, size_t len, uint8_t out[kSignatureBytes]) {
    if (msg == nullptr && len > 0) return false;
    ed25519_sign(out, msg, len, id.pub, id.priv);
    return true;
}

bool identityVerify(const uint8_t pub[kPubKeyBytes], const uint8_t* msg, size_t len,
                    const uint8_t sig[kSignatureBytes]) {
    if (pub == nullptr || sig == nullptr || (msg == nullptr && len > 0)) return false;
    return ed25519_verify(sig, msg, len, pub) != 0;
}

size_t advertDataBuild(uint8_t* out, size_t out_size, const char* name) {
    if (out == nullptr || out_size < 2) return 0;
    const size_t name_len = boundedLen(name, kMaxAdvertData - 1);
    if (name_len == 0) return 0;
    out[0] = (uint8_t)(kAdvTypeChat | kAdvNameMask);
    memcpy(&out[1], name, name_len);
    return 1 + name_len;
}

size_t advertBuild(const Identity& id, uint32_t timestamp, const char* name, uint8_t hash_size,
                   uint8_t* out, size_t out_size) {
    if (hash_size < 1 || hash_size > 3 || out == nullptr) return 0;

    uint8_t app_data[kMaxAdvertData];
    const size_t app_len = advertDataBuild(app_data, sizeof(app_data), name);
    if (app_len == 0) return 0;

    const size_t payload_len = kPubKeyBytes + 4 + kSignatureBytes + app_len;
    if (payload_len > kMaxPayload) return 0;
    if (out_size < 2 + payload_len) return 0;

    out[0] = (uint8_t)((kTypeAdvert << 2) | kRouteFlood);   // 0x11
    out[1] = (uint8_t)((hash_size - 1) << 6);               // count 0: a fresh flood

    uint8_t* payload   = &out[2];
    uint8_t* signature = payload + kPubKeyBytes + 4;
    uint8_t* data      = signature + kSignatureBytes;

    memcpy(payload, id.pub, kPubKeyBytes);
    // MeshCore writes the timestamp with memcpy, i.e. little-endian on every target this runs on.
    payload[kPubKeyBytes + 0] = (uint8_t)(timestamp & 0xFF);
    payload[kPubKeyBytes + 1] = (uint8_t)((timestamp >> 8) & 0xFF);
    payload[kPubKeyBytes + 2] = (uint8_t)((timestamp >> 16) & 0xFF);
    payload[kPubKeyBytes + 3] = (uint8_t)((timestamp >> 24) & 0xFF);
    memcpy(data, app_data, app_len);

    // The signed message is pub_key || timestamp || app_data, with the signature field left out.
    uint8_t message[kPubKeyBytes + 4 + kMaxAdvertData];
    memcpy(message, payload, kPubKeyBytes + 4);
    memcpy(message + kPubKeyBytes + 4, app_data, app_len);
    if (!identitySign(id, message, kPubKeyBytes + 4 + app_len, signature)) return 0;

    return 2 + payload_len;
}

bool advertParse(const View& pkt, uint8_t pub_out[kPubKeyBytes], uint32_t* timestamp, char* name,
                 size_t name_size, uint8_t* type_out) {
    if (pkt.type() != kTypeAdvert) return false;
    if (pkt.payload_len < kPubKeyBytes + 4 + kSignatureBytes + 1) return false;
    if (name == nullptr || name_size < 2) return false;

    const uint8_t* pub = pkt.payload;
    const uint8_t* ts  = pub + kPubKeyBytes;
    const uint8_t* sig = ts + 4;
    const uint8_t* app = sig + kSignatureBytes;
    const size_t   app_len = (size_t)pkt.payload_len - (kPubKeyBytes + 4 + kSignatureBytes);

    const uint32_t stamp = (uint32_t)ts[0] | ((uint32_t)ts[1] << 8) | ((uint32_t)ts[2] << 16) |
                           ((uint32_t)ts[3] << 24);

    uint8_t message[kPubKeyBytes + 4 + kMaxAdvertData];
    memcpy(message, pub, kPubKeyBytes + 4);
    memcpy(message + kPubKeyBytes + 4, app, app_len);
    if (!identityVerify(pub, message, kPubKeyBytes + 4 + (app_len > kMaxAdvertData ? kMaxAdvertData : app_len),
                        sig))
        return false;

    // app_data: flags byte, then (when the name mask is set) the name, no NUL.
    name[0] = '\0';
    if ((app[0] & kAdvNameMask) != 0 && app_len > 1) {
        size_t n = app_len - 1;
        if (n > name_size - 1) n = name_size - 1;
        memcpy(name, &app[1], n);
        name[n] = '\0';
    }
    const uint8_t type = (uint8_t)(app[0] & 0x0F);
    // The caller decides: a chat app gets chat nodes only, the Nodes list gets every verified type.
    if (type_out == nullptr && type != kAdvTypeChat) return false;   // verified, but not our kind
    if (type == 0) return false;                                     // no type at all: not a node advert
    if (type_out != nullptr) *type_out = type;

    if (pub_out != nullptr) memcpy(pub_out, pub, kPubKeyBytes);
    if (timestamp != nullptr) *timestamp = stamp;
    return true;
}

}  // namespace meshcore
