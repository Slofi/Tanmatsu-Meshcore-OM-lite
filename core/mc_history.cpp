// SPDX-License-Identifier: MIT
#include "mc_history.h"

#include <stdio.h>
#include <string.h>

// The L2's history format: the SAME header the other device writes, so one log can be read by either.
#include "../../firmware/src/history/history_log.h"

namespace om {
namespace mc {
namespace {

// S515/EST-462: what a restored record can honestly say about the path.
//   · hops == 0 is "direct" in BOTH encoders (v57 wrote 0 for a direct frame, and this build writes the real 0 too);
//   · hops > 0 with pathCount == 0 is a v57-style FAKE record (the old encoder wrote hops 1, pathCount 0, no
//     bytes): the hop count is UNKNOWN and must not be shown;
//   · this build always writes pathCount == hops with pathHashSize bytes each, so only that shape is a real path.
// What the record never carries stays absent: bytes, route type and copy counts.
struct RestoredPath {
    bool     hops_known = false;
    bool     direct     = false;
    uint8_t  hops       = 0;
    uint8_t  hash_size  = 0;
    uint8_t  path_bytes = 0;
    uint8_t  path[meshcore::kMaxPath] = {0};
};

RestoredPath pathFromRecord(const om::history::Msg& h) {
    RestoredPath p;
    if (h.hops < 0) return p;                       // no count at all
    if (h.hops == 0) {                              // direct, true in both encoders
        p.hops_known = true;
        p.direct     = true;
        return p;
    }
    const int hs = h.pathHashSize;
    const int hc = h.pathCount;
    if (hc == 0 || hc != (int)h.hops || hs < 1 || hs > 3) return p;   // the v57 fake shape: unknown
    const int bytes = hs * hc;
    if (bytes > (int)meshcore::kMaxPath || bytes != (int)h.pathLen) return p;
    p.hops_known = true;
    p.hops       = (uint8_t)hc;
    p.hash_size  = (uint8_t)hs;
    p.path_bytes = (uint8_t)bytes;
    memcpy(p.path, h.path, (size_t)bytes);
    return p;
}

}  // namespace

int historyEncode(const Message& m, uint32_t log_id, uint8_t* out, int cap) {
    if (out == nullptr || cap <= 0) return 0;
    om::history::Msg h;
    // Two different ids, on purpose (S503): the FILE's id is what the Meta record's seen point refers to and it
    // must be consecutive and stable across reboots; the ring's id is per boot and only for the USB dump.
    h.logId  = log_id != 0 ? log_id : m.seq;
    h.seq    = m.seq;
    h.uptime = 0;
    h.wall   = m.ts ? m.ts : m.local_ts;  // the wire timestamp, or when we saw it if the frame had none
    h.setWallReal(h.wall != 0);
    h.setMine(m.outgoing);
    h.setDm(false);                       // channels only in this build; DMs are a later slice
    // S515/EST-462: the REAL path, in the format's own fields (nothing new is added to the L2's layout). A message
    // whose hop count is not honestly known (our own flood) keeps the old flood shape — hops 1 with pathCount 0 —
    // which this build's loader reads back as UNKNOWN rather than inventing a count.
    h.hops    = (int8_t)(m.hops_known ? (int)m.hops : (m.direct ? 0 : 1));
    h.snr4    = (int8_t)(m.snr_x10 / 25);   // the L2 stores SNR in 0.25 dB steps
    h.repeats = 0;                        // copies/echoes are live facts, never restored (item 2)
    om::history::msgSetPath(h, m.path, m.hash_size, m.hops_known ? m.hops : 0);
    om::history::msgSetText(h, m.sender, m.text);

    uint8_t payload[om::history::kMaxPayload];
    const int plen = om::history::encodeMsgPayload(h, payload, (int)sizeof(payload));
    if (plen <= 0) return 0;
    const int total = om::history::encodeRecord(om::history::Type::Msg, payload, plen, out, cap);
    return total > 0 ? total : 0;
}

// S513/EST-434: a DM record. Same header and payload as a channel message; `dm` is the format's own flag and the
// name field carries the PEER, so the L2's reader shows the thread correctly.
int historyEncodeDm(const DmMessage& m, uint32_t log_id, uint8_t* out, int cap) {
    if (out == nullptr || cap <= 0) return 0;
    om::history::Msg h;
    h.logId  = log_id != 0 ? log_id : m.seq;
    h.seq    = m.seq;
    h.uptime = 0;
    h.wall   = m.ts ? m.ts : m.local_ts;
    h.setWallReal(h.wall != 0);
    h.setMine(m.outgoing);
    h.setDm(true);
    h.hops    = (int8_t)(m.hops_known ? (int)m.hops : (m.direct ? 0 : 1));
    h.snr4    = (int8_t)(m.snr_x10 / 25);
    h.repeats = 0;                        // live fact, not restored (item 2)
    // The delivery byte is the L2's own DmState value (dm_delivery.h -> history_log.h Msg::delivery). An outgoing
    // message is written as Sending; the ACK later appends an Update that raises it to Delivered.
    h.delivery = (uint8_t)m.delivery;
    om::history::msgSetPath(h, m.path, m.hash_size, m.hops_known ? m.hops : 0);
    om::history::msgSetText(h, m.peer_name, m.text);

    uint8_t payload[om::history::kMaxPayload];
    const int plen = om::history::encodeMsgPayload(h, payload, (int)sizeof(payload));
    if (plen <= 0) return 0;
    const int total = om::history::encodeRecord(om::history::Type::Msg, payload, plen, out, cap);
    return total > 0 ? total : 0;
}

int historyEncodeDmState(uint32_t log_id, om::ui::DmState state, uint32_t ack_wall, uint8_t* out, int cap) {
    if (out == nullptr || cap <= 0 || log_id == 0) return 0;
    om::history::Update u;
    u.logId    = log_id;
    u.kind     = om::history::kUpdateState;
    u.delivery = (uint8_t)state;
    u.ackReal  = (ack_wall != 0);
    u.ackWall  = ack_wall;
    u.repeats  = 0;
    uint8_t   payload[om::history::kMaxPayload];
    const int plen = om::history::encodeUpdatePayload(u, payload, (int)sizeof(payload));
    if (plen <= 0) return 0;
    const int total = om::history::encodeRecord(om::history::Type::Update, payload, plen, out, cap);
    return total > 0 ? total : 0;
}

size_t historyLoadDm(const uint8_t* data, size_t len, const uint8_t peer_pub[32], const char* peer_name,
                     DmMessage* out, size_t max_out, uint32_t* out_max_log_id) {
    if (out == nullptr || max_out == 0 || peer_pub == nullptr) return 0;
    if (out_max_log_id != nullptr) *out_max_log_id = 0;
    if (data == nullptr) return 0;
    size_t   kept       = 0;
    size_t   pos        = 0;
    uint32_t max_log_id = 0;
    while (pos + (size_t)om::history::kHeaderBytes <= len) {
        om::history::RecordRef ref;
        const int r = om::history::decodeRecordAt(data + pos, (int)(len - pos), (uint32_t)pos, &ref);
        if (r < 0) break;   // the torn-tail rule, the same one the channel log uses
        pos += (size_t)r;
        if (ref.type == om::history::Type::Update) {
            // A delivery update names the message by its log id. The log is read oldest first, so the message it
            // refers to is already in `out` whenever it is still in the newest window; an update for an evicted
            // record simply has nothing to change.
            om::history::Update u;
            if (!om::history::decodeUpdatePayload(ref.payload, (int)ref.len - om::history::kHeaderBytes, &u)) continue;
            for (size_t i = 0; i < kept; i++) {
                if (out[i].log_id == u.logId) out[i].delivery = (om::ui::DmState)u.delivery;
            }
            continue;
        }
        if (ref.type != om::history::Type::Msg) continue;

        om::history::Msg h;
        if (!om::history::decodeMsgPayload(ref.payload, (int)ref.len - om::history::kHeaderBytes, &h)) continue;
        if (h.logId > max_log_id) max_log_id = h.logId;

        DmMessage m;
        m.seq = (uint32_t)(h.seq ? h.seq : h.logId);
        m.log_id = h.logId;
        memcpy(m.peer_pub, peer_pub, sizeof(m.peer_pub));
        // Incoming records carry the PEER's name; an outgoing record carries ours, so the caller's name wins.
        const char* name = (!h.mine() && h.name[0] != '\0') ? h.name : (peer_name != nullptr ? peer_name : "");
        snprintf(m.peer_name, sizeof(m.peer_name), "%.*s", (int)(sizeof(m.peer_name) - 1), name);
        m.outgoing = h.mine();
        m.ts       = h.wall;
        m.local_ts = h.wall;
        m.snr_x10  = (int16_t)((int)h.snr4 * 25);
        {
            const RestoredPath p = pathFromRecord(h);
            m.direct     = p.direct;
            m.hops_known = p.hops_known;
            m.hops       = p.hops;
            m.hash_size  = p.hash_size;
            m.path_bytes = p.path_bytes;
            if (p.path_bytes > 0) memcpy(m.path, p.path, p.path_bytes);
            // Bytes, route and copy counts are not in the record; leave them absent (item 2).
            m.frame_len = 0;
            m.route     = p.direct ? RouteType::Direct : RouteType::Unknown;
            m.copies    = 0;
            m.restored  = true;   // S515/EST-469: the Info sheet must not print these zeros as measurements
        }
        m.delivery = h.delivery <= (uint8_t)om::ui::DmState::Failed
                         ? (om::ui::DmState)h.delivery
                         : om::ui::DmState::NotConfirmed;   // a byte from a future format is never shown as "ok"
        snprintf(m.text, sizeof(m.text), "%.*s", (int)(sizeof(m.text) - 1), h.text);

        if (kept < max_out) {
            out[kept++] = m;
        } else {
            memmove(&out[0], &out[1], sizeof(DmMessage) * (max_out - 1));
            out[max_out - 1] = m;
        }
    }
    if (out_max_log_id != nullptr) *out_max_log_id = max_log_id;
    return kept;
}

// ---- S513/EST-441: one restored thread, with no contact invented ---------------------------------------------
size_t dmRestoreThread(DmRing& ring, ContactStore& contacts, const uint8_t pub[32], const DmMessage* recs,
                       size_t n, const SeenPoint& sp, RestoredDm* out) {
    RestoredDm r;
    r.seen_found = sp.found;
    if (pub == nullptr || (recs == nullptr && n > 0)) {
        if (out != nullptr) *out = r;
        return 0;
    }

    // The name comes from the thread's own records: an INCOMING record carries the peer's name, an outgoing one
    // carries ours. With neither, the 8-hex prefix (the caller's label) is the honest answer.
    for (size_t i = 0; i < n; i++) {
        if (!recs[i].outgoing && recs[i].peer_name[0] != '\0') {
            snprintf(r.label, sizeof(r.label), "%s", recs[i].peer_name);
            break;
        }
    }
    if (r.label[0] == '\0') {
        snprintf(r.label, sizeof(r.label), "%02x%02x%02x%02x", pub[0], pub[1], pub[2], pub[3]);
    }

    uint8_t key[32];   // the public key's own width (mc_identity.h kPubKeyBytes; not included here)
    memcpy(key, pub, sizeof(key));
    int ci = contactsFind(contacts, key);
    if (contactKeyIncomplete(key)) {
        // Four bytes from a file name identify nothing. A row may already exist because a verified advert put a
        // FULL key there; it can only be this thread's peer when there is exactly one such prefix.
        ci = contactsFindUniquePrefix(contacts, key);
        if (ci >= 0) {
            memcpy(key, contacts.c[ci].pub, sizeof(key));
            r.rebound = true;
        } else {
            ci = -1;
        }
    }

    for (size_t i = 0; i < n; i++) {
        DmMessage m = recs[i];
        memcpy(m.peer_pub, key, sizeof(m.peer_pub));   // the prefix, or the full key when one was found
        if (m.peer_name[0] == '\0') snprintf(m.peer_name, sizeof(m.peer_name), "%s", r.label);
        const uint32_t seq = dmPush(ring, m);
        if (m.log_id > r.max_log_id) r.max_log_id = m.log_id;
        if (sp.found && m.log_id != 0 && m.log_id <= sp.seen_log_id) r.seen_seq = seq;
        r.messages++;
    }

    if (ci >= 0) {
        // The contact row is the thread's seen point (item 3), so a rebound thread keeps its unread count.
        contacts.c[ci].dm_seen     = sp.found ? r.seen_seq : 0;   // no Meta record: all unread, honestly
        contacts.c[ci].dm_log_next = r.max_log_id + 1;            // continue the file's own numbering
        if (r.label[0] != '\0' && contacts.c[ci].name[0] == '\0') {
            snprintf(contacts.c[ci].name, sizeof(contacts.c[ci].name), "%s", r.label);
        }
    }
    if (out != nullptr) *out = r;
    return r.messages;
}

size_t historyLoad(const uint8_t* data, size_t len, uint8_t channel, Message* out, size_t max_out,
                   uint32_t* out_max_log_id) {
    if (out == nullptr || max_out == 0) return 0;
    if (out_max_log_id != nullptr) *out_max_log_id = 0;
    if (data == nullptr) return 0;
    size_t   kept       = 0;
    size_t   pos        = 0;
    uint32_t max_log_id = 0;
    while (pos + (size_t)om::history::kHeaderBytes <= len) {
        om::history::RecordRef ref;
        const int r = om::history::decodeRecordAt(data + pos, (int)(len - pos), (uint32_t)pos, &ref);
        if (r < 0) break;   // the torn-tail rule: everything before this record is still good
        pos += (size_t)r;
        if (ref.type != om::history::Type::Msg) continue;

        om::history::Msg h;
        if (!om::history::decodeMsgPayload(ref.payload, (int)ref.len - om::history::kHeaderBytes, &h)) continue;
        if (h.logId > max_log_id) max_log_id = h.logId;

        Message m;
        m.seq      = (uint32_t)(h.seq ? h.seq : h.logId);
        m.log_id   = h.logId;   // the FILE's id: what the seen point refers to
        m.channel  = channel;
        m.outgoing = h.mine();
        m.ts       = h.wall;
        m.local_ts = h.wall;
        m.snr_x10  = (int16_t)((int)h.snr4 * 25);
        {
            const RestoredPath p = pathFromRecord(h);
            m.direct     = p.direct;
            m.hops_known = p.hops_known;
            m.hops       = p.hops;
            m.hash_size  = p.hash_size;
            m.path_bytes = p.path_bytes;
            if (p.path_bytes > 0) memcpy(m.path, p.path, p.path_bytes);
            m.frame_len = 0;   // not in the record: bytes are shown for THIS boot only
            m.route     = p.direct ? RouteType::Direct : RouteType::Unknown;
            m.copies    = 0;
            m.restored  = true;   // S515/EST-469: the Info sheet must not print these zeros as measurements
        }
        // The L2's name field is 32 bytes and ours is 24, so a long name is CUT here, not refused.
        snprintf(m.sender, sizeof(m.sender), "%.*s", (int)(sizeof(m.sender) - 1), h.name);
        snprintf(m.text, sizeof(m.text), "%.*s", (int)(sizeof(m.text) - 1), h.text);

        if (kept < max_out) {
            out[kept++] = m;
        } else {
            // Keep the NEWEST window: drop the oldest and append. A log can be far longer than the ring.
            memmove(&out[0], &out[1], sizeof(Message) * (max_out - 1));
            out[max_out - 1] = m;
        }
    }
    if (out_max_log_id != nullptr) *out_max_log_id = max_log_id;
    return kept;
}

int historyEncodeMeta(uint32_t seen_log_id, uint16_t unread, uint8_t* out, int cap) {
    if (out == nullptr || cap <= 0) return 0;
    om::history::Meta meta;
    meta.seenLogId = seen_log_id;
    meta.unread    = unread;
    uint8_t   payload[8];
    const int plen = om::history::encodeMetaPayload(meta, payload, (int)sizeof(payload));
    if (plen <= 0) return 0;
    const int total = om::history::encodeRecord(om::history::Type::Meta, payload, plen, out, cap);
    return total > 0 ? total : 0;
}

SeenPoint historyReadSeen(const uint8_t* data, size_t len) {
    SeenPoint sp;
    if (data == nullptr || len == 0) return sp;
    size_t pos = 0;
    while (pos + (size_t)om::history::kHeaderBytes <= len) {
        om::history::RecordRef ref;
        const int r = om::history::decodeRecordAt(data + pos, (int)(len - pos), (uint32_t)pos, &ref);
        if (r < 0) break;   // the torn-tail rule applies here too
        pos += (size_t)r;
        if (ref.type != om::history::Type::Meta) continue;
        om::history::Meta meta;
        if (!om::history::decodeMetaPayload(ref.payload, (int)ref.len - om::history::kHeaderBytes, &meta)) continue;
        // Last one wins: the L2's loader applies records in file order, and a later Meta supersedes an older one.
        sp.seen_log_id = meta.seenLogId;
        sp.unread      = meta.unread;
        sp.found       = true;
    }
    return sp;
}

void historyFileName(uint8_t hash, const char* name, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    char id[48];
    om::history::sanitizeId(name != nullptr ? name : "channel", id, (int)sizeof(id));
    snprintf(out, cap, "ch-%02x-%s.log", (unsigned)hash, id);
}

}  // namespace mc
}  // namespace om
