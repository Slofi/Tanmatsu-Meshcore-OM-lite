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
                 size_t name_size, uint8_t* type_out, AdvertPosition* position_out) {
    if (pkt.type() != kTypeAdvert) return false;
    if (pkt.payload_len < kPubKeyBytes + 4 + kSignatureBytes + 1) return false;
    if (name == nullptr || name_size < 2) return false;

    const uint8_t* pub = pkt.payload;
    const uint8_t* ts  = pub + kPubKeyBytes;
    const uint8_t* sig = ts + 4;
    const uint8_t* app = sig + kSignatureBytes;
    const size_t   raw_app_len = (size_t)pkt.payload_len - (kPubKeyBytes + 4 + kSignatureBytes);

    // CLAMP BEFORE THE COPY, exactly as upstream MeshCore does it (firmware/lib/meshcore/Mesh.cpp:269:
    //   if (app_data_len > MAX_ADVERT_DATA_SIZE) { app_data_len = MAX_ADVERT_DATA_SIZE; }
    // The payload length comes straight off the air (up to kMaxPayload 184 -> app_len 84), while `message` is
    // kPubKeyBytes + 4 + kMaxAdvertData = 68 bytes. Without this clamp any LoRa transmitter can overflow the
    // stack before the signature is even checked (proven under ASan, S513/EST-427).
    const size_t app_len = raw_app_len > kMaxAdvertData ? kMaxAdvertData : raw_app_len;

    const uint32_t stamp = (uint32_t)ts[0] | ((uint32_t)ts[1] << 8) | ((uint32_t)ts[2] << 16) |
                           ((uint32_t)ts[3] << 24);

    uint8_t message[kPubKeyBytes + 4 + kMaxAdvertData];
    memcpy(message, pub, kPubKeyBytes + 4);
    memcpy(message + kPubKeyBytes + 4, app, app_len);
    // The clamped length is used for the verify too, so the signature is checked over exactly the bytes copied.
    if (!identityVerify(pub, message, kPubKeyBytes + 4 + app_len, sig)) return false;

    // app_data, parsed the upstream way (firmware/lib/meshcore/helpers/AdvertDataHelpers.cpp, the
    // AdvertDataParser constructor): flags, then lat+lon (8 bytes) if ADV_LATLON_MASK, then 2 bytes if
    // ADV_FEAT1_MASK, then 2 bytes if ADV_FEAT2_MASK, then the name if ADV_NAME_MASK. Unlike upstream, every
    // field is bounds-checked against app_len: a truncated field means no name and no position, never an over-read.
    name[0] = '\0';
    const uint8_t flags = app[0];
    size_t        off   = 1;
    bool          fields_ok = true;
    int32_t       lat_e6 = 0;
    int32_t       lon_e6 = 0;
    bool          has_position = false;

    if ((flags & kAdvLatLonMask) != 0) {
        if (off + 8 <= app_len) {
            memcpy(&lat_e6, &app[off], 4);   // MeshCore stores these with memcpy: little-endian on our targets
            off += 4;
            memcpy(&lon_e6, &app[off], 4);
            off += 4;
            has_position = true;
        } else {
            fields_ok = false;               // truncated lat/lon: give up on the rest of app_data
        }
    }
    if (fields_ok && (flags & kAdvFeat1Mask) != 0) {
        if (off + 2 <= app_len) off += 2; else fields_ok = false;
    }
    if (fields_ok && (flags & kAdvFeat2Mask) != 0) {
        if (off + 2 <= app_len) off += 2; else fields_ok = false;
    }
    if (fields_ok && (flags & kAdvNameMask) != 0 && app_len > off) {
        size_t n = app_len - off;
        if (n > name_size - 1) n = name_size - 1;
        memcpy(name, &app[off], n);
        name[n] = '\0';
    }
    const uint8_t type = (uint8_t)(flags & 0x0F);
    // The caller decides: a chat app gets chat nodes only, the Nodes list gets every verified type.
    if (type_out == nullptr && type != kAdvTypeChat) return false;   // verified, but not our kind
    if (type == 0) return false;                                     // no type at all: not a node advert
    if (type_out != nullptr) *type_out = type;

    if (pub_out != nullptr) memcpy(pub_out, pub, kPubKeyBytes);
    if (timestamp != nullptr) *timestamp = stamp;
    if (position_out != nullptr) {
        position_out->lat_e6       = lat_e6;
        position_out->lon_e6       = lon_e6;
        position_out->has_position = has_position;
    }
    return true;
}

}  // namespace meshcore
