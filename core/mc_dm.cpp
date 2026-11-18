// SPDX-License-Identifier: MIT
#include "mc_dm.h"

#include <string.h>

#include "mc_crypto.h"

namespace meshcore {
namespace {

uint32_t readLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// BaseChatMesh.cpp:244-248. The hash covers the plaintext UP TO the text (timestamp + flags + text, no NUL) and
// then the SENDER's public key, so only the sender's own key can produce an ACK the sender will accept. Byte 4 is
// the extended attempt byte that sits just after the text's NUL; byte 5 is random and is the caller's.
void buildAck(const uint8_t* plain, size_t plain_len, size_t text_end, const uint8_t peer_pub[kPubKeyBytes],
              uint8_t random_byte, uint8_t ack[kAckBytes]) {
    uint8_t buf[5 + kMaxText + kPubKeyBytes];
    const size_t n = text_end;   // 5 + text_len, checked by the caller
    memcpy(buf, plain, n);
    memcpy(buf + n, peer_pub, kPubKeyBytes);
    uint8_t full[32];
    (void)sha256(buf, n + kPubKeyBytes, full);
    memcpy(ack, full, 4);
    // One past the NUL is inside the (zero-padded) decrypted block; if a short frame left it outside, use 0
    // rather than reading past the buffer — the S513 rule.
    ack[4] = (text_end + 1 < plain_len) ? plain[text_end + 1] : 0;
    ack[5] = random_byte;
}

}  // namespace

bool dmOpen(const Identity& self, const View& pkt, const uint8_t* peer_pubs, size_t peer_count,
            uint8_t random_byte, DmMessage* out) {
    if (out == nullptr) return false;
    if (pkt.type() != kTypeTxtMsg) return false;

    // dest_hash(1) + src_hash(1) + MAC(2) + at least ONE cipher block (Mesh.cpp:135-139 and the MACThenDecrypt
    // rule). Refuse before any pointer arithmetic: payload_len is a length straight off the air.
    if (pkt.payload_len < 2 + kMacBytes + kBlockBytes) return false;
    if (pkt.payload == nullptr) return false;

    const uint8_t dest_hash = pkt.payload[0];
    const uint8_t src_hash  = pkt.payload[1];
    if (dest_hash != self.pub[0]) return false;   // self_id.isHashMatch(&dest_hash), Mesh.cpp:147

    const uint8_t* mac_and_data = pkt.payload + 2;
    const size_t   sealed_len   = (size_t)pkt.payload_len - 2;
    if (sealed_len <= kMacBytes || ((sealed_len - kMacBytes) % kBlockBytes) != 0) return false;

    uint8_t plain[kMaxPayload];
    if (peer_pubs == nullptr) return false;
    for (size_t i = 0; i < peer_count; i++) {
        const uint8_t* peer = peer_pubs + i * kPubKeyBytes;
        if (peer[0] != src_hash) continue;   // searchPeersByHash: only these are even tried

        uint8_t secret[kSecretBytes];
        if (!identitySharedSecret(self, peer, secret)) continue;
        const size_t n = openPayload(secret, plain, sizeof(plain), mac_and_data, sealed_len);
        if (n == 0) continue;   // wrong key / damaged MAC: the next contact with this hash gets its turn

        if (n < 6) continue;    // BaseChatMesh.cpp:222 `len > 5`: ts(4) + flags(1) + at least one byte

        DmMessage m;
        m.peer      = (int)i;
        m.timestamp = readLe32(plain);
        m.txt_type  = (uint8_t)(plain[4] >> 2);
        m.attempt   = (uint8_t)(plain[4] & 0x03);

        // The text ends at the first NUL inside the decrypted block (MeshCore zero-pads the last block).
        size_t end = 5;
        while (end < n && plain[end] != '\0') end++;
        const size_t text_len = end - 5;
        if (text_len > kMaxText) continue;   // longer than MeshCore's MAX_TEXT_LEN: refuse, do not cut blindly

        m.text_len = (uint16_t)text_len;
        memcpy(m.text, &plain[5], text_len);
        m.text[text_len] = '\0';

        if (m.txt_type == kTxtTypePlain) {
            buildAck(plain, n, end, peer, random_byte, m.ack);
            m.ack_valid = true;
        }
        *out = m;
        return true;
    }
    return false;
}

size_t dmBuildAck(const uint8_t* out_path, uint8_t out_path_len, const uint8_t ack[kAckBytes], uint8_t* out,
                  size_t out_size) {
    if (ack == nullptr || out == nullptr) return 0;
    if (kAckBytes > kMaxPayload) return 0;

    if (out_path == nullptr) {
        // OUT_PATH_UNKNOWN: sendAckTo takes the createAck() branch and floods it (BaseChatMesh.cpp:44-47). A
        // fresh 2-byte-hash flood, exactly like our other floods; the payload is the 6 ACK bytes and nothing else.
        const size_t need = 2 + kAckBytes;
        if (need > out_size) return 0;
        out[0] = (uint8_t)((kTypeAck << 2) | kRouteFlood);
        out[1] = (uint8_t)((2 - 1) << 6);   // 2-byte hashes, count 0
        memcpy(&out[2], ack, kAckBytes);
        return need;
    }

    // A stored route: copyPath()'s length, so path_len's hash mode still says how many bytes each hash is.
    const uint8_t hash_size  = (uint8_t)((out_path_len >> 6) + 1);
    const uint8_t hash_count = (uint8_t)(out_path_len & 63);
    const size_t  path_bytes = (size_t)hash_count * hash_size;
    if (path_bytes > kMaxPath) return 0;
    const size_t need = 2 + path_bytes + kAckBytes;
    if (need > out_size) return 0;

    out[0] = (uint8_t)((kTypeAck << 2) | kRouteDirect);   // Mesh.cpp:565 + sendDirect's route bits
    out[1] = out_path_len;                                // the route the contact stored, copied back
    if (path_bytes > 0) memcpy(&out[2], out_path, path_bytes);
    memcpy(&out[2 + path_bytes], ack, kAckBytes);
    return need;
}

size_t dmBuildPathReturn(const View& arrived, uint8_t peer_src_hash, const uint8_t self_pub[kPubKeyBytes],
                         const uint8_t secret[kSecretBytes], const uint8_t ack[kAckBytes], uint8_t* out,
                         size_t out_size) {
    if (self_pub == nullptr || secret == nullptr || ack == nullptr || out == nullptr) return 0;
    if (arrived.path_bytes > 0 && arrived.path == nullptr) return 0;

    // createPathReturn's size check: path bytes + extra + path_len byte + extra_type byte + 2 hashes <= the
    // payload MeshCore will accept (MAX_PACKET_PAYLOAD - 2 - CIPHER_BLOCK_SIZE, Mesh.cpp:440).
    const size_t data_len = 1 + arrived.path_bytes + 1 + kAckBytes;
    if (arrived.path_bytes + kAckBytes + 5 > kMaxPayload - 2 - kBlockBytes) return 0;
    if (data_len > kMaxPayload) return 0;

    uint8_t data[kMaxPayload];
    data[0] = arrived.path_len;   // the route TO us: this is what the sender stores as out_path
    if (arrived.path_bytes > 0) memcpy(&data[1], arrived.path, arrived.path_bytes);
    data[1 + arrived.path_bytes] = kTypeAck;   // extra_type (Mesh.cpp:466)
    memcpy(&data[2 + arrived.path_bytes], ack, kAckBytes);

    uint8_t sealed[kMaxPayload];
    const size_t sealed_len = sealPayload(secret, sealed, sizeof(sealed), data, data_len);
    if (sealed_len == 0) return 0;

    const size_t payload_len = kPathHashSize + kPathHashSize + sealed_len;
    const size_t need        = 2 + payload_len;
    if (payload_len > kMaxPayload || need > out_size) return 0;

    out[0] = (uint8_t)((kTypePath << 2) | kRouteFlood);   // Mesh.cpp:456 + sendFlood's route bits
    out[1] = (uint8_t)((2 - 1) << 6);                    // fresh flood, 2-byte path hashes (the SLO mesh)
    out[2] = peer_src_hash;                              // dest hash: the sender's pub_key[0]
    out[3] = self_pub[0];                                // src hash: ours
    memcpy(&out[4], sealed, sealed_len);
    return need;
}

}  // namespace meshcore
