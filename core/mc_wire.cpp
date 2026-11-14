// SPDX-License-Identifier: MIT
// Portable MeshCore wire codec. No IDF, no Arduino, no allocation: the host test and the device compile
// this same file (see mc_wire.h for the line-by-line sources of every rule below).
#include "mc_wire.h"

#include <string.h>

#include "mc_crypto.h"

namespace meshcore {
namespace {

uint8_t s_scratch[kMaxPayload];

// MeshCore's Utils::encryptThenMAC: zero-pad to whole 16-byte blocks, AES-128-ECB, then HMAC-SHA256 over
// the ciphertext with the 32-byte secret buffer, truncated to 2 bytes. Returns MAC+ciphertext length.
size_t encryptThenMac(const uint8_t secret[kSecretBytes], uint8_t* dest, size_t dest_size,
                      const uint8_t* src, size_t src_len) {
    const size_t blocks = (src_len + kBlockBytes - 1) / kBlockBytes;
    const size_t ct_len = blocks * kBlockBytes;
    if (src_len == 0 || ct_len + kMacBytes > dest_size) return 0;

    uint8_t block[kBlockBytes];
    for (size_t i = 0; i < blocks; i++) {
        memset(block, 0, sizeof(block));
        const size_t off = i * kBlockBytes;
        const size_t n   = (src_len > off) ? (src_len - off) : 0;
        if (n > 0) memcpy(block, src + off, n < kBlockBytes ? n : kBlockBytes);
        if (!aes128EcbEncrypt(secret, block, dest + kMacBytes + off)) return 0;
    }

    uint8_t mac[32];
    if (!hmacSha256(secret, kSecretBytes, dest + kMacBytes, ct_len, mac)) return 0;
    memcpy(dest, mac, kMacBytes);
    return kMacBytes + ct_len;
}

// Inverse of the above; returns the plaintext length (block-aligned, zero-padded) or 0 when the MAC fails.
size_t macThenDecrypt(const uint8_t secret[kSecretBytes], uint8_t* dest, size_t dest_size,
                      const uint8_t* src, size_t src_len) {
    if (src_len <= kMacBytes) return 0;
    const size_t ct_len = src_len - kMacBytes;
    if (ct_len % kBlockBytes != 0 || ct_len > dest_size) return 0;

    uint8_t mac[32];
    if (!hmacSha256(secret, kSecretBytes, src + kMacBytes, ct_len, mac)) return 0;
    if (memcmp(mac, src, kMacBytes) != 0) return 0;   // not our packet / wrong key

    for (size_t off = 0; off < ct_len; off += kBlockBytes) {
        if (!aes128EcbDecrypt(secret, src + kMacBytes + off, dest + off)) return 0;
    }
    return ct_len;
}

size_t boundedStrLen(const char* s, size_t max) {
    if (s == nullptr) return 0;
    size_t n = 0;
    while (n < max && s[n] != '\0') n++;
    return n;
}

}  // namespace

bool channelSetKey(Channel& ch, const uint8_t key[kKeyBytes], const char* name) {
    if (key == nullptr) return false;
    memset(ch.secret, 0, sizeof(ch.secret));      // MeshCore's 128-bit rule (BaseChatMesh.cpp:938)
    memcpy(ch.secret, key, kKeyBytes);
    memset(ch.name, 0, sizeof(ch.name));
    if (name != nullptr) {
        const size_t n = boundedStrLen(name, sizeof(ch.name) - 1);
        memcpy(ch.name, name, n);
    }
    uint8_t full[32];
    if (!sha256(ch.secret, kKeyBytes, full)) return false;
    ch.hash = full[0];
    return true;
}

bool decode(const uint8_t* buf, size_t len, View* out) {
    if (buf == nullptr || out == nullptr || len < 2) return false;
    size_t i = 0;
    *out     = View{};
    out->header = buf[i++];

    const uint8_t route = out->header & 0x03;
    const bool    transport = (route == 0x00 || route == 0x03);
    if (transport) {
        if (i + 4 > len) return false;
        out->transport = &buf[i];
        i += 4;
    }

    out->path_len   = buf[i++];
    out->hash_size  = (uint8_t)((out->path_len >> 6) + 1);
    out->hash_count = (uint8_t)(out->path_len & 63);
    if (out->hash_size == 4) return false;                       // reserved (Packet.cpp:13-18)
    out->path_bytes = (uint8_t)(out->hash_count * out->hash_size);
    if (out->path_bytes > kMaxPath) return false;
    if (i + out->path_bytes > len) return false;
    out->path = &buf[i];
    i += out->path_bytes;

    // MeshCore requires at least one payload byte (Packet.cpp:79 `if (i >= len) return false`), so a frame
    // that ends exactly at the path is invalid, not an empty packet.
    if (i >= len) return false;
    out->payload_len = (uint16_t)(len - i);
    out->payload     = &buf[i];
    return out->payload_len <= kMaxPayload;
}

size_t buildPlain(uint8_t* out, size_t out_size, uint32_t timestamp, const char* sender,
                  const char* text) {
    if (out == nullptr || sender == nullptr || text == nullptr) return 0;
    const size_t name_len = boundedStrLen(sender, kMaxNameChars);
    const size_t text_len = boundedStrLen(text, kMaxText);
    const size_t need     = 5 + name_len + 2 + text_len;   // ts + type + "name: " + text
    if (name_len == 0 || text_len == 0 || need > out_size || need > kMaxPlain) return 0;

    // MeshCore writes the timestamp with memcpy, i.e. little-endian on every target this runs on.
    out[0] = (uint8_t)(timestamp & 0xFF);
    out[1] = (uint8_t)((timestamp >> 8) & 0xFF);
    out[2] = (uint8_t)((timestamp >> 16) & 0xFF);
    out[3] = (uint8_t)((timestamp >> 24) & 0xFF);
    out[4] = kTxtTypePlain;
    memcpy(&out[5], sender, name_len);
    out[5 + name_len]     = ':';
    out[5 + name_len + 1] = ' ';
    memcpy(&out[5 + name_len + 2], text, text_len);
    return need;
}

bool parsePlain(const uint8_t* plain, size_t len, char* sender, size_t sender_size, char* text,
                size_t text_size, uint32_t* timestamp) {
    if (plain == nullptr || len < 7 || sender == nullptr || text == nullptr) return false;
    if (timestamp != nullptr) {
        *timestamp = (uint32_t)plain[0] | ((uint32_t)plain[1] << 8) | ((uint32_t)plain[2] << 16) |
                     ((uint32_t)plain[3] << 24);
    }
    if (plain[4] != kTxtTypePlain) return false;

    // "name: text", terminated by the first NUL or by the end of the decrypted buffer (MeshCore pads the
    // last block with zeroes, so the text ends at the first NUL in practice).
    size_t end = 5;
    while (end < len && plain[end] != '\0') end++;
    const uint8_t* body = &plain[5];
    size_t         n    = end - 5;

    size_t sep = 0;
    while (sep + 1 < n && !(body[sep] == ':' && body[sep + 1] == ' ')) sep++;
    if (sep + 1 >= n) return false;   // no "name: " prefix

    const size_t name_len = sep;
    const size_t body_len = n - (sep + 2);
    if (name_len == 0 || name_len >= sender_size || body_len >= text_size) return false;
    memcpy(sender, body, name_len);
    sender[name_len] = '\0';
    memcpy(text, body + sep + 2, body_len);
    text[body_len] = '\0';
    return true;
}

size_t buildGroupText(const Channel& ch, uint32_t timestamp, const char* sender, const char* text,
                      uint8_t hash_size, uint8_t* out, size_t out_size) {
    if (hash_size < 1 || hash_size > 3) return 0;
    if (out == nullptr || out_size < 2) return 0;

    uint8_t plain[kMaxPlain];
    const size_t plain_len = buildPlain(plain, sizeof(plain), timestamp, sender, text);
    if (plain_len == 0) return 0;

    // payload = channel hash + MAC + ciphertext (Mesh.cpp:createGroupDatagram)
    const size_t sealed = encryptThenMac(ch.secret, s_scratch, sizeof(s_scratch), plain, plain_len);
    if (sealed == 0) return 0;
    const size_t payload_len = 1 + sealed;
    if (payload_len > kMaxPayload) return 0;

    // frame = header, path_len, path (empty for a fresh flood), payload (Packet::writeTo)
    if (out_size < 2 + payload_len) return 0;
    const uint8_t header = (uint8_t)((kTypeGrpTxt << 2) | kRouteFlood);
    out[0]               = header;
    out[1]               = (uint8_t)((hash_size - 1) << 6);   // count 0: a flood starts with no path
    out[2]               = ch.hash;
    memcpy(&out[3], s_scratch, sealed);
    return 2 + payload_len;
}

bool openGroupText(const Channel& ch, const View& pkt, char* sender, size_t sender_size, char* text,
                   size_t text_size, uint32_t* timestamp) {
    if (pkt.type() != kTypeGrpTxt) return false;
    if (pkt.payload_len < 1 + kMacBytes + kBlockBytes) return false;
    if (pkt.payload[0] != ch.hash) return false;   // a different channel that happens to share a slot

    uint8_t plain[kMaxPayload];
    const size_t n = macThenDecrypt(ch.secret, plain, sizeof(plain), pkt.payload + 1,
                                    (size_t)pkt.payload_len - 1);
    if (n == 0) return false;
    return parsePlain(plain, n, sender, sender_size, text, text_size, timestamp);
}

}  // namespace meshcore
