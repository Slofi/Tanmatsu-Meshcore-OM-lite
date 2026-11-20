// SPDX-License-Identifier: MIT
// S515/EST-464: the node files' codec, the boot merge and the one-time NVS migration (see node_store.h).
#include "node_store.h"

#include <string.h>

namespace om {
namespace mc {
namespace {

void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

uint16_t get16(const uint8_t* p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool selected(const Contact& c, bool radio_only) { return !radio_only || c.in_radio; }

}  // namespace

const char* nodeFileResultText(NodeFileResult r) {
    switch (r) {
        case NodeFileResult::Ok: return "ok";
        case NodeFileResult::Empty: return "empty";
        case NodeFileResult::Truncated: return "truncated";
        case NodeFileResult::BadMagic: return "wrong magic";
        case NodeFileResult::BadVersion: return "wrong version";
        case NodeFileResult::BadRecordSize: return "wrong record size";
        case NodeFileResult::BadCount: return "too many nodes";
        case NodeFileResult::BadSize: return "wrong size";
        case NodeFileResult::BadCrc: return "bad CRC";
    }
    return "unknown";
}

uint32_t nodeFileSignature(const uint8_t* file, size_t len) {
    if (file == nullptr || len < kNodeFileCrcBytes) return 0;
    return nodeFileCrc32(file, len - kNodeFileCrcBytes);
}

uint32_t nodeFileCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    if (data != nullptr) {
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

size_t nodeFileEncode(const ContactStore& s, bool radio_only, size_t max_rows, uint8_t* out, size_t cap) {
    if (out == nullptr || s.c == nullptr) return 0;
    if (max_rows == 0) max_rows = kMaxNodes;
    size_t count = 0;
    for (size_t i = 0; i < s.count && i < s.cap && count < max_rows; i++)
        if (selected(s.c[i], radio_only)) count++;
    const size_t need = kNodeFileHeaderBytes + count * kContactBlobRecordBytes + kNodeFileCrcBytes;
    if (cap < need) return 0;

    put32(out + 0, kNodeFileMagic);
    put16(out + 4, kNodeFileVersion);
    put16(out + 6, (uint16_t)kContactBlobRecordBytes);
    put16(out + 8, (uint16_t)count);
    put16(out + 10, 0);   // flags, reserved
    size_t o = kNodeFileHeaderBytes;
    size_t written = 0;
    for (size_t i = 0; i < s.count && i < s.cap && written < count; i++) {
        if (!selected(s.c[i], radio_only)) continue;
        contactEncodeRecord(s.c[i], out + o);
        o += kContactBlobRecordBytes;
        written++;
    }
    put32(out + o, nodeFileCrc32(out, o));
    return need;
}

NodeFileResult nodeFileCheck(const uint8_t* in, size_t len) {
    if (len == 0) return NodeFileResult::Empty;
    if (in == nullptr || len < kNodeFileHeaderBytes) return NodeFileResult::Truncated;
    if (get32(in + 0) != kNodeFileMagic) return NodeFileResult::BadMagic;
    if (get16(in + 4) != kNodeFileVersion) return NodeFileResult::BadVersion;
    if (get16(in + 6) != kContactBlobRecordBytes) return NodeFileResult::BadRecordSize;
    const size_t count = get16(in + 8);
    if (count > kMaxNodes) return NodeFileResult::BadCount;
    const size_t need = kNodeFileHeaderBytes + count * kContactBlobRecordBytes + kNodeFileCrcBytes;
    if (len < need) return NodeFileResult::Truncated;
    if (len > need) return NodeFileResult::BadSize;
    const size_t body = kNodeFileHeaderBytes + count * kContactBlobRecordBytes;
    if (get32(in + body) != nodeFileCrc32(in, body)) return NodeFileResult::BadCrc;
    return NodeFileResult::Ok;
}

NodeFileResult nodeFileDecode(const uint8_t* in, size_t len, ContactStore& out) {
    const NodeFileResult checked = nodeFileCheck(in, len);
    if (checked == NodeFileResult::Empty) {
        out.count = 0;
        return NodeFileResult::Empty;
    }
    if (checked != NodeFileResult::Ok) return checked;
    const size_t count = get16(in + 8);
    if (out.c == nullptr || count > out.cap) return NodeFileResult::BadCount;

    // Every check is above this line: a wrong file leaves the table alone.
    out.count = count;
    for (size_t i = 0; i < count; i++) {
        contactDecodeRecord(in + kNodeFileHeaderBytes + i * kContactBlobRecordBytes, kContactBlobRecordBytes,
                            out.c[i]);
    }
    return NodeFileResult::Ok;
}

size_t markAllInRadio(ContactStore& s, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < s.count && i < s.cap && n < cap; i++) {
        s.c[i].in_radio = true;
        n++;
    }
    return n;
}

MergeResult mergeArchive(ContactStore& live, const ContactStore& archive) {
    MergeResult r;
    if (live.c == nullptr) return r;
    for (size_t i = 0; i < archive.count && i < archive.cap; i++) {
        r.archive_rows++;
        const Contact& a  = archive.c[i];
        const int      at = contactsFind(live, a.pub);
        if (at < 0) {
            if (live.count >= live.cap) {
                r.dropped++;   // the table is full: the radio rows and the earlier archive rows stay
                continue;
            }
            const int idx = (int)live.count++;
            live.c[idx]   = a;
            // The radio file (already in `live`) is the only authority for the radio mark: a row the archive
            // carries but radio.bin does not is archive-only, even if the archive's own copied flag says otherwise.
            live.c[idx].in_radio = false;
            r.added++;
            continue;
        }
        Contact& d = live.c[at];
        const bool d_radio = d.in_radio;
        d.fresh = a.fresh;   // the archive owns the new-node dot
        if (!d_radio) {
            // Archive-only in both: the archive is the only source for the route and the local DM cursor.
            d.out_path_len = a.out_path_len;
            memcpy(d.out_path, a.out_path, sizeof(d.out_path));
            d.favourite   = a.favourite;
            d.dm_seen     = a.dm_seen;
            d.dm_seen_log = a.dm_seen_log;
            d.dm_log_next = a.dm_log_next;
        }
        if (a.heard_at > d.heard_at) {
            // The newer sighting wins the last-heard fields (the radio row keeps only its in_radio mark and route).
            memcpy(d.name, a.name, sizeof(d.name));
            d.type         = a.type;
            d.path_len     = a.path_len;
            d.remote_ts    = a.remote_ts;
            d.heard_at     = a.heard_at;
            d.rssi         = a.rssi;
            d.snr_x10      = a.snr_x10;
            d.has_position = a.has_position;
            d.lat_e6       = a.lat_e6;
            d.lon_e6       = a.lon_e6;
        }
        r.updated++;
    }
    return r;
}

size_t migrateNvsToRadio(ContactStore& s, uint8_t nvs_blob_version) {
    if (nvs_blob_version >= kContactBlobVersion) {
        size_t n = 0;
        for (size_t i = 0; i < s.count && i < s.cap; i++)
            if (s.c[i].in_radio) n++;
        return n;   // v4 already carries the flags
    }
    // v1-v3 had no mark: every stored row was a radio contact (the old table was the radio). It never held more
    // than the old 32, and markAllInRadio enforces kMaxRadio even on a hand-edited blob.
    return markAllInRadio(s, kMaxRadio);
}

}  // namespace mc
}  // namespace om
