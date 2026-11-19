// SPDX-License-Identifier: MIT
// S514/EST-456: the two L2 node actions the Tanmatsu did not have yet — PING and TRACE.
//
// Every rule below is the L2's, read from firmware/src/mesh_link.cpp and firmware/src/ui/node_menu.h:
//   · PING is BaseChatMesh::sendRequest(REQ_TYPE_GET_STATUS): the request frame is the EST-453 status request
//     (core/rptr_wire.* buildStatusRequest, reused — not a second copy), its answer is a RESPONSE opened by
//     rptr::openResponse(). States are the L2's Idle/Sent/Replied/Timeout/Failed, the timeout is the L2's
//     floor (8 s, rptr_link.cpp kEstimateMs), and the toast on a reply is "Ping <name>: 1.8 s".
//   · TRACE is upstream Mesh::createTrace() + sendDirect() along the contact's stored out_path
//     (firmware/lib/meshcore/Mesh.cpp:605-620 and 692-714):
//         payload = tag(4 LE) + auth_code(4 LE) + flags(1) + the path's hash bytes
//         header  = (PAYLOAD_TYPE_TRACE(0x09) << 2) | ROUTE_TYPE_DIRECT(0x02), path_len = 0
//     flags' low two bits are the path hash size code (0 = 1 B, 1 = 2 B, 2 = 3 B). The answer is parsed like
//     Mesh::onRecvPacket + mesh_link.cpp's onTraceRecv: pkt.path holds one SNR (x4) per hop, the payload after
//     the 9-byte head holds the hop hashes, hash_bytes = 1 << (flags & 3).
//
// Plain C++17, no display / NVS / radio: the host tests drive exactly these decisions.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_wire.h"
#include "rptr_wire.h"

namespace om {
namespace ops {

// The L2's timeout floor for a status request (rptr_link.cpp kEstimateMs) and its trace deadline
// (mesh_link.cpp: meshNodeTrace sets 12 000 ms).
constexpr uint32_t kPingTimeoutMs  = 8000;
constexpr uint32_t kTraceTimeoutMs = 12000;
constexpr int      kMaxTraceHops   = 8;
constexpr size_t   kTraceNameBytes = 32;

enum class PingState : uint8_t { Idle = 0, Sent, Replied, Timeout, Failed };

// One trace hop, the L2's TraceHop (firmware/src/ui/node_menu.h): the 2-byte path hash is shown as hex when no
// known repeater matches, otherwise its name.
struct TraceHop {
    char    name[kTraceNameBytes] = {0};
    uint8_t hash[3]               = {0};
    uint8_t hash_len              = 0;
    int8_t  snr4                  = 0;   // SNR x 4, MeshCore's own unit
    bool    known                 = false;
};

struct TraceView {
    TraceHop hops[kMaxTraceHops];
    int      count     = 0;
    int8_t   finalSnr4 = 0;    // the trace's own SNR at our receiver
    uint32_t rttMs     = 0;
    bool     haveRtt   = false;
    char     node[kTraceNameBytes] = {0};
};

// A hop's hash -> a known repeater's name. `ctx` is the caller's contact table; true fills `out`. Pure: the tests
// pass their own table, the device passes its contact store.
using HopNamer = bool (*)(const uint8_t* hash, uint8_t hash_len, void* ctx, char* out, size_t cap);

// --- PING -------------------------------------------------------------------------------------------
// The request frame, REUSING rptr::buildStatusRequest: `secret` is identitySharedSecret(self, dest). A known
// route makes a DIRECT frame, no route a flood (BaseChatMesh.cpp:690-715). 0 = refused.
size_t buildPingFrame(const meshcore::Identity& self, const uint8_t dest_pub[meshcore::kPubKeyBytes],
                      const uint8_t secret[meshcore::kSecretBytes], uint32_t tag, uint32_t random_blob,
                      const uint8_t* path, uint8_t path_len, uint8_t* out, size_t cap);

// Open the ping's RESPONSE with the EST-453 parser and require the 4-byte timestamp every answer starts with.
bool openPingResponse(const meshcore::Identity& self, const uint8_t from_pub[meshcore::kPubKeyBytes],
                      const uint8_t secret[meshcore::kSecretBytes], const meshcore::View& pkt);

// A job is one outstanding ping: `key` is the contact's public key, so only that contact's answer counts.
struct PingJob {
    PingState state       = PingState::Idle;
    uint32_t  tag         = 0;
    uint32_t  sent_ms     = 0;
    uint32_t  deadline_ms = 0;
    uint32_t  rtt_ms      = 0;
    uint8_t   key[meshcore::kPubKeyBytes] = {0};
    char      name[24]    = {0};
};

// The pure state machine: one start, one answer, one deadline.
void pingStarted(PingJob& j, const uint8_t key[meshcore::kPubKeyBytes], const char* name, uint32_t tag,
                 uint32_t now_ms);
void pingReplied(PingJob& j, uint32_t now_ms);   // only from Sent
void pingTick(PingJob& j, uint32_t now_ms);      // Sent + past the deadline -> Timeout
void pingFailed(PingJob& j);
// Esc: stop waiting; the packet already queued is not recalled (it may already be on the air).
void pingCancel(PingJob& j);

// "Ping <name>: 1.8 s" / "Ping <name>: no reply" / "Ping: send failed" / "Ping <name>: waiting...".
void pingText(char* out, size_t cap, const PingJob& j);

// --- TRACE ------------------------------------------------------------------------------------------
// Upstream createTrace() + sendDirect() for a TRACE: the path bytes go at the END OF THE PAYLOAD and the
// header's path_len is 0. `hash_size` is 1..3. 0 = refused (cap too small / bad arguments).
size_t buildTraceFrame(uint32_t tag, uint32_t auth_code, uint8_t hash_size, const uint8_t* path,
                       uint8_t path_bytes, uint8_t* out, size_t cap);

// Parse a DIRECT TRACE frame that came back. `snr4_final` is the radio's own SNR for the whole frame.
// Bounds-checked: every length off the air is compared before it is used. False for another type/route or a
// frame too short to be a trace; a zero-hop answer is a VALID result (count 0, haveRtt true).
bool parseTraceResponse(const meshcore::View& pkt, int8_t snr4_final, HopNamer namer, void* namer_ctx,
                        TraceView* out);

// The hop's display name: the known repeater's name, else the hash as hex ("4b91" in 2-byte mode).
void traceHopLabel(char* out, size_t cap, const TraceHop& hop);

// The result's total line: "3 hops · RTT 2.4 s". The rows are the hops; this is the total.
void traceTotalText(char* out, size_t cap, const TraceView& t);

// A pending job's deadline and its cancel; the same state machine shape as the ping.
struct TraceJob {
    bool     pending     = false;
    uint32_t sent_ms     = 0;
    uint32_t deadline_ms = 0;
    char     node[kTraceNameBytes] = {0};
};
void traceStarted(TraceJob& j, const char* node, uint32_t now_ms);
void traceTick(TraceJob& j, uint32_t now_ms);    // pending + past the deadline -> not pending
void traceCancel(TraceJob& j);

// 2 bytes is this mesh's path hash mode (Settings > Radio, kFloodPathHashSize); the L2 passes the contact's own
// spec. Kept as one helper so the frame and the parser cannot disagree.
inline uint8_t traceFlagsFor(uint8_t hash_size) { return (uint8_t)((hash_size - 1) & 0x03); }

}  // namespace ops
}  // namespace om
