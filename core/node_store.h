// SPDX-License-Identifier: MIT
// S515/EST-464 (EST-463 items 3 and 5): the two node FILES and the boot merge, pure and host-tested.
//
// There is one table (mc_store.h) and one record layout (contact_blob.h, version 4). This file wraps that record
// in the file format both stores use:
//
//   magic u32 | version u16 | record_size u16 | count u16 | flags u16 | records... | CRC-32 u32
//
// The CRC covers every byte before it (header + records). The radio file (/int/om-lite/radio.bin) holds only the
// rows with in_radio, at most kMaxRadio; the archive (/sdcard/om-lite/nodes.bin) holds every row. A wrong magic,
// version, record width, count, length or CRC is refused WHOLE: a damaged file must never half-fill the table.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "contact_blob.h"
#include "mc_store.h"

namespace om {
namespace mc {

// 'O','M','N','1' in little-endian order, so a hex dump reads it.
constexpr uint32_t kNodeFileMagic       = 0x314E4D4Fu;
constexpr uint16_t kNodeFileVersion     = 1;
constexpr size_t   kNodeFileHeaderBytes = 12;   // magic u32 | version u16 | record u16 | count u16 | flags u16
constexpr size_t   kNodeFileCrcBytes    = 4;
constexpr size_t   kNodeFileMaxBytes =
    kNodeFileHeaderBytes + kMaxNodes * kContactBlobRecordBytes + kNodeFileCrcBytes;

// The old NVS budget: the fallback blob may carry no more than this many radio rows (EST-463 item 0). S516:
// one constant, in mc_store.h, because the node rule (core/node_table.h) and the headers use the same number.
constexpr size_t kNodeNvsRadioCap = kMaxRadioNvs;
static_assert(kNodeNvsRadioCap == 32, "the NVS radio cap is the one the old blob held");

enum class NodeFileResult : uint8_t {
    Ok = 0,
    Empty,          // len == 0: no file yet, not an error
    Truncated,      // fewer bytes than the header/count/CRC claim
    BadMagic,
    BadVersion,
    BadRecordSize,
    BadCount,
    BadSize,        // more bytes than the header/count account for
    BadCrc,
};

const char* nodeFileResultText(NodeFileResult r);

// The standard reflected CRC-32 (poly 0xEDB88320), the same bytes on the host and the device.
uint32_t nodeFileCrc32(const uint8_t* data, size_t len);

// S515 (Claude, found on the device): "did this encoded file change?" A CRC-32 over a WHOLE node file is useless
// for that: the file ends with its own CRC-32, and CRC-32 over (data || CRC-32(data)) is the constant residue
// 0x2144DF1C for ANY data, so every file looked unchanged and radio.bin was never rewritten after the migration.
// The signature is the CRC over the file WITHOUT its trailing CRC field (0 for a buffer too short to hold one).
uint32_t nodeFileSignature(const uint8_t* file, size_t len);

// Encode `s` into `out`. `radio_only` selects the radio file (rows with in_radio) or the archive (all rows).
// `max_rows` caps the selection (0 = kMaxNodes). Returns the byte count, or 0 when `out` is null / cap is small.
size_t nodeFileEncode(const ContactStore& s, bool radio_only, size_t max_rows, uint8_t* out, size_t cap);

// Decode a whole file. `out` is untouched unless the answer is Ok (or Empty, which clears it).
NodeFileResult nodeFileDecode(const uint8_t* in, size_t len, ContactStore& out);

// S516/EST-472 item 2: is this WHOLE file trustworthy, without a store to decode into? Same checks as
// nodeFileDecode (magic, version, record width, count, exact length, CRC-32 over every byte before it) but it only
// reads. `readNodeFileOrTemp()` uses it BEFORE it renames a power-cut temp file into place: a torn temp must never
// become the real name (the ticket's rule: the temp file is CRC-checked before it is trusted).
NodeFileResult nodeFileCheck(const uint8_t* in, size_t len);

// S516/EST-475 (A2): is a refused temp file DAMAGED (safe to delete) or merely from a format THIS build does not
// know? Only Truncated and BadCrc prove damage. A wrong magic/version/record width/count/size may be a perfectly
// good file written by a newer (or older) app version; deleting it would destroy it for the version that can
// read it, so `readNodeFileOrTemp()` keeps those and just falls back. (The next write still reuses the same temp
// name — see PROGRESS; this rule only stops the boot from DESTROYING the file.)
inline bool nodeFileTempIsTorn(NodeFileResult r) {
    return r == NodeFileResult::Truncated || r == NodeFileResult::BadCrc;
}

// S516/EST-475 (B): when must the SD archive be rewritten? A STRUCTURAL change (a new row, a rename, a type or
// route change, a delete, a favourite or an ack) is what a power cut must not lose and keeps the old 30 s floor.
// A SOFT change (a repeat advert moving only last-heard/SNR/position) is already covered by the row on the card;
// rewriting all 318,016 bytes for it every 30 s cost ~38 MB/h and a main-task freeze each time, so it waits the
// same 10-minute floor the radio file gives the same fields. `last_write_ms == 0` means "nothing written yet".
constexpr uint32_t kArchiveWriteMs     = 30u * 1000u;
constexpr uint32_t kArchiveSoftWriteMs = 10u * 60u * 1000u;
inline bool archiveWriteDue(uint32_t now_ms, uint32_t last_write_ms, bool structural, bool soft) {
    if (!structural && !soft) return false;
    const uint32_t floor_ms = structural ? kArchiveWriteMs : kArchiveSoftWriteMs;
    return last_write_ms == 0 || (uint32_t)(now_ms - last_write_ms) >= floor_ms;
}

// Mark every row in_radio, up to `cap` (a radio file is by definition the radio list; a migrated v1-v3 NVS blob
// had no flag at all). Returns how many were marked.
size_t markAllInRadio(ContactStore& s, size_t cap = kMaxRadio);

struct MergeResult {
    size_t archive_rows = 0;
    size_t added        = 0;
    size_t updated      = 0;
    size_t dropped      = 0;   // the archive had more rows than the table could hold (never on a 2,000-row file)
};

// EST-463 item 3c: `live` already holds the radio rows (radio.bin, or the NVS fallback). Merge the archive by key:
//   * a key only in the archive is INSERTED archive-only (in_radio false, fully usable);
//   * a key in both keeps the RADIO row's in_radio and route (the radio file wins those);
//   * the newer heard_at wins the last-heard fields (name, type, hops, remote_ts, heard_at, rssi, snr, position);
//   * `fresh` always comes from the archive (it is the archive's record that carries the new-node dot).
MergeResult mergeArchive(ContactStore& live, const ContactStore& archive);

// EST-463 item 3c, one-time migration: the NVS blob's rows become radio nodes. A v1-v3 blob had no in_radio mark,
// so every row it carries is a radio node; a v4 blob already decodes the flags. Never marks more than kMaxRadio.
// Returns the number of radio rows after the call.
size_t migrateNvsToRadio(ContactStore& s, uint8_t nvs_blob_version);

}  // namespace mc
}  // namespace om
