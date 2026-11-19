// SPDX-License-Identifier: MIT
// The map packs' Fast Seek link map (S514/EST-452): the numbers FatFs's CLMT wants, what to do when the driver
// says the table was too small, and the ownership rule that frees the table exactly once.
//
// ⚠️ Why this exists. CONFIG_FATFS_USE_FASTSEEK is already 1 (build/tanmatsu/config/sdkconfig.h:913 makes
// ffconf.h FF_USE_FASTSEEK 1), but FatFs only takes its short path when the FIL carries a cluster link map
// table (`cltbl`, ff.c clmt_clust()). Without one, f_lseek() to a LOWER offset walks the FAT chain from the
// start of the file: on the 3.6 GB slo17 pack that is ~110k cluster entries / ~860 FAT sectors for EVERY
// backward seek. That walk is what a z17 tile's 465 ms at boot and most of the ~130 ms a tile inside a z17 fill
// are made of (measurements/tanmatsu-v43-glass-2026-11-19-S514.log: 7 tiles in 966 ms with only 30 card reads).
//
// The protocol is ff.c f_lseek(CREATE_LINKMAP): `tbl[0]` is the table's size in DWORDs. The driver writes
// (run length, first cluster) pairs, terminates them with a 0, then puts the number of DWORDs it USED back in
// `tbl[0]` — that is 2 + 2 * fragments (the size DWORD, two per fragment, the terminator). If the table was too
// small it returns FR_NOT_ENOUGH_CORE with the size it needs in `tbl[0]`, so ONE retry with exactly that size
// succeeds (`ulen <= tlen` then holds by construction).
//
// Everything here is arithmetic and pointer bookkeeping, so the host tests pin it and the device only allocates
// what these functions say and frees what the owner hands back. FatFs itself is deliberately NOT here: it cannot
// be faked on the host, and a fake would only test the fake.
#pragma once

#include <stdint.h>

namespace om {
namespace map {

// DWORDs a table needs for `fragments` fragments: the size DWORD, two per fragment, and the 0 terminator.
constexpr uint32_t kClmtHeaderItems      = 2;   // ff.c: ulen starts at 2 (size DWORD + terminator)
constexpr uint32_t kClmtItemsPerFragment = 2;   // (run length, first cluster)

constexpr uint32_t clmtItemsForFragments(uint32_t fragments) {
    return kClmtHeaderItems + kClmtItemsPerFragment * fragments;
}

// How many fragments a table of `items` DWORDs holds. A table the driver has filled reports ulen = 2 + 2 *
// fragments, so this is also the fragment count of the OPEN pack (what the open log line prints).
constexpr uint32_t clmtFragmentsForItems(uint32_t items) {
    return items <= kClmtHeaderItems ? 0 : (items - kClmtHeaderItems) / kClmtItemsPerFragment;
}

// The first attempt when a pack opens. A pack written by tools/tiles_export.py is a handful of fragments; 256 is
// far past that and costs 2 KB of PSRAM, so the retry below is the rare path and not the usual one. A retry is
// not free: the driver walks the whole FAT chain again to size the table.
constexpr uint32_t kClmtFirstTryFragments = 256;

// The ceiling on a retry: 1 MB = 262,144 DWORDs. Past that the pack is in tens of thousands of pieces, and a
// slower map is the better answer — a 1 MB table on a device with a 512 KB PSRAM budget for the map is a leak.
constexpr uint32_t kClmtMaxItems = 1024 * 1024 / 4;

struct ClmtRetry {
    bool     retry = false;   // allocate `items` DWORDs and call f_lseek(CREATE_LINKMAP) once more
    uint32_t items = 0;       // the size to allocate; 0 when there is no retry
};

// What to do after f_lseek(&fil, CREATE_LINKMAP) returned FR_NOT_ENOUGH_CORE: `needed` is what the driver wrote
// back into tbl[0]. Retry ONLY when it asks for MORE than we gave it — a driver that did not write the field
// hands back the size we passed (or 0), and retrying on that would be a loop on the same table — and only
// while the size is still sane. Exact size, not "a bit more": the driver's own test is `ulen <= tlen`.
constexpr ClmtRetry clmtRetry(uint32_t needed_items, uint32_t tried_items) {
    ClmtRetry r;
    if (needed_items > tried_items && needed_items <= kClmtMaxItems) {
        r.retry = true;
        r.items = needed_items;
    }
    return r;
}

// One open pack's link map table: at most one live allocation, and drop() hands it back exactly once, so
// closePack() — which runs on every reopen (a zoom across a band boundary) and at teardown — can neither free
// it twice nor leak it. No FatFs type here: the pointer is plain PSRAM the device frees.
struct ClmtTable {
    uint32_t* table = nullptr;   // the live table, or nullptr when fast seek is off for this pack
    uint32_t  items = 0;         // its size in DWORDs (the allocation, not the used count)

    // Take ownership of a table that proved itself to f_lseek(CREATE_LINKMAP). Refuses (and changes nothing)
    // when a table is already held, so the caller keeps its pointer and frees it: no allocation is lost.
    bool adopt(uint32_t* new_table, uint32_t new_items) {
        if (table != nullptr || new_table == nullptr) return false;
        table = new_table;
        items = new_items;
        return true;
    }

    // Hand the table back to the caller, who frees it, and clear the state HERE. The second call returns
    // nullptr: a close path that runs twice still frees once.
    uint32_t* drop() {
        uint32_t* out = table;
        table         = nullptr;
        items         = 0;
        return out;
    }
};

}  // namespace map
}  // namespace om
