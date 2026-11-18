// SPDX-License-Identifier: MIT
// Node identity and the MeshCore advert, portable.
//
// The advert, read from the L2's library (S501):
//   payload = pub_key(32) + timestamp(4, little-endian) + signature(64) + app_data   (Mesh.cpp:404-434)
//   signature covers pub_key || timestamp || app_data, Ed25519 (Mesh.cpp:262-276 verify side)
//   app_data for a chat node = [ADV_TYPE_CHAT | ADV_NAME_MASK] + name                (AdvertDataHelpers.cpp:4-29)
//   and the frame itself is a flood: header = (PAYLOAD_TYPE_ADVERT << 2) | ROUTE_TYPE_FLOOD = 0x11.
//
// Signing uses the SAME Ed25519 implementation the L2 ships (`core/ed25519/`, orlp/ed25519, zlib licence,
// copied from firmware/lib/meshcore_ed25519), so the signatures are not merely compatible — they are produced
// by the same code as the other end of the link. mbedTLS's PSA has no Ed25519 in either of the two builds we
// have (measured S501: PSA_ERROR_NOT_SUPPORTED with the host's 3.6.5), which is why this is vendored.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_wire.h"

namespace meshcore {

constexpr size_t  kSeedBytes      = 32;
constexpr size_t  kPubKeyBytes    = 32;
constexpr size_t  kPrivKeyBytes   = 64;   // seed || public key (the lib's convention)
constexpr size_t  kSignatureBytes = 64;
constexpr size_t  kMaxAdvertData  = 32;   // MAX_ADVERT_DATA_SIZE
constexpr uint8_t kAdvTypeChat    = 1;
constexpr uint8_t kAdvNameMask    = 0x80;
// MeshCore's app_data flags (AdvertDataHelpers.cpp, AdvertDataParser): the low nibble is the node type, and
// these bits announce fields that sit BETWEEN the flags byte and the name.
constexpr uint8_t kAdvLatLonMask  = 0x10;   // 8 bytes: int32 lat, int32 lon, both x1e6
constexpr uint8_t kAdvFeat1Mask   = 0x20;   // 2 bytes
constexpr uint8_t kAdvFeat2Mask   = 0x40;   // 2 bytes

// The optional position an advert may carry. `has_position` is the only safe way to read lat_e6/lon_e6: an advert
// without ADV_LATLON_MASK (or one whose field is truncated) leaves them zero and this flag false.
struct AdvertPosition {
    int32_t lat_e6 = 0;
    int32_t lon_e6 = 0;
    bool    has_position = false;
};

struct Identity {
    uint8_t pub[kPubKeyBytes];
    uint8_t priv[kPrivKeyBytes];
};

// Derive both keys from a 32-byte seed. The seed is what must be stored (that is what MeshCore keeps).
bool identityFromSeed(Identity& id, const uint8_t seed[kSeedBytes]);

bool identitySign(const Identity& id, const uint8_t* msg, size_t len, uint8_t out[kSignatureBytes]);
bool identityVerify(const uint8_t pub[kPubKeyBytes], const uint8_t* msg, size_t len,
                    const uint8_t sig[kSignatureBytes]);

// app_data = [ADV_TYPE_CHAT | ADV_NAME_MASK] + name, no location (the Tanmatsu has no GPS).
size_t advertDataBuild(uint8_t* out, size_t out_size, const char* name);

// A complete flood advert frame. Returns its length, or 0 when it does not fit / the name is empty.
size_t advertBuild(const Identity& id, uint32_t timestamp, const char* name, uint8_t hash_size,
                   uint8_t* out, size_t out_size);

// Parse an advert frame and verify its signature. On success fills the public key, timestamp and name.
//
// `type_out` decides which adverts are accepted: pass nullptr (the default) and only CHAT nodes come back,
// which is what a chat app wants; pass a pointer and ANY verified type is returned, with its MeshCore type
// (1 chat, 2 repeater, 3 room, 4 sensor) written there. The Nodes page needs the second form: its list is of
// repeaters as much as of chat nodes (S502, step 4).
//
// `position_out` (optional) receives the advert's location when ADV_LATLON_MASK is set AND the field is complete;
// otherwise `has_position` stays false. Every app_data field is bounds-checked against app_len, so a truncated
// advert yields no name and no position — never an over-read.
bool advertParse(const View& pkt, uint8_t pub_out[kPubKeyBytes], uint32_t* timestamp, char* name,
                 size_t name_size, uint8_t* type_out = nullptr, AdvertPosition* position_out = nullptr);

}  // namespace meshcore
