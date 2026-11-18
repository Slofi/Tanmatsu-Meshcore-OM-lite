// SPDX-License-Identifier: MIT
// Direct messages, slice 2: SEND, and the delivered mark that proves it arrived.
//
// Every rule is a port of the L2's own MeshCore library (firmware/lib/meshcore, v1.18.0 + its companion), read
// in S513/EST-436 — not invented:
//   · compose: helpers/BaseChatMesh.cpp:451-471 composeMsgPacket
//         plain = ts(4, little-endian) | (attempt & 3) | text        (len = 5 + text_len, no NUL)
//         attempt > 3 appends NUL + the full attempt byte; text_len must then be <= MAX_TEXT_LEN-2
//         expected_ack = sha256(plain[0 .. 5+text_len) || OUR pub_key)[0..4]
//     and the frame is createDatagram(TXT_MSG, ...) (Mesh.cpp:488-505):
//         payload = THEIR pub[0] | OUR pub[0] | encryptThenMAC(shared secret, plain)
//   · send: BaseChatMesh.cpp:473-492 sendMessage — no stored out_path -> the usual 2-byte-hash FLOOD
//     (path_len 0x40, count 0); otherwise DIRECT along out_path with that path_len byte copied back.
//   · the timeouts are the companion's arithmetic, MyMesh.cpp:888-895 with the constants at :108-111:
//         flood  = 500 + 16 * airtime_ms
//         direct = 500 + (airtime_ms * 6 + 250) * (path_hash_count + 1)
//   · learning the route + the delivered mark: a PATH packet (Mesh.cpp:160-180) decrypts to
//         path_len | path | extra_type | extra        (extra may be zero-padded)
//     and helpers/BaseChatMesh.cpp:356-370 stores out_path for that contact, then, when
//     extra_type == ACK, matches the ACK. A plain ACK packet (Mesh.cpp:117-128, payload = a 4-byte crc)
//     is matched the same way (BaseChatMesh.cpp:372-384); MyMesh::processAck compares the first FOUR
//     bytes only (MyMesh.cpp:417-438) and notes "the same ACK can be received multiple times".
//
// The retry rule here is the ticket's, and PROGRESS says exactly what upstream does: the retry loop is NOT in
// BaseChatMesh/MyMesh in this tree (BaseChatMesh::loop only fires onSendTimeout(), which the companion leaves
// empty; the phone app drives attempts). The L2's own app retried on timeout, but never forced a flood and never
// cleared a path. So, per the ticket when the companion loop is out of tree: retry on timeout, the LAST attempt
// is a flood, and a failed DIRECT attempt clears the stored path.
//
// Nothing here allocates, and every length that came off the air is checked before it is used.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_dm.h"
#include "mc_identity.h"
#include "mc_wire.h"

namespace meshcore {

constexpr uint8_t kDmMaxAttempts = 3;   // attempt 0, 1, 2 — the ticket's rule
constexpr uint8_t kDmRouteFlood  = 0x01;

constexpr size_t kDmFrameBytes = 256;   // one MeshCore frame (the radio's own packet buffer)

// One composed DM: the complete frame, the ACK the recipient will send back, and whether it went as a flood.
struct DmComposed {
    uint8_t  frame[kDmFrameBytes];   // the exact bytes to transmit
    size_t   frame_len = 0;
    uint8_t  expected_ack[4] = {0};   // sha256(plain || OUR pub)[0..4], BaseChatMesh.cpp:461-462
    bool     flood = true;
};

// Compose one attempt of a DM to `peer_pub`, sealed with `secret` (identitySharedSecret).
// `out_path` nullptr OR `out_path_len == kDmPathUnknown` (0xFF, OUT_PATH_UNKNOWN) = no route -> a flood; every
// other VALID path encoding is a DIRECT frame copying `out_path_len` and its bytes. A path count of 0 is a valid
// direct route: the recipient is a neighbour heard directly, and MeshCore's sendMessage() sends it that way
// (BaseChatMesh.cpp:481-486 tests only OUT_PATH_UNKNOWN). False when the text does not fit or a length is bad.
bool dmCompose(const Identity& self, const uint8_t peer_pub[kPubKeyBytes],
               const uint8_t secret[kSecretBytes], uint32_t timestamp, uint8_t attempt, const char* text,
               const uint8_t* out_path, uint8_t out_path_len, DmComposed* out);

// ---- the route we learn from a PATH packet ---------------------------------------------------------------
constexpr uint8_t  kDmPathUnknown    = 0xFF;   // OUT_PATH_UNKNOWN (MeshCore.h:19) — path_len 0xFF, never a route

struct DmPath {
    int      peer = -1;                 // index into the caller's contact array (the MAC decided it)
    uint8_t  out_path[kMaxPath] = {0};  // the route TO us, i.e. what we store as the contact's out_path
    uint8_t  out_path_len = kDmPathUnknown;
    uint8_t  extra_type = 0;
    uint8_t  extra[kMaxPayload] = {0};
    uint16_t extra_len = 0;
};

// Open a PATH packet against `peer_count` contacts packed as 32-byte public keys. The decrypted data is
//   path_len | path(hash_count * hash_size) | extra_type | extra
// (Mesh.cpp:160-180). `path_len` is validated exactly as Packet::isValidPathLen does (Packet.cpp:13-18) and
// every offset is bounds-checked against the decrypted length BEFORE it is used, so a truncated or a
// 0xFF-length PATH is refused rather than read past.
bool dmOpenPath(const Identity& self, const View& pkt, const uint8_t* peer_pubs, size_t peer_count, DmPath* out);

// ---- the pending table: expected ACK -> which message, and when to give up ---------------------------------
constexpr size_t kDmPendingSlots = 8;

struct DmPending {
    bool     used = false;
    uint8_t  ack[4] = {0};
    uint8_t  peer_pub[kPubKeyBytes] = {0};
    uint32_t msg_id = 0;      // the caller's message id (the log id / seq)
    uint32_t deadline_ms = 0;
    uint8_t  attempt = 0;
    bool     direct = false;  // this attempt went along a stored path
};

struct DmPendingTable {
    DmPending slot[kDmPendingSlots];
};

// Record a frame that just went on the air. The oldest entry is reused when the table is full, so a new send is
// never lost (the L2's DmDeliveryTable rule). False only for bad arguments.
bool dmPendingBegin(DmPendingTable& t, const uint8_t peer_pub[kPubKeyBytes], uint32_t msg_id,
                    const uint8_t ack[4], uint32_t timeout_ms, bool direct, uint32_t now_ms);

// Point an existing entry at a retry's new ACK/timeout (the attempt byte is part of the ACK hash).
bool dmPendingRearm(DmPendingTable& t, uint32_t msg_id, const uint8_t ack[4], uint32_t timeout_ms, bool direct,
                    uint32_t now_ms);

// The frame never reached the radio (the send queue was full), so no ACK can arrive for it: forget the entry.
bool dmPendingDrop(DmPendingTable& t, uint32_t msg_id);

// The retry could not be QUEUED (the radio queue is full): wait a little and try the SAME attempt again, without
// spending it. Only the deadline moves — the attempt counter is what dmPendingTimeout() advances.
bool dmPendingDelay(DmPendingTable& t, uint32_t msg_id, uint32_t extra_ms, uint32_t now_ms);

// A received 4-byte ACK crc. True and the slot is CLEARED when it matches (MyMesh.cpp:430-434: the same ACK can
// arrive more than once, so a cleared slot simply does not match again). Fills the message id.
bool dmPendingAck(DmPendingTable& t, const uint8_t ack[4], uint32_t* msg_id_out);

enum class DmTimeoutAction : uint8_t { None, Retry, GiveUp };

struct DmTimeout {
    DmTimeoutAction action = DmTimeoutAction::None;
    uint32_t        msg_id = 0;
    uint8_t         attempt = 0;   // the attempt that is DUE
    bool            flood = false; // send it as a flood
    bool            clear_path = false;  // the timed-out attempt was direct: forget the stored route
};

// The retry rule (the ticket's, since the companion's loop is out of tree): at the deadline, the next attempt
// is due while fewer than kDmMaxAttempts have gone; the LAST attempt is a flood even if a path is stored, and a
// failed DIRECT attempt clears the stored path. A spent entry is removed and answers GiveUp.
DmTimeout dmPendingTimeout(DmPendingTable& t, uint32_t now_ms);

// ---- timeouts (MyMesh.cpp:888-895, constants at :108-111) ---------------------------------------------------
uint32_t dmFloodTimeoutMillis(uint32_t airtime_ms);
uint32_t dmDirectTimeoutMillis(uint32_t airtime_ms, uint8_t out_path_len);

// The airtime those two rules want, estimated from the frame's length. The Tanmatsu's lora component (the C6) has
// no getEstAirtimeFor(), so this is the SX1262 time-on-air formula at THIS radio's own numbers (core/radio_plan.h
// kSloPlan: SF8, BW 62.5 kHz, CR 4/8, preamble 32, CRC on, LDRO off), in integer microseconds. It only has to be
// right enough to size a timeout; the ACK still decides delivery.
uint32_t dmAirtimeMillis(size_t frame_bytes);

// ---- the ACK packet -----------------------------------------------------------------------------------------
// A plain ACK as Mesh.cpp:117-128 carries it: payload = the 4-byte crc, nothing else (the frame already says
// ACK in its header). Returns the payload ONLY when it is exactly that; nullptr otherwise.
const uint8_t* dmAckPayload(const View& pkt);

}  // namespace meshcore
