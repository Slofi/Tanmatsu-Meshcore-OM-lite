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
//
// S513/EST-439: the out_path slot IS the route now (it was reserved by EST-437, so the record width does not
// move). Version 2 encodes it; a version-1 blob still loads, with every contact's route OUT_PATH_UNKNOWN (0xFF),
// because "an old blob must not make a message look direct".
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"
#include "mc_wire.h"   // kMaxPath: the route slot's width is MeshCore's own path limit

namespace om {
namespace mc {

constexpr uint8_t kContactBlobVersion     = 2;   // 2 = out_path filled; 1 still loads (route unknown)
constexpr size_t  kContactBlobHeaderBytes = 4;
constexpr size_t  kContactBlobRouteBytes  = meshcore::kMaxPath;   // one out_path_len byte + the path

// 32 + 24 + 1 + 1 + 4 + 4 + 2 + 2 + 1 + 4 + 4 + 4 + 1 + 64.
constexpr size_t kContactBlobRecordBytes = 148;
constexpr size_t kContactBlobMaxBytes    = kContactBlobHeaderBytes + kMaxContacts * kContactBlobRecordBytes;

enum class ContactDecode : uint8_t {
    Ok = 0,
    Empty,           // len == 0: nothing stored, not an error
    Truncated,       // fewer bytes than the header/records claim
    BadVersion,      // a blob from a format this build does not know (1 and 2 are accepted)
    BadRecordSize,   // the header's record width is not this build's
    BadCount,        // more contacts than the table can hold
    BadSize,         // more bytes than the header/count account for
};

// A short stable name for the log line; never nullptr.
const char* contactDecodeText(ContactDecode r);

// Encode `s` into `out`. Returns the number of bytes written, or 0 when `out` is null or `cap` is too small.
// The size depends on s.count, so a caller can ask with cap 0 to learn the requirement first.
size_t contactsEncode(const ContactStore& s, uint8_t* out, size_t cap);

// Decode a blob into `out`. `out` is left UNTOUCHED unless the answer is Ok (or Empty, which clears it): a
// wrong blob must never half-replace a table that is already in RAM.
ContactDecode contactsDecode(const uint8_t* in, size_t len, ContactStore& out);

}  // namespace mc
}  // namespace om
