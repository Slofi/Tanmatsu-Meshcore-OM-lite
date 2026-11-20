// SPDX-License-Identifier: MIT
#include "contact_blob.h"

#include <string.h>

namespace om {
namespace mc {
namespace {

void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

uint16_t get16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The stored route must be an encoding MeshCore itself would send: hash_size 4 is reserved and the byte count
// must fit MAX_PATH_SIZE (Packet::isValidPathLen, Packet.cpp:13-18). A blob is not trusted to hold a valid one.
bool validStoredPath(uint8_t path_len) {
    if (path_len == kOutPathUnknown) return true;   // the "no route" value is allowed
    const uint8_t hash_count = (uint8_t)(path_len & 63);
    const uint8_t hash_size  = (uint8_t)((path_len >> 6) + 1);
    if (hash_size == 4) return false;
    return (size_t)hash_count * hash_size <= kContactBlobRouteBytes;
}

// A record is written field by field, so its bytes do not depend on the compiler's struct layout, and the
// route slot is written from Contact::out_path (version 2).
void encodeRecord(const Contact& c, uint8_t* p) {
    size_t o = 0;
    memcpy(p + o, c.pub, sizeof(c.pub));
    o += sizeof(c.pub);
    memcpy(p + o, c.name, sizeof(c.name));
    o += sizeof(c.name);
    p[o++] = c.type;
    p[o++] = c.path_len;
    put32(p + o, c.remote_ts);
    o += 4;
    put32(p + o, c.heard_at);
    o += 4;
    put16(p + o, (uint16_t)c.rssi);
    o += 2;
    put16(p + o, (uint16_t)c.snr_x10);
    o += 2;
    p[o++] = c.favourite ? 1 : 0;
    put32(p + o, c.dm_seen);
    o += 4;
    put32(p + o, c.dm_seen_log);
    o += 4;
    put32(p + o, c.dm_log_next);
    o += 4;
    // The route: out_path_len, then the byte count its own encoding names (EST-439). A Contact that carries an
    // invalid encoding is stored as unknown rather than as bytes no sender should ever copy onto the air.
    const uint8_t len = validStoredPath(c.out_path_len) ? c.out_path_len : kOutPathUnknown;
    p[o++] = len;
    size_t path_bytes = 0;
    if (len != kOutPathUnknown) path_bytes = (size_t)(len & 63) * (size_t)((len >> 6) + 1);
    if (path_bytes > kContactBlobRouteBytes) path_bytes = 0;
    if (path_bytes > 0) memcpy(p + o, c.out_path, path_bytes);
    memset(p + o + path_bytes, 0, kContactBlobRouteBytes - path_bytes);
    o += kContactBlobRouteBytes;
    // S515/EST-461 (version 3): the new-contact flag and the advert's position. lat/lon are written only when
    // the advert carried them, so a row without a position stores zeros the decoder will not trust either.
    p[o++] = c.fresh ? 1 : 0;
    p[o++] = c.has_position ? 1 : 0;
    put32(p + o, (uint32_t)c.lat_e6);
    o += 4;
    put32(p + o, (uint32_t)c.lon_e6);
    o += 4;
    p[o++] = c.in_radio ? 1 : 0;   // S515/EST-463 (version 4)
}

// `stride` is the record width the header claims: 148 for versions 1-2, 158 for version 3. The tail is read only
// when the version actually has one.
void decodeRecord(const uint8_t* p, uint8_t version, size_t stride, Contact& c) {
    c      = Contact{};
    size_t o = 0;
    memcpy(c.pub, p + o, sizeof(c.pub));
    o += sizeof(c.pub);
    memcpy(c.name, p + o, sizeof(c.name));
    c.name[sizeof(c.name) - 1] = '\0';   // a blob from flash is not trusted to be NUL-terminated
    o += sizeof(c.name);
    c.type      = p[o++];
    c.path_len  = p[o++];
    c.remote_ts = get32(p + o);
    o += 4;
    c.heard_at = get32(p + o);
    o += 4;
    c.rssi = (int16_t)get16(p + o);
    o += 2;
    c.snr_x10 = (int16_t)get16(p + o);
    o += 2;
    c.favourite = p[o++] != 0;
    c.dm_seen = get32(p + o);
    o += 4;
    c.dm_seen_log = get32(p + o);
    o += 4;
    c.dm_log_next = get32(p + o);
    o += 4;
    // The route (EST-439). Version 1 wrote this slot as zeroes for a reason that no longer holds; it is read as
    // OUT_PATH_UNKNOWN, so an upgraded device floods until it learns a route rather than trusting a zero-length
    // direct path it never stored.
    if (version >= 2) {
        const uint8_t len = p[o];
        if (validStoredPath(len)) {
            c.out_path_len = len;
            if (len != kOutPathUnknown) {
                const size_t path_bytes = (size_t)(len & 63) * (size_t)((len >> 6) + 1);
                if (path_bytes > 0) memcpy(c.out_path, &p[o + 1], path_bytes);
            }
        }
    }
    // Version 3's tail: the new-contact flag and the advert's position, at the record's own width (stride - 10).
    // A version-1/2 record has no tail, so both stay false (and `Contact{}` already zeroed them).
    if (version >= 3 && stride >= kContactBlobRecordBytesV3) {
        size_t t = (version >= kContactBlobVersion) ? stride - 11 : stride - 10;
        c.fresh        = p[t++] != 0;
        c.has_position = p[t++] != 0;
        c.lat_e6       = (int32_t)get32(p + t);
        t += 4;
        c.lon_e6 = (int32_t)get32(p + t);
        if (!c.has_position) {   // a blob is not trusted to carry a position it does not claim
            c.lat_e6 = 0;
            c.lon_e6 = 0;
        }
        if (version >= kContactBlobVersion) c.in_radio = p[stride - 1] != 0;
    }
}

}  // namespace

const char* contactDecodeText(ContactDecode r) {
    switch (r) {
        case ContactDecode::Ok: return "ok";
        case ContactDecode::Empty: return "empty";
        case ContactDecode::Truncated: return "truncated";
        case ContactDecode::BadVersion: return "wrong version";
        case ContactDecode::BadRecordSize: return "wrong record size";
        case ContactDecode::BadCount: return "too many contacts";
        case ContactDecode::BadSize: return "wrong size";
    }
    return "unknown";
}

size_t contactsEncode(const ContactStore& s, uint8_t* out, size_t cap) {
    if (out == nullptr || s.count > kMaxContacts || s.c == nullptr) return 0;
    // S516/EST-476: a session-only row is RAM state, never a file record. It is skipped here as well as in the
    // radio/archive encoders, so no blob can ever bring one back after a restart.
    size_t count = 0;
    for (size_t i = 0; i < s.count && i < s.cap; i++)
        if (!s.c[i].session_only) count++;
    const size_t need = kContactBlobHeaderBytes + count * kContactBlobRecordBytes;
    if (cap < need) return 0;

    out[0] = kContactBlobVersion;
    out[1] = 0;                                   // reserved (was the u8 count in versions 1-3)
    put16(out + 2, (uint16_t)count);
    put16(out + 4, (uint16_t)kContactBlobRecordBytes);
    size_t o = kContactBlobHeaderBytes;
    size_t written = 0;
    for (size_t i = 0; i < s.count && i < s.cap && written < count; i++) {
        if (s.c[i].session_only) continue;
        encodeRecord(s.c[i], out + o);
        o += kContactBlobRecordBytes;
        written++;
    }
    return need;
}

// S515/EST-464: only the radio rows, capped. This is the NVS fallback encoder; /int and the SD card use
// node_store.cpp, which wraps the same record bytes with a magic/version/CRC header.
size_t contactsEncodeRadio(const ContactStore& s, uint8_t* out, size_t cap, size_t max_rows) {
    if (out == nullptr || s.c == nullptr) return 0;
    size_t count = 0;
    for (size_t i = 0; i < s.count && i < s.cap; i++) {
        // S516/EST-476: session-only rows are never written, not even the NVS fallback.
        if (s.c[i].in_radio && !s.c[i].session_only && count < max_rows) count++;
    }
    const size_t need = kContactBlobHeaderBytes + count * kContactBlobRecordBytes;
    if (cap < need) return 0;

    out[0] = kContactBlobVersion;
    out[1] = 0;
    put16(out + 2, (uint16_t)count);
    put16(out + 4, (uint16_t)kContactBlobRecordBytes);
    size_t o = kContactBlobHeaderBytes;
    size_t written = 0;
    for (size_t i = 0; i < s.count && i < s.cap && written < count; i++) {
        if (!s.c[i].in_radio || s.c[i].session_only) continue;
        encodeRecord(s.c[i], out + o);
        o += kContactBlobRecordBytes;
        written++;
    }
    return need;
}

void contactEncodeRecord(const Contact& c, uint8_t* out) { encodeRecord(c, out); }

bool contactDecodeRecord(const uint8_t* in, size_t stride, Contact& out) {
    if (in == nullptr || stride != kContactBlobRecordBytes) return false;
    decodeRecord(in, kContactBlobVersion, stride, out);
    return true;
}

ContactDecode contactsDecode(const uint8_t* in, size_t len, ContactStore& out) {
    if (len == 0) {
        out.count = 0;   // keep the table's own PSRAM allocation: constructing a new one would take another 312 KB
        return ContactDecode::Empty;
    }
    if (in == nullptr || len < kContactBlobHeaderBytesLegacy) return ContactDecode::Truncated;
    const uint8_t version = in[0];
    // Version 1 is the EST-437 blob whose route slot was reserved (every route unknown); version 2 filled it;
    // version 3 appends fresh + position; version 4 (S515/EST-463) widens the count to u16 and adds in_radio.
    // A v1/v2 record is 148 bytes, v3 158, v4 159, and the header is 4 bytes before v4 and 6 from v4.
    if (version != 1 && version != 2 && version != 3 && version != 4) return ContactDecode::BadVersion;
    const size_t hdr = (version >= kContactBlobVersion) ? kContactBlobHeaderBytes : kContactBlobHeaderBytesLegacy;
    if (len < hdr) return ContactDecode::Truncated;
    const size_t count  = (version >= kContactBlobVersion) ? (size_t)get16(in + 2) : (size_t)in[1];
    const size_t stride = (version >= kContactBlobVersion) ? (size_t)get16(in + 4) : (size_t)get16(in + 2);
    const size_t expected = (version >= kContactBlobVersion)  ? kContactBlobRecordBytes
                            : (version == 3)                  ? kContactBlobRecordBytesV3
                                                              : kContactBlobRecordBytesV2;
    if (stride != expected) return ContactDecode::BadRecordSize;
    if (count > kMaxContacts) return ContactDecode::BadCount;

    const size_t need = hdr + count * stride;
    if (len < need) return ContactDecode::Truncated;
    if (len > need) return ContactDecode::BadSize;

    // Every check is above this line, so nothing can fail after the first write: a wrong blob leaves `out` alone
    // without needing a second 2.7 KB table on the boot stack.
    if (out.c == nullptr || count > out.cap) return ContactDecode::BadCount;
    out.count = count;
    for (size_t i = 0; i < count; i++) {
        decodeRecord(in + hdr + i * stride, version, stride, out.c[i]);
    }
    return ContactDecode::Ok;
}

}  // namespace mc
}  // namespace om
