// SPDX-License-Identifier: MIT
// See node_ops.h for the upstream file:line each rule came from.
#include "node_ops.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace ops {
namespace {

void putLe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

}  // namespace

size_t buildPingFrame(const meshcore::Identity& self, const uint8_t dest_pub[meshcore::kPubKeyBytes],
                      const uint8_t secret[meshcore::kSecretBytes], uint32_t tag, uint32_t random_blob,
                      const uint8_t* path, uint8_t path_len, uint8_t* out, size_t cap) {
    // sendRequest() builds exactly the EST-453 status request; a second builder would be a second format.
    return rptr::buildStatusRequest(self, dest_pub, secret, tag, random_blob, path, path_len, out, cap);
}

bool openPingResponse(const meshcore::Identity& self, const uint8_t from_pub[meshcore::kPubKeyBytes],
                      const uint8_t secret[meshcore::kSecretBytes], const meshcore::View& pkt) {
    uint8_t plain[meshcore::kMaxPayload];
    size_t  plain_len = 0;
    if (!rptr::openResponse(self, from_pub, secret, pkt, plain, sizeof(plain), &plain_len)) return false;
    // Every MeshCore answer starts with the responder's 4-byte timestamp (mesh_link.cpp:parseStatusAnswer).
    return plain_len >= 4;
}

void pingStarted(PingJob& j, const uint8_t key[meshcore::kPubKeyBytes], const char* name, uint32_t tag,
                 uint32_t now_ms) {
    j.state = PingState::Sent;
    j.tag   = tag;
    j.sent_ms     = now_ms;
    j.deadline_ms = now_ms + kPingTimeoutMs;
    j.rtt_ms      = 0;
    if (key != nullptr) memcpy(j.key, key, meshcore::kPubKeyBytes);
    snprintf(j.name, sizeof(j.name), "%s", name != nullptr ? name : "");
}

void pingReplied(PingJob& j, uint32_t now_ms) {
    if (j.state != PingState::Sent) return;   // an answer to nothing, or to an older ping, changes nothing
    j.state  = PingState::Replied;
    j.rtt_ms = now_ms - j.sent_ms;
}

void pingTick(PingJob& j, uint32_t now_ms) {
    if (j.state == PingState::Sent && (int32_t)(now_ms - j.deadline_ms) >= 0) j.state = PingState::Timeout;
}

void pingFailed(PingJob& j) { j.state = PingState::Failed; }

void pingCancel(PingJob& j) {
    if (j.state == PingState::Sent) j.state = PingState::Idle;
}

void pingText(char* out, size_t cap, const PingJob& j) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    const char* who = j.name[0] != '\0' ? j.name : "this node";
    switch (j.state) {
        case PingState::Sent:
            snprintf(out, cap, "Ping %s: waiting...", who);
            break;
        case PingState::Replied:
            // The ticket's words: the round trip in seconds, one decimal.
            snprintf(out, cap, "Ping %s: %.1f s", who, (double)j.rtt_ms / 1000.0);
            break;
        case PingState::Timeout:
            snprintf(out, cap, "Ping %s: no reply", who);
            break;
        case PingState::Failed:
            snprintf(out, cap, "Ping: send failed");
            break;
        case PingState::Idle:
        default:
            break;
    }
}

size_t buildTraceFrame(uint32_t tag, uint32_t auth_code, uint8_t hash_size, const uint8_t* path,
                       uint8_t path_bytes, uint8_t* out, size_t cap) {
    // S514 (Claude): upstream codes the trace entry size as 1 << (flags & 3) (Mesh.cpp:701), i.e. 1, 2, 4 or 8
    // bytes, so a 3-byte path hash cannot be traced: refuse it instead of sending 4-byte entries.
    if (out == nullptr || (hash_size != 1 && hash_size != 2)) return 0;
    if (path_bytes > 0 && path == nullptr) return 0;
    const size_t need = 2 + 9 + (size_t)path_bytes;
    if (need > cap) return 0;
    // sendDirect() for TRACE: path appended to the payload, the header's path_len stays 0.
    out[0] = (uint8_t)((0x09 << 2) | rptr::kRouteDirect);
    out[1] = 0;
    putLe32(&out[2], tag);
    putLe32(&out[6], auth_code);
    out[10] = traceFlagsFor(hash_size);
    if (path_bytes > 0) memcpy(&out[11], path, path_bytes);
    return need;
}

bool parseTraceResponse(const meshcore::View& pkt, int8_t snr4_final, HopNamer namer, void* namer_ctx,
                        TraceView* out) {
    if (out == nullptr) return false;
    if (pkt.route() != rptr::kRouteDirect || pkt.type() != 0x09) return false;
    if (pkt.payload == nullptr || pkt.payload_len < 9) return false;

    const uint8_t flags     = pkt.payload[8];
    const uint8_t hash_bytes = (uint8_t)(1u << (flags & 0x03));
    if (hash_bytes < 1 || hash_bytes > 3) return false;

    const uint8_t* hashes      = pkt.payload + 9;
    const size_t   hash_bytes_len = (size_t)pkt.payload_len - 9;
    const size_t   count       = hash_bytes_len / hash_bytes;
    // One SNR byte per hop lives in the received frame's path (Mesh.cpp:42-57): never more than the path carries
    // and never more than the hashes name.
    size_t hops = pkt.path_len;
    if (hops > count) hops = count;
    if (hops > (size_t)kMaxTraceHops) hops = (size_t)kMaxTraceHops;

    TraceView v;
    v.count     = (int)hops;
    v.finalSnr4 = snr4_final;
    v.haveRtt   = true;
    for (size_t i = 0; i < hops; ++i) {
        TraceHop& h = v.hops[i];
        h.hash_len  = hash_bytes;
        memcpy(h.hash, hashes + i * hash_bytes, hash_bytes);
        h.snr4  = pkt.path != nullptr ? (int8_t)pkt.path[i] : 0;
        h.known = false;
        h.name[0] = '\0';
        if (namer != nullptr && namer(h.hash, hash_bytes, namer_ctx, h.name, sizeof(h.name))) {
            h.known = h.name[0] != '\0';
        }
        if (!h.known) traceHopLabel(h.name, sizeof(h.name), h);
    }
    *out = v;
    return true;
}

void traceHopLabel(char* out, size_t cap, const TraceHop& hop) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    if (hop.known && hop.name[0] != '\0') {
        snprintf(out, cap, "%s", hop.name);
        return;
    }
    size_t n = 0;
    for (uint8_t b = 0; b < hop.hash_len && b < sizeof(hop.hash) && n + 3 < cap; ++b) {
        n += (size_t)snprintf(out + n, cap - n, "%02x", hop.hash[b]);
    }
}

void traceTotalText(char* out, size_t cap, const TraceView& t) {
    if (out == nullptr || cap == 0) return;
    snprintf(out, cap, "%d hop%s \xc2\xb7 RTT %.1f s", t.count, t.count == 1 ? "" : "s",
             (double)t.rttMs / 1000.0);
}

void traceStarted(TraceJob& j, const char* node, uint32_t now_ms) {
    j.pending     = true;
    j.sent_ms     = now_ms;
    j.deadline_ms = now_ms + kTraceTimeoutMs;
    snprintf(j.node, sizeof(j.node), "%s", node != nullptr ? node : "");
}

void traceTick(TraceJob& j, uint32_t now_ms) {
    if (j.pending && (int32_t)(now_ms - j.deadline_ms) >= 0) j.pending = false;
}

void traceCancel(TraceJob& j) { j.pending = false; }

}  // namespace ops
}  // namespace om
