// SPDX-License-Identifier: MIT
#include "dm_send.h"

#include <string.h>

#include "mc_crypto.h"

namespace meshcore {
namespace {

void putLe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// Packet::isValidPathLen (Packet.cpp:13-18): hash_size 4 is reserved, and the count must fit MAX_PATH_SIZE.
bool validPathLen(uint8_t path_len) {
    const uint8_t hash_count = path_len & 63;
    const uint8_t hash_size  = (uint8_t)((path_len >> 6) + 1);
    if (hash_size == 4) return false;
    return (size_t)hash_count * hash_size <= kMaxPath;
}

}  // namespace

bool dmCompose(const Identity& self, const uint8_t peer_pub[kPubKeyBytes],
               const uint8_t secret[kSecretBytes], uint32_t timestamp, uint8_t attempt, const char* text,
               const uint8_t* out_path, uint8_t out_path_len, DmComposed* out) {
    if (peer_pub == nullptr || secret == nullptr || text == nullptr || out == nullptr) return false;

    const size_t text_len = strlen(text);
    if (text_len > kMaxText) return false;                             // BaseChatMesh.cpp:453
    if (attempt > 3 && text_len > kMaxText - 2) return false;          // BaseChatMesh.cpp:454

    // plain = ts(4) | (attempt & 3) | text          (BaseChatMesh.cpp:456-459)
    uint8_t plain[5 + kMaxText + 2];
    putLe32(plain, timestamp);
    plain[4] = (uint8_t)(attempt & 3);
    memcpy(&plain[5], text, text_len);
    size_t plain_len = 5 + text_len;

    // The ACK the RECIPIENT will send: sha256(plain[0 .. 5+text_len) || OUR pub)[0..4]. Only the five bytes
    // BEFORE any extended-attempt tail are hashed, and the hash needs no NUL (BaseChatMesh.cpp:461-462).
    uint8_t buf[5 + kMaxText + kPubKeyBytes];
    memcpy(buf, plain, plain_len);
    memcpy(buf + plain_len, self.pub, kPubKeyBytes);
    uint8_t full[32];
    if (!sha256(buf, plain_len + kPubKeyBytes, full)) return false;
    memcpy(out->expected_ack, full, 4);

    // attempt > 3: one NUL and the FULL attempt byte at the tail, so the frame hash stays unique
    // (BaseChatMesh.cpp:464-468). Our rule stops at attempt 2, but the port is faithful.
    if (attempt > 3) {
        plain[plain_len++] = 0;
        plain[plain_len++] = attempt;
    }

    uint8_t sealed[kMaxPayload];
    const size_t sealed_len = sealPayload(secret, sealed, sizeof(sealed), plain, plain_len);
    if (sealed_len == 0) return false;

    size_t  at    = 0;
    size_t  path_bytes = 0;
    uint8_t prefix_len = 0;
    // OUT_PATH_UNKNOWN (0xFF, ContactInfo.h; MeshCore.h:19) is the ONLY "no route" value. A DIRECT route with a
    // path COUNT of zero is not unknown: it means the recipient is a neighbour heard directly, and sendMessage()
    // sends exactly that (prefix kept, count 0) down sendDirect() — it only tests `out_path_len ==
    // OUT_PATH_UNKNOWN` (BaseChatMesh.cpp:481-486). So the count is allowed to be 0 here.
    out->flood = (out_path == nullptr || out_path_len == kDmPathUnknown);
    if (out->flood) {
        out->frame[0] = (uint8_t)((kTypeTxtMsg << 2) | kDmRouteFlood);
        prefix_len    = (uint8_t)((2 - 1) << 6);   // 2-byte path hashes, count 0 (sendFlood, Mesh.cpp:641-676)
    } else {
        if (!validPathLen(out_path_len)) return false;
        const uint8_t hash_size  = (uint8_t)((out_path_len >> 6) + 1);
        const uint8_t hash_count = (uint8_t)(out_path_len & 63);
        path_bytes               = (size_t)hash_count * hash_size;
        out->frame[0] = (uint8_t)((kTypeTxtMsg << 2) | kRouteDirect);
        prefix_len    = out_path_len;
    }

    const size_t need = 2 + path_bytes + 2 + sealed_len;
    if (need > sizeof(out->frame)) return false;

    out->frame[1] = prefix_len;
    at            = 2;
    if (!out->flood) {
        memcpy(&out->frame[at], out_path, path_bytes);
        at += path_bytes;
    }
    // createDatagram's payload (Mesh.cpp:500-503): dest hash, src hash, then encrypt-then-MAC.
    out->frame[at++] = peer_pub[0];
    out->frame[at++] = self.pub[0];
    memcpy(&out->frame[at], sealed, sealed_len);
    out->frame_len = at + sealed_len;
    return true;
}

bool dmOpenPath(const Identity& self, const View& pkt, const uint8_t* peer_pubs, size_t peer_count, DmPath* out) {
    if (out == nullptr) return false;
    if (pkt.type() != kTypePath) return false;
    if (pkt.payload == nullptr) return false;
    if (pkt.payload_len < 2 + kMacBytes + kBlockBytes) return false;

    const uint8_t dest_hash = pkt.payload[0];
    const uint8_t src_hash  = pkt.payload[1];
    if (dest_hash != self.pub[0]) return false;

    const uint8_t* mac_and_data = pkt.payload + 2;
    const size_t   sealed_len   = (size_t)pkt.payload_len - 2;
    if (sealed_len <= kMacBytes || ((sealed_len - kMacBytes) % kBlockBytes) != 0) return false;
    if (peer_pubs == nullptr) return false;

    for (size_t i = 0; i < peer_count; i++) {
        const uint8_t* peer = peer_pubs + i * kPubKeyBytes;
        if (peer[0] != src_hash) continue;

        uint8_t secret[kSecretBytes];
        if (!identitySharedSecret(self, peer, secret)) continue;

        uint8_t      data[kMaxPayload];
        const size_t n = openPayload(secret, data, sizeof(data), mac_and_data, sealed_len);
        if (n == 0) continue;

        // path_len | path | extra_type | extra        (Mesh.cpp:162-173)
        size_t        k        = 0;
        const uint8_t path_len = data[k++];
        if (!validPathLen(path_len)) continue;   // Mesh.cpp:163-167 rejects a bad encoding
        const uint8_t hash_size  = (uint8_t)((path_len >> 6) + 1);
        const uint8_t hash_count = (uint8_t)(path_len & 63);
        const size_t  path_bytes = (size_t)hash_count * hash_size;

        // Bounds FIRST: the decrypted block is padded, so `n` may be larger than the data, never smaller.
        if (k + path_bytes + 1 > n) continue;
        const uint8_t* path = &data[k];
        k += path_bytes;
        const uint8_t extra_type = (uint8_t)(data[k++] & 0x0F);
        const size_t  extra_len  = n - k;
        if (extra_len > sizeof(out->extra)) continue;

        DmPath p;
        p.peer         = (int)i;
        p.out_path_len = path_len;
        if (path_bytes > 0) memcpy(p.out_path, path, path_bytes);
        p.extra_type = extra_type;
        p.extra_len  = (uint16_t)extra_len;
        if (extra_len > 0) memcpy(p.extra, &data[k], extra_len);
        *out = p;
        return true;
    }
    return false;
}

bool dmPendingBegin(DmPendingTable& t, const uint8_t peer_pub[kPubKeyBytes], uint32_t msg_id,
                    const uint8_t ack[4], uint32_t timeout_ms, bool direct, uint32_t now_ms) {
    if (peer_pub == nullptr || ack == nullptr) return false;

    int at = -1;
    for (size_t i = 0; i < kDmPendingSlots; i++) {
        if (!t.slot[i].used) {
            at = (int)i;
            break;
        }
    }
    if (at < 0) {
        // Full: reuse the oldest deadline, so a new send is never dropped (the L2's DmDeliveryTable rule).
        at = 0;
        for (size_t i = 1; i < kDmPendingSlots; i++) {
            if (t.slot[i].deadline_ms < t.slot[at].deadline_ms) at = (int)i;
        }
    }

    DmPending& e = t.slot[at];
    e.used       = true;
    memcpy(e.ack, ack, 4);
    memcpy(e.peer_pub, peer_pub, kPubKeyBytes);
    e.msg_id      = msg_id;
    e.attempt     = 0;
    e.direct      = direct;
    e.deadline_ms = now_ms + timeout_ms;
    return true;
}

bool dmPendingRearm(DmPendingTable& t, uint32_t msg_id, const uint8_t ack[4], uint32_t timeout_ms, bool direct,
                    uint32_t now_ms) {
    if (ack == nullptr) return false;
    for (size_t i = 0; i < kDmPendingSlots; i++) {
        DmPending& e = t.slot[i];
        if (!e.used || e.msg_id != msg_id) continue;
        memcpy(e.ack, ack, 4);
        e.direct      = direct;
        e.deadline_ms = now_ms + timeout_ms;
        return true;
    }
    return false;
}

bool dmPendingDrop(DmPendingTable& t, uint32_t msg_id) {
    for (size_t i = 0; i < kDmPendingSlots; i++) {
        DmPending& e = t.slot[i];
        if (!e.used || e.msg_id != msg_id) continue;
        e = DmPending{};
        return true;
    }
    return false;
}

bool dmPendingDelay(DmPendingTable& t, uint32_t msg_id, uint32_t extra_ms, uint32_t now_ms) {
    for (size_t i = 0; i < kDmPendingSlots; i++) {
        DmPending& e = t.slot[i];
        if (!e.used || e.msg_id != msg_id) continue;
        // dmPendingTimeout() already moved `attempt` to the attempt that was DUE, but its frame never reached the
        // radio. Put it back, so the next poll offers the SAME attempt again instead of spending it.
        if (e.attempt > 0) --e.attempt;
        e.deadline_ms = now_ms + extra_ms;
        return true;
    }
    return false;
}

bool dmPendingAck(DmPendingTable& t, const uint8_t ack[4], uint32_t* msg_id_out) {
    if (ack == nullptr) return false;
    for (size_t i = 0; i < kDmPendingSlots; i++) {
        DmPending& e = t.slot[i];
        // FOUR bytes, as MyMesh::processAck compares them (MyMesh.cpp:425); bytes 4-5 are not part of it.
        if (!e.used || memcmp(e.ack, ack, 4) != 0) continue;
        if (msg_id_out != nullptr) *msg_id_out = e.msg_id;
        e = DmPending{};   // the same ACK can arrive twice: a cleared slot simply does not match again
        return true;
    }
    return false;
}

DmTimeout dmPendingTimeout(DmPendingTable& t, uint32_t now_ms) {
    DmTimeout d;
    for (size_t i = 0; i < kDmPendingSlots; i++) {
        DmPending& e = t.slot[i];
        if (!e.used) continue;
        if ((int32_t)(now_ms - e.deadline_ms) < 0) continue;

        d.msg_id = e.msg_id;
        if (e.attempt + 1 < kDmMaxAttempts) {
            ++e.attempt;
            d.action     = DmTimeoutAction::Retry;
            d.attempt    = e.attempt;
            // The last attempt is a flood even if a path is stored; and a DIRECT attempt that timed out is
            // taken as a dead route, so the next try is a flood too and the path is cleared.
            d.flood      = (e.attempt == kDmMaxAttempts - 1) || e.direct;
            d.clear_path = e.direct;
            e.direct     = !d.flood;
            // Stops a second poll from firing before the caller's rearm sets the real deadline.
            e.deadline_ms = now_ms + 1;
            return d;
        }
        e = DmPending{};
        d.action = DmTimeoutAction::GiveUp;
        return d;
    }
    return d;
}

uint32_t dmFloodTimeoutMillis(uint32_t airtime_ms) {
    return 500u + 16u * airtime_ms;   // SEND_TIMEOUT_BASE_MILLIS + FLOOD_SEND_TIMEOUT_FACTOR * airtime
}

uint32_t dmDirectTimeoutMillis(uint32_t airtime_ms, uint8_t out_path_len) {
    const uint8_t path_hash_count = (uint8_t)(out_path_len & 63);
    return 500u + (airtime_ms * 6u + 250u) * (uint32_t)(path_hash_count + 1);
}

uint32_t dmAirtimeMillis(size_t frame_bytes) {
    // Semtech's time-on-air, at kSloPlan's numbers: Tsym = 2^SF / BW = 256 / 62500 s = 4.096 ms.
    const int64_t tsym_us  = 4096;
    const int64_t sf       = 8;
    const int64_t cr       = 8;    // the plan's cr field means 4/8 here, so the symbol multiplier is 8
    const int64_t preamble = 32;   // MeshCore's preambleLengthForSF(8) (RadioLibWrappers.h:56)
    // payloadSymbNb = 8 + ceil((8*PL - 4*SF + 28 + 16*CRC - 20*IH) / (4*(SF - 2*DE))) * (CR + 4)
    int64_t num    = 8 * (int64_t)frame_bytes - 4 * sf + 28 + 16;   // CRC on, no explicit header (IH = 0)
    int64_t blocks = (num + (4 * sf) - 1) / (4 * sf);
    if (blocks < 0) blocks = 0;
    const int64_t total_us = (preamble + 4) * tsym_us + 1024 /* the 4.25 symbol preamble tail */ +
                             (8 + blocks * cr) * tsym_us;
    return (uint32_t)((total_us + 999) / 1000);
}

const uint8_t* dmAckPayload(const View& pkt) {
    if (pkt.type() != kTypeAck) return nullptr;
    // Mesh.cpp:119-121 reads the first FOUR bytes and only refuses an incomplete payload; our own EST-434
    // builder sends the 4 crc bytes plus the attempt byte and a random byte, so 6 is normal.
    if (pkt.payload == nullptr || pkt.payload_len < 4) return nullptr;
    return pkt.payload;
}

}  // namespace meshcore
