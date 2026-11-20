// SPDX-License-Identifier: MIT
// The contact table as ONE versioned NVS blob (S513/EST-437).
//
// A DM from a known node could not be opened after a restart until that node advertised again, because the
// contact table was RAM only. The format lives here, away from NVS and away from the radio: the host tests
// round-trip it, truncate it and hand it a wrong version, and the device writes the same bytes.
//
// Layout (little-endian, no struct padding):
//   header: version u8 | count u8 | record_size u16
//   record: pub[32] | name[kNameBytes] | type u8 | path_len u8 | remote_ts u32 | heard_at u32 |
//           rssi i16 | snr_x10 i16 | favourite u8 | dm_seen u32 | dm_seen_log u32 | dm_log_next u32 |
//           out_path_len u8 | out_path[kMaxPath]
//   v3 tail: fresh u8 | has_position u8 | lat_e6 i32 | lon_e6 i32
//
// S513/EST-439: the out_path slot IS the route now (it was reserved by EST-437, so the record width does not
// move). Version 2 encodes it; a version-1 blob still loads, with every contact's route OUT_PATH_UNKNOWN (0xFF),
// because "an old blob must not make a message look direct".
//
// S515/EST-461: version 3 appends the new-contact flag AND the advert's position (EST-463 will draw it), so the
// NVS format changes once instead of twice. Versions 1 and 2 still load: their records are 148 bytes and every
// row comes back with fresh=false and no position, which is exactly what an older build knew.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"
#include "mc_wire.h"   // kMaxPath: the route slot's width is MeshCore's own path limit

namespace om {
namespace mc {

// S515/EST-463: version 4. The count had to widen — a u8 cannot say 2,000 — and the record grew one byte for
// `in_radio`. The header carries a u16 count from version 4 on; versions 1-3 are read with their old u8 count and
// their old header width, so a blob already on a device still loads.
constexpr uint8_t kContactBlobVersion     = 4;   // 4 = u16 count + in_radio; 3 = fresh + position; 2 = route; 1 loads
constexpr size_t  kContactBlobHeaderBytes = 6;   // version u8 | 0 u8 | count u16 | record_size u16 (version 4+)
constexpr size_t  kContactBlobHeaderBytesLegacy = 4;   // version u8 | count u8 | record_size u16 (versions 1-3)
constexpr size_t  kContactBlobRouteBytes  = meshcore::kMaxPath;   // one out_path_len byte + the path

// 32 + 24 + 1 + 1 + 4 + 4 + 2 + 2 + 1 + 4 + 4 + 4 + 1 + 64 = 148, the width of a version-1/2 record.
constexpr size_t kContactBlobRecordBytesV2 = 148;
// …plus fresh u8 + has_position u8 + lat_e6 i32 + lon_e6 i32 = 158 for a version-3 record…
constexpr size_t kContactBlobRecordBytesV3 = kContactBlobRecordBytesV2 + 10;
// …plus in_radio u8 = 159 for a version-4 record (S515/EST-463).
constexpr size_t kContactBlobRecordBytes = kContactBlobRecordBytesV3 + 1;
constexpr size_t kContactBlobMaxBytes    = kContactBlobHeaderBytes + kMaxContacts * kContactBlobRecordBytes;

enum class ContactDecode : uint8_t {
    Ok = 0,
    Empty,           // len == 0: nothing stored, not an error
    Truncated,       // fewer bytes than the header/records claim
    BadVersion,      // a blob from a format this build does not know (1, 2 and 3 are accepted)
    BadRecordSize,   // the header's record width is not what THAT version's records are
    BadCount,        // more contacts than the table can hold
    BadSize,         // more bytes than the header/count account for
};

// A short stable name for the log line; never nullptr.
const char* contactDecodeText(ContactDecode r);

// Encode `s` into `out`. Returns the number of bytes written, or 0 when `out` is null or `cap` is too small.
// The size depends on the SAVED count (S516/EST-476 session-only rows are skipped), so a caller can ask with
// cap 0 to learn the requirement first.
size_t contactsEncode(const ContactStore& s, uint8_t* out, size_t cap);

// S515/EST-464 (EST-463 item 3): the NVS blob is now only the FALLBACK used when /int does not mount. It must
// never carry an archive-only node (Filip: nothing enters the radio automatically), and the old 32-row NVS
// budget still applies to that partition. So this encoder writes ONLY SAVED rows with in_radio, at most
// `max_rows` (32 on the device); S516/EST-476 session-only rows are skipped too. Returns 0 when out is null, the
// table is null, or cap is too small. The caller learns whether it had to drop radio rows by comparing the count
// it wrote with radioCount().
size_t contactsEncodeRadio(const ContactStore& s, uint8_t* out, size_t cap, size_t max_rows);

// S515/EST-464: one version-4 record, for the /int and SD node files, which share this exact record layout.
// `out` must have kContactBlobRecordBytes room; `contactRecordDecode` refuses any other stride.
void contactEncodeRecord(const Contact& c, uint8_t* out);
bool contactDecodeRecord(const uint8_t* in, size_t stride, Contact& out);

// Decode a blob into `out`. `out` is left UNTOUCHED unless the answer is Ok (or Empty, which clears it): a
// wrong blob must never half-replace a table that is already in RAM.
ContactDecode contactsDecode(const uint8_t* in, size_t len, ContactStore& out);

}  // namespace mc
}  // namespace om
