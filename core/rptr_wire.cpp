// SPDX-License-Identifier: MIT
// See rptr_wire.h for the upstream file:line every layout below was taken from.
#include "rptr_wire.h"

#include <string.h>

namespace om {
namespace rptr {
namespace {

void putLe32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// Packet::isValidPathLen (Packet.cpp:13-18): hash_size 4 is reserved, and the count must fit MAX_PATH_SIZE.
bool validPathLen(uint8_t path_len) {
  const uint8_t hash_size = (uint8_t)((path_len >> 6) + 1);
  const uint8_t count = (uint8_t)(path_len & 63);
  if (hash_size == 4) return false;
  return (size_t)count * hash_size <= meshcore::kMaxPath;
}

// A shared route preamble: header already decided, then path_len/path. Returns bytes written or 0.
size_t writeRoute(uint8_t* out, size_t cap, uint8_t header, const uint8_t* path, uint8_t path_len,
                  size_t* path_bytes) {
  const bool flood = (path == nullptr);
  uint8_t len = 0;
  if (flood) {
    len = kFloodPathLen;
  } else {
    if (!validPathLen(path_len)) return 0;
    len = path_len;
  }
  const size_t bytes = (size_t)(len & 63) * (size_t)((len >> 6) + 1);
  if (2 + bytes > cap) return 0;
  out[0] = header;
  out[1] = len;
  if (!flood && bytes > 0) memcpy(&out[2], path, bytes);
  *path_bytes = bytes;
  return 2 + bytes;
}

}  // namespace

size_t buildScan(uint32_t tag, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < 12) return 0;
  // sendZeroHop: header route becomes DIRECT and path_len is 0 (Mesh.cpp:717-726).
  out[0] = (uint8_t)((kTypeControl << 2) | kRouteDirect);
  out[1] = 0;
  out[2] = kCtlDiscoverReq;                     // prefix_only = 0: ask for the whole key
  out[3] = (uint8_t)(1u << kAdvTypeRepeater);   // the request's filter byte
  putLe32(&out[4], tag);
  putLe32(&out[8], 0);                          // "since"
  return 12;
}

size_t buildLogin(const meshcore::Identity& self, const uint8_t dest_pub[kPubKeyBytes],
                  const uint8_t secret[meshcore::kSecretBytes], uint32_t timestamp, const char* password,
                  const uint8_t* path, uint8_t path_len, uint8_t* out, size_t cap) {
  if (dest_pub == nullptr || secret == nullptr || password == nullptr || out == nullptr) return 0;
  size_t pw_len = strlen(password);
  if (pw_len > (size_t)kPasswordMax) pw_len = (size_t)kPasswordMax;
  if (pw_len == 0) return 0;

  // temp = timestamp(4) | password          (BaseChatMesh.cpp:607-617; a room adds sync_since, not us)
  uint8_t temp[4 + kPasswordMax];
  putLe32(temp, timestamp);
  memcpy(&temp[4], password, pw_len);

  uint8_t sealed[meshcore::kMaxPayload];
  const size_t sealed_len = meshcore::sealPayload(secret, sealed, sizeof(sealed), temp, 4 + pw_len);
  if (sealed_len == 0) return 0;

  const uint8_t header = (uint8_t)((path == nullptr ? kRouteFlood : kRouteDirect) | (kTypeAnonReq << 2));
  size_t path_bytes = 0;
  const size_t at = writeRoute(out, cap, header, path, path_len, &path_bytes);
  if (at == 0) return 0;

  // createAnonDatagram (Mesh.cpp:520-533): dest hash, the SENDER'S FULL PUBLIC KEY, then MAC+ciphertext.
  const size_t need = at + 1 + kPubKeyBytes + sealed_len;
  if (need > cap || need > 2 + path_bytes + meshcore::kMaxPayload) return 0;
  size_t o = at;
  out[o++] = dest_pub[0];
  memcpy(&out[o], self.pub, kPubKeyBytes);
  o += kPubKeyBytes;
  memcpy(&out[o], sealed, sealed_len);
  return o + sealed_len;
}

size_t buildStatusRequest(const meshcore::Identity& self, const uint8_t dest_pub[kPubKeyBytes],
                          const uint8_t secret[meshcore::kSecretBytes], uint32_t tag, uint32_t random_blob,
                          const uint8_t* path, uint8_t path_len, uint8_t* out, size_t cap) {
  if (dest_pub == nullptr || secret == nullptr || out == nullptr) return 0;
  // temp = tag(4) | REQ_TYPE_GET_STATUS | reserved(4 zero) | random(4)   (BaseChatMesh.cpp:690-700)
  uint8_t temp[13];
  putLe32(temp, tag);
  temp[4] = kReqTypeGetStatus;
  memset(&temp[5], 0, 4);
  putLe32(&temp[9], random_blob);

  uint8_t sealed[meshcore::kMaxPayload];
  const size_t sealed_len = meshcore::sealPayload(secret, sealed, sizeof(sealed), temp, sizeof(temp));
  if (sealed_len == 0) return 0;

  const uint8_t header = (uint8_t)((path == nullptr ? kRouteFlood : kRouteDirect) | (kTypeReq << 2));
  size_t path_bytes = 0;
  const size_t at = writeRoute(out, cap, header, path, path_len, &path_bytes);
  if (at == 0) return 0;

  // createDatagram (Mesh.cpp:490-509): dest hash, src hash, then MAC+ciphertext.
  const size_t need = at + 2 + sealed_len;
  if (need > cap) return 0;
  size_t o = at;
  out[o++] = dest_pub[0];
  out[o++] = self.pub[0];
  memcpy(&out[o], sealed, sealed_len);
  return o + sealed_len;
}

bool openResponse(const meshcore::Identity& self, const uint8_t from_pub[kPubKeyBytes],
                  const uint8_t secret[meshcore::kSecretBytes], const meshcore::View& pkt, uint8_t* plain,
                  size_t plain_cap, size_t* plain_len) {
  if (plain == nullptr || plain_len == nullptr || from_pub == nullptr || secret == nullptr) return false;
  *plain_len = 0;
  if (pkt.type() != kTypeResponse || pkt.payload == nullptr) return false;
  if (pkt.payload_len < 2 + meshcore::kMacBytes + meshcore::kBlockBytes) return false;
  if (pkt.payload[0] != self.pub[0]) return false;    // not addressed to us
  if (pkt.payload[1] != from_pub[0]) return false;    // not the repeater we asked
  const uint8_t* mac_and_data = pkt.payload + 2;
  const size_t sealed_len = (size_t)pkt.payload_len - 2;
  const size_t n = meshcore::openPayload(secret, plain, plain_cap, mac_and_data, sealed_len);
  if (n == 0) return false;
  *plain_len = n;
  return true;
}

bool parseScanAnswer(const meshcore::View& pkt, uint32_t want_tag, int8_t snr_out4, Found* out) {
  if (out == nullptr || pkt.payload == nullptr) return false;
  if (pkt.type() != kTypeControl || pkt.payload_len < 6) return false;
  const uint8_t* p = pkt.payload;
  if ((p[0] & 0xF0) != kCtlDiscoverResp) return false;
  if ((p[0] & 0x0F) != kAdvTypeRepeater) return false;
  uint32_t tag = 0;
  memcpy(&tag, &p[2], 4);
  if (tag != want_tag) return false;                  // another scan's answer
  const int key_len = (int)pkt.payload_len - 6;
  if (key_len < 6) return false;

  *out = Found();
  memcpy(out->prefix, &p[6], 6);
  if (key_len >= kPubKeyBytes) {
    memcpy(out->key, &p[6], kPubKeyBytes);
    out->keyLen = (uint8_t)kPubKeyBytes;
  } else {
    memcpy(out->key, &p[6], (size_t)key_len);
    out->keyLen = (uint8_t)key_len;
  }
  out->hasSnrIn = true;
  out->snrIn4 = (int8_t)p[1];
  out->hasSnrOut = true;
  out->snrOut4 = snr_out4;
  out->hops = pkt.hash_count;
  return true;
}

bool parseLoginAnswer(const uint8_t* plain, size_t len, LoginAnswer* out) {
  if (plain == nullptr || out == nullptr) return false;
  if (len < 4) return false;
  uint32_t clock = 0;
  memcpy(&clock, plain, 4);
  LoginAnswer a;
  if (!loginParse(plain + 4, (int)(len - 4), &a)) return false;
  a.clock = clock;
  a.clockValid = clock >= 1735689600u;   // 2025-01-01: 2024 is the MeshCore restart clock
  *out = a;
  return true;
}

bool parseStatusAnswer(const uint8_t* plain, size_t len, Status* out) {
  if (plain == nullptr || out == nullptr) return false;
  if (len < 4 + (size_t)kRepeaterStatsBytes) return false;
  return statusParse(plain + 4, (int)(len - 4), out);
}

}  // namespace rptr
}  // namespace om
