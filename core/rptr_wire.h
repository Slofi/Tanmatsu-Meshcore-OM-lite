// SPDX-License-Identifier: MIT
// RPTR part 1 (S514/EST-453): the four MeshCore packets this page needs, ported BYTE FOR BYTE from the
// upstream library the L2 vendors. The Tanmatsu does not link upstream, so every rule below was read there:
//
//   · frame on the air (Packet.cpp:37-80): header, [transport 4], path_len, path, payload. `path_len` is
//     ((hash_size - 1) << 6) | hash_count; a zero-hop packet is path_len 0.
//   · scan request (Mesh.cpp:621-633 createControlData + Mesh.cpp:717-726 sendZeroHop):
//         header = (PAYLOAD_TYPE_CONTROL(0x0B) << 2) | ROUTE_TYPE_DIRECT(0x02)
//         path_len = 0
//         payload = [0x80 | (prefix_only ? 0 : 0), filter, tag(4 LE), since(4 LE)]
//     The companion's CMD_SEND_CONTROL_DATA is exactly createControlData(&frame[1], len-1) + sendZeroHop,
//     and the L2 builds the same 10 bytes (firmware/src/rptr_link.cpp:478-491).
//   · login (helpers/BaseChatMesh.cpp:603-629 sendLogin + Mesh.cpp:512-538 createAnonDatagram):
//         header = (PAYLOAD_TYPE_ANON_REQ(0x07) << 2) | route
//         payload = dest_hash(1) + SENDER PUB_KEY(32) + encryptThenMAC(secret, temp)
//         temp    = RTC(4 LE) + password (at most 15 chars)
//     An unknown route is a flood (route 0x01, 2-byte path hashes, count 0); a known route is DIRECT.
//   · status request (helpers/BaseChatMesh.cpp:690-715 sendRequest + Mesh.cpp:490-509 createDatagram):
//         header = (PAYLOAD_TYPE_REQ(0x00) << 2) | route
//         payload = dest_hash(1) + src_hash(1) + encryptThenMAC(secret, temp)
//         temp    = tag(4 LE) + REQ_TYPE_GET_STATUS(0x01) + reserved(4 zero) + random(4)
//   · replies (Mesh.cpp:133-205): a RESPONSE payload is dest_hash(1) + src_hash(1) + sealed. The decrypted
//     plaintext starts with the repeater's 4-byte timestamp, then the login answer or the 56-byte
//     RepeaterStats. This file strips the timestamp and hands the body to rptr_model.
//
// Every length that came off the air is checked before it is used: the parsers are in the ASan stage.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_identity.h"
#include "mc_wire.h"
#include "rptr_model.h"

namespace om {
namespace rptr {

// MeshCore constants this port names (Packet.h:8-30, helpers/BaseChatMesh.h:18-21).
constexpr uint8_t kTypeReq = 0x00;
constexpr uint8_t kTypeResponse = 0x01;
constexpr uint8_t kTypeTxtMsg = 0x02;   // Packet.h:22: a TXT_MSG datagram (a DM, and the CLI's own carrier)
constexpr uint8_t kTypeAnonReq = 0x07;
constexpr uint8_t kTypeControl = 0x0B;
constexpr uint8_t kRouteFlood = 0x01;
constexpr uint8_t kRouteDirect = 0x02;
constexpr uint8_t kReqTypeGetStatus = 0x01;
constexpr uint8_t kCtlDiscoverReq = 0x80;
constexpr uint8_t kCtlDiscoverResp = 0x90;
constexpr uint8_t kAdvTypeRepeater = 2;
// sendFlood() uses the app's flood hash size (2 on this mesh); a DIRECT frame copies the contact's path_len.
constexpr uint8_t kFloodPathLen = 0x40;

// The scan request. `tag` is the random value the answers must echo. Returns the frame length or 0.
size_t buildScan(uint32_t tag, uint8_t* out, size_t cap);

// The login request. `path` nullptr means "no route yet" -> a flood; otherwise `path_len` is the contact's
// stored byte and the frame is DIRECT. `secret` is identitySharedSecret(self, dest_pub).
size_t buildLogin(const meshcore::Identity& self, const uint8_t dest_pub[kPubKeyBytes],
                  const uint8_t secret[meshcore::kSecretBytes], uint32_t timestamp, const char* password,
                  const uint8_t* path, uint8_t path_len, uint8_t* out, size_t cap);

// The status request. Same route rule as buildLogin.
size_t buildStatusRequest(const meshcore::Identity& self, const uint8_t dest_pub[kPubKeyBytes],
                          const uint8_t secret[meshcore::kSecretBytes], uint32_t tag, uint32_t random_blob,
                          const uint8_t* path, uint8_t path_len, uint8_t* out, size_t cap);

// Open a RESPONSE addressed to us and sourced by `from_pub`. True and the decrypted bytes (still starting with
// the repeater's 4-byte timestamp) on success. False for another type, another destination, a bad MAC or a
// frame too short to be a datagram.
bool openResponse(const meshcore::Identity& self, const uint8_t from_pub[kPubKeyBytes],
                  const uint8_t secret[meshcore::kSecretBytes], const meshcore::View& pkt, uint8_t* plain,
                  size_t plain_cap, size_t* plain_len);

// The discovery answer. False for another frame, another node type, another scan's tag, or a key shorter than
// six bytes. `snr_out4` is the radio's own SNR for the frame, not a field of the packet.
bool parseScanAnswer(const meshcore::View& pkt, uint32_t want_tag, int8_t snr_out4, Found* out);

// The bodies after the 4-byte repeater timestamp has been stripped.
bool parseLoginAnswer(const uint8_t* plain, size_t len, LoginAnswer* out);
bool parseStatusAnswer(const uint8_t* plain, size_t len, Status* out);

// ------------------------------------------------------------------------------- EST-470: the CLI path
// Upstream BaseChatMesh::sendCommandData (helpers/BaseChatMesh.cpp:492-516) builds a TXT_MSG datagram whose
// plaintext is
//     ts(4 LE) | flags | text,   flags = (attempt & 3) | (TXT_TYPE_CLI_DATA << 2)
// TXT_TYPE_CLI_DATA = 1 (helpers/TxtDataHelpers.h:7), so the flags byte is 0x04 for attempt 0. The datagram is
// dest_hash(1) + src_hash(1) + encryptThenMAC(secret, plaintext) (Mesh.cpp:490-509), sent DIRECT down the route
// learned at login or as a flood when there is none, which is sendCommandData's `out_path_len == OUT_PATH_UNKNOWN`
// branch. `path` nullptr means "no route yet" -> flood.
constexpr uint8_t kTxtTypeCliData = 1;

size_t buildCli(const meshcore::Identity& self, const uint8_t dest_pub[kPubKeyBytes],
                const uint8_t secret[meshcore::kSecretBytes], uint32_t timestamp, const char* line,
                const uint8_t* path, uint8_t path_len, uint8_t* out, size_t cap);

// The repeater's ANSWER: a TXT_MSG with the SAME CLI type, from the node whose key we logged in to (usually not in
// the contact table). openCliAnswer accepts a frame addressed to us and sourced by `from_pub` whose MAC opens with
// `secret`; the TYPE check is parseCliReply's job below, so a PLAIN text from that same node still falls through
// to the DM path and is never swallowed here.
bool openCliAnswer(const meshcore::Identity& self, const uint8_t from_pub[kPubKeyBytes],
                   const uint8_t secret[meshcore::kSecretBytes], const meshcore::View& pkt, uint8_t* plain,
                   size_t plain_cap, size_t* plain_len);

// The decrypted CLI answer. Upstream (reference/meshcore-repeater-v1.18.0/MyMesh.cpp:743-757) sends
//     ts(4 LE) | (TXT_TYPE_CLI_DATA << 2) | text
// where ts is the repeater's own clock. Only a CLI-typed text is accepted, so this is also the routing decision:
// a frame that is not a CLI answer returns false and the DM code gets its chance.
struct CliReply {
  uint32_t timestamp = 0;
  uint8_t  txt_type = 0;
  char     text[meshcore::kMaxText + 1] = "";
};
bool parseCliReply(const uint8_t* plain, size_t len, CliReply* out);

}  // namespace rptr
}  // namespace om
