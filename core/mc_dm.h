// SPDX-License-Identifier: MIT
// Direct messages (TXT_MSG), portable: open against a contact list, and the two frames that answer one.
//
// Every rule below is a port of the L2's own MeshCore library, read in S513/EST-433:
//   · packet layout and receive (Mesh.cpp:133-205):
//         payload = dest_hash(1) src_hash(1) MAC(2) ciphertext
//     accepted only when dest_hash == our pub_key[0]; EVERY contact whose pub_key[0] == src_hash is tried
//     and the 2-byte MAC decides; the first that opens wins ("first packet wins", Mesh.cpp:143);
//   · shared secret (Identity.cpp:155): ed25519_key_exchange(their_pub, our_prv), i.e. the vendored
//     core/ed25519/key_exchange.c — the same X25519 every other MeshCore node computes;
//   · MAC-then-decrypt (Utils.cpp), reusing mc_wire's helpers: the 32-byte secret keys an HMAC-SHA256 over the
//     ciphertext, and AES-128-ECB uses its first 16 bytes;
//   · plaintext (helpers/BaseChatMesh.cpp:222-270): ts(4, little-endian), flags, text. txt_type = flags >> 2,
//     attempt = flags & 3. Only TXT_TYPE_PLAIN is shown; the others are reported and NOT acknowledged;
//   · the 6-byte ACK (BaseChatMesh.cpp:244-248): sha256(data[0 .. 5+text_len) || SENDER pub)[0..3],
//     byte 4 = the extended attempt byte (data[5+text_len+1]), byte 5 = random;
//   · createPathReturn (Mesh.cpp:448-486) on a FLOOD arrival, createAck (Mesh.cpp:560-573) on a DIRECT one.
//
// Nothing here allocates, and every length that came off the air is checked before it is used: the parser is in
// the ASan stage of test/host/run.sh (the S513 advert overflow is the reason).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_identity.h"
#include "mc_wire.h"

namespace meshcore {

// Payload types (Packet.h:21-27) and route bits (Packet.h:14-17).
constexpr uint8_t kTypeTxtMsg   = 0x02;
constexpr uint8_t kTypeAck      = 0x03;
constexpr uint8_t kTypePath     = 0x08;
constexpr uint8_t kRouteDirect   = 0x02;

// helpers/BaseChatMesh: txt types live in the top 6 bits of the flags byte.
constexpr uint8_t kTxtTypeCliData    = 1;
constexpr uint8_t kTxtTypeCliCommand = 2;

constexpr size_t kAckBytes     = 6;   // BaseChatMesh.cpp:244-248
constexpr size_t kPathHashSize = 1;   // PATH_HASH_SIZE (MeshCore.h:18), the dest/src hashes in the payload

// One opened TXT_MSG. `peer` is the index into the caller's contact array, never a hash (two contacts can share
// one first byte, and the MAC is what tells them apart).
struct DmMessage {
    int      peer      = -1;
    uint32_t timestamp = 0;
    uint8_t  txt_type  = 0;
    uint8_t  attempt   = 0;
    uint16_t text_len  = 0;
    char     text[kMaxText + 1] = {0};
    // Valid only for TXT_TYPE_PLAIN, which is the only type MeshCore acknowledges (BaseChatMesh.cpp:244).
    bool     ack_valid = false;
    uint8_t  ack[kAckBytes] = {0};
};

// Open a TXT_MSG frame against `peer_count` contacts packed as 32-byte public keys (`peer_pubs + 32*i`).
// False when: the frame is not TXT_MSG, dest_hash is not ours, no contact with that src_hash exists, or no MAC
// matches. `random_byte` is ACK byte 5; it is a parameter so the host test can pin it (the device passes
// esp_random()).
bool dmOpen(const Identity& self, const View& pkt, const uint8_t* peer_pubs, size_t peer_count,
            uint8_t random_byte, DmMessage* out);

// The plain ACK of a DIRECT arrival, as BaseChatMesh::sendAckTo (BaseChatMesh.cpp:43-58) sends it:
//   · `out_path` nullptr = the contact's route is still OUT_PATH_UNKNOWN (we do not store PATH packets yet):
//     upstream then sends createAck() as a FLOOD (Mesh.cpp:560-573 + sendFloodScoped), so the frame is
//         header (ACK << 2) | flood, path_len 0x40 (2-byte hashes, count 0), payload = the 6 ACK bytes.
//   · otherwise `out_path_len` is the contact's stored path_len byte (hash mode in bits 6-7, count in 0-5) and
//     `out_path` its path bytes; the frame is a DIRECT one that copies that route back (Mesh.cpp:693-712).
// The later PATH-slice only has to pass the stored route in; nothing else here changes.
size_t dmBuildAck(const uint8_t* out_path, uint8_t out_path_len, const uint8_t ack[kAckBytes], uint8_t* out,
                  size_t out_size);

// The path return of a FLOOD arrival: a PATH packet that carries the route back TO us and the ACK as its extra
// data, exactly as createPathReturn builds it (Mesh.cpp:448-486):
//   payload = dest_hash(1) src_hash(1) + encryptThenMAC(secret, path_len || path || 0x03 || ack6)
// The frame itself is a fresh flood (2-byte path hashes, count 0), which is what sendFlood does to it
// (Mesh.cpp:641-676). `peer_src_hash` is the sender's pub_key[0] from the received payload, `self_pub` ours.
size_t dmBuildPathReturn(const View& arrived, uint8_t peer_src_hash, const uint8_t self_pub[kPubKeyBytes],
                         const uint8_t secret[kSecretBytes], const uint8_t ack[kAckBytes], uint8_t* out,
                         size_t out_size);

}  // namespace meshcore
