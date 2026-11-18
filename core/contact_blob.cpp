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
}

void decodeRecord(const uint8_t* p, uint8_t version, Contact& c) {
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
    if (out == nullptr || s.count > kMaxContacts) return 0;
    const size_t need = kContactBlobHeaderBytes + s.count * kContactBlobRecordBytes;
    if (cap < need) return 0;

    out[0] = kContactBlobVersion;
    out[1] = (uint8_t)s.count;
    put16(out + 2, (uint16_t)kContactBlobRecordBytes);
    size_t o = kContactBlobHeaderBytes;
    for (size_t i = 0; i < s.count; i++) {
        encodeRecord(s.c[i], out + o);
        o += kContactBlobRecordBytes;
    }
    return need;
}

ContactDecode contactsDecode(const uint8_t* in, size_t len, ContactStore& out) {
    if (len == 0) {
        out = ContactStore{};
        return ContactDecode::Empty;
    }
    if (in == nullptr || len < kContactBlobHeaderBytes) return ContactDecode::Truncated;
    const uint8_t version = in[0];
    // Version 1 is the EST-437 blob whose route slot was reserved; it loads with every route unknown.
    if (version != 1 && version != kContactBlobVersion) return ContactDecode::BadVersion;
    const size_t count = in[1];
    if (get16(in + 2) != kContactBlobRecordBytes) return ContactDecode::BadRecordSize;
    if (count > kMaxContacts) return ContactDecode::BadCount;

    const size_t need = kContactBlobHeaderBytes + count * kContactBlobRecordBytes;
    if (len < need) return ContactDecode::Truncated;
    if (len > need) return ContactDecode::BadSize;

    // Every check is above this line, so nothing can fail after the first write: a wrong blob leaves `out` alone
    // without needing a second 2.7 KB table on the boot stack.
    out       = ContactStore{};
    out.count = count;
    for (size_t i = 0; i < count; i++) {
        decodeRecord(in + kContactBlobHeaderBytes + i * kContactBlobRecordBytes, version, out.c[i]);
    }
    return ContactDecode::Ok;
}

}  // namespace mc
}  // namespace om
