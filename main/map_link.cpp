// SPDX-License-Identifier: MIT
#include "map_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/jpeg_decode.h"   // S506: the P4's hardware JPEG codec (SOC_JPEG_CODEC_SUPPORTED)
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ff.h"   // S513: the pack is read through FatFs, whose FSIZE_t reaches 4 GiB - 1
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"         // espressif/esp_jpeg: the software decoder, and the fallback
#include "map_clmt.h"             // S514/EST-452: the Fast Seek link map's numbers, retry rule and ownership
#include "map_pack.h"
#include "map_view.h"
#include "nvs.h"   // S514/EST-447: the MAP rows and the last position live in NVS "mcset"

namespace om {
namespace maplink {

namespace {

const char* TAG = "tm/map";

constexpr int    kTileSide  = om::map::kTilePx;
constexpr size_t kTileBytes = (size_t)kTileSide * (size_t)kTileSide * 2;

// Decoding touches the card (FATFS, reentrant in this IDF: FF_FS_REENTRANT 1) and one decoder. 12 KiB matches
// the radio task, where the measured headroom after real work was ~9 KB.
constexpr int kWorkerStack = 12288;
// Below the radio task (5) and below everything esp-hosted runs. The P4 has two cores, so this fills tiles on one
// while the panel task composes on the other; at priority 1 it would round-robin with the UI and halve it.
constexpr int kWorkerPriority = 2;

// The pack currently open. One at a time: zooming across a band boundary closes the old pair and opens the new
// one, which keeps the mount's open-file count at two instead of six.
struct OpenPack {
    int      z         = -1;
    FILE*    idx       = nullptr;   // tiles.idx stays on stdio: at most 21.6 MB, read by record number
    FIL      pack      = {};        // tiles.pack goes through FatFs: 3.6 GB, past what a signed long can seek
    bool     pack_open = false;
    char     drive     = 0;         // the FatFs drive the pack was found on ('0'..'9'), for the log and errors
    uint32_t count     = 0;
    // S514: the block search's PSRAM storage. Sized from `count` when the pack opens (NO index read at open);
    // the block keys fill lazily as searches visit them. ~132 KB for z17, well under the 512 KB budget.
    om::map::BlockCache block;
    om::map::Entry*     block_key_storage  = nullptr;
    uint8_t*            block_key_valid    = nullptr;
    om::map::Entry*     block_lru_storage  = nullptr;
    uint32_t*           block_lru_id       = nullptr;
    uint32_t*           block_lru_got      = nullptr;
    uint8_t*            block_lru_valid    = nullptr;
    // S514/EST-452: the Fast Seek cluster link map table FatFs is given through `pack.cltbl`. Owned here and
    // freed in closePack(), exactly once; nullptr means this pack seeks the slow way (the v48 behaviour).
    om::map::ClmtTable  clmt;
};

OpenPack           g_pack;
om::map::TileCache g_cache;
uint16_t*          g_pixels[om::map::kCacheSlots] = {nullptr};
bool               g_ready = false;

// S514/EST-447: defined with the other MAP-settings helpers at the end of this file; begin() calls it once.
void loadMapSettings();

// The worker's queue: replaced wholesale by showView(), drained one tile at a time.
om::map::TileRequest g_req[om::map::kMaxRequests];
int                  g_req_n = 0;
int                  g_req_i = 0;

SemaphoreHandle_t g_lock = nullptr;   // the cache, the queue, the pixel-slot array
SemaphoreHandle_t g_wake = nullptr;   // the worker's doorbell
TaskHandle_t      g_task = nullptr;

volatile bool g_busy   = false;
volatile bool g_landed = false;

// The hardware decoder, when the P4's JPEG codec comes up. The software decoder is the fallback: a codec that
// refuses to start is a slower map, never a broken one. `g_hw_rejected` remembers a codec the boot check measured
// to draw the wrong picture, so a later begin() cannot bring it back.
jpeg_decoder_handle_t g_hw          = nullptr;
bool                  g_hw_rejected = false;

// S514/EST-443: the old per-column RowHint is GONE. It was the S506 workaround for the ~18-read binary search
// (one full search per column, one 24-byte read per tile after that). The block search now answers a tile in at
// most one 4 KB block read plus the record read, and its key cache makes the tree top free; a hint would ADD a
// record read on every tile whose block is already cached. Host measurement (test_map.cpp): a 15-tile view is
// 166 reads with the plain search and 0.0/tile on the block search once warm.

// S513: a card read that failed is logged ONCE per fill, with its offset. Silence is what made the z17 holes
// invisible for a whole session: the tiles just landed in the failed ring and the map drew void.
bool g_read_fail_logged = false;

// What one drain of the queue cost, logged once, so the glass gives a number instead of a feel. S514: the line
// carries the zoom and the reads per tile, so the next glass test reads the improvement directly.
int      g_fill_tiles    = 0;
int      g_fill_z        = 0;
int64_t  g_fill_start_us = 0;
uint32_t g_card_reads    = 0;   // index records read since the fill started

// ---------------------------------------------------------------------------------------------- card + buffers

bool readRecord(void* ctx, uint32_t i, om::map::Entry* out) {
    FILE* f = static_cast<FILE*>(ctx);
    if (f == nullptr) return false;
    ++g_card_reads;
    if (fseek(f, (long)i * (long)om::map::kIdxRecordBytes, SEEK_SET) != 0) return false;
    uint8_t rec[om::map::kIdxRecordBytes];
    if (fread(rec, 1, sizeof(rec), f) != sizeof(rec)) return false;
    return om::map::decodeEntry(rec, out);
}

// S514: one card read for a whole 4 KB block (up to 170 records). This is the block search's second level, so the
// fill pays one read for the tile's block instead of one per binary-search probe.
uint32_t readBlockRecords(void* ctx, uint32_t first, om::map::Entry* out, uint32_t cap) {
    FILE* f = static_cast<FILE*>(ctx);
    if (f == nullptr || out == nullptr || cap == 0 || cap > om::map::kBlockRecords) return 0;
    if (first >= g_pack.count) return 0;
    uint32_t n = g_pack.count - first;
    if (n > cap) n = cap;
    ++g_card_reads;
    if (fseek(f, (long)first * (long)om::map::kIdxRecordBytes, SEEK_SET) != 0) return 0;
    static uint8_t buf[om::map::kBlockRecords * om::map::kIdxRecordBytes];
    const size_t   want = (size_t)n * om::map::kIdxRecordBytes;
    if (fread(buf, 1, want, f) != want) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!om::map::decodeEntry(buf + (size_t)i * om::map::kIdxRecordBytes, &out[i])) return 0;
    }
    return n;
}

void closePack() {
    // S514/EST-452: the link map is OURS, not FatFs's. Detach it from the FIL before the FIL goes away and take
    // it back from the owner, so this close path (which runs on every reopen) frees it exactly once and a second
    // close frees nothing. heap_caps_free() ignores nullptr, so the slow-seek case needs no branch.
#if FF_USE_FASTSEEK
    g_pack.pack.cltbl = nullptr;   // detach before the FIL goes away (the field exists only with fast seek on)
#endif
    uint32_t* const clmt = g_pack.clmt.drop();
    if (g_pack.idx) fclose(g_pack.idx);
    if (g_pack.pack_open) f_close(&g_pack.pack);
    heap_caps_free(clmt);
    // S514: the block cache is owned by the open pack; free it with it, so a zoom across a band boundary does not
    // leak 132 KB of PSRAM per pack.
    heap_caps_free(g_pack.block_key_storage);
    heap_caps_free(g_pack.block_key_valid);
    heap_caps_free(g_pack.block_lru_storage);
    heap_caps_free(g_pack.block_lru_id);
    heap_caps_free(g_pack.block_lru_got);
    heap_caps_free(g_pack.block_lru_valid);
    g_pack = OpenPack{};
}

// The FatFs drive the packs are on. This is NOT guessed and it is not the mount point: the drive letter is
// '0' + the volume index FatFs handed out when the card was mounted
// (managed_components/badgeteam__badge-bsp/targets/tanmatsu/badge_bsp_storage.c:248 calls
// esp_vfs_fat_sdmmc_mount, and IDF components/fatfs/vfs/vfs_fat_sdmmc.c:116,147-152 make that drive from
// ff_diskio_get_drive() — the first free FF_VOLUMES slot). It is "0:" today, but it would be another letter the
// day anything else mounts a FAT volume first. So instead of assuming, the drive is PROBED for the pack we want
// and logged, and the boot log says which one it was.
char probeDrive(const char* dir) {
    char    path[80];
    FILINFO info;
    for (char d = '0'; d <= '9'; d++) {
        snprintf(path, sizeof(path), "%c:/maps/%s/tiles.pack", d, dir);
        if (f_stat(path, &info) == FR_OK && info.fsize > 0) {
            ESP_LOGI(TAG, "fat: the %s pack is on drive %c: (%lu B)", dir, d, (unsigned long)info.fsize);
            return d;
        }
    }
    ESP_LOGW(TAG, "fat: no mounted drive has /maps/%s/tiles.pack (probed 0:-9:)", dir);
    return 0;
}

#if FF_USE_FASTSEEK
// S514/EST-452: FatFs Fast Seek. CONFIG_FATFS_USE_FASTSEEK is 1 (build/tanmatsu/config/sdkconfig.h:913 ->
// ffconf.h FF_USE_FASTSEEK), but f_lseek() only takes its short path when the FIL carries a cluster link map
// table (`cltbl`, ff.c clmt_clust()). Without one it walks the FAT chain from the start of the file to reach any
// LOWER offset, which on slo17 is ~110k cluster entries / ~860 FAT sectors per seek: the 465 ms boot tile and
// most of the ~130 ms a tile inside a z17 fill. Building the table costs ONE such walk, here at open; afterwards
// a seek is a scan of a handful of (length, first cluster) pairs.
//
// Not fatal on any failure: without a table the pack reads exactly as it did in v48, only slower. The FIL is
// never left pointing at a table that has been freed.
void attachLinkMap(int z) {
    uint32_t items = om::map::clmtItemsForFragments(om::map::kClmtFirstTryFragments);
    for (int attempt = 1; attempt <= 2; attempt++) {
        const size_t bytes = (size_t)items * sizeof(uint32_t);
        // PSRAM: a few KB next to the block index, and the map's budget is PSRAM. tbl[0] must be the table's
        // size in DWORDs before the call — that is the only field the driver reads before it writes.
        uint32_t* tbl = (uint32_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        if (tbl == nullptr) {
            ESP_LOGW(TAG, "z%d: no PSRAM for the fast-seek link map (%u B): seeking as before", z, (unsigned)bytes);
            return;
        }
        tbl[0]            = items;
        g_pack.pack.cltbl = tbl;
        const FRESULT fr  = f_lseek(&g_pack.pack, CREATE_LINKMAP);
        if (fr == FR_OK) {
            // tbl[0] now holds the DWORDs the driver USED (2 + 2 * fragments), which is the fragment count.
            if (!g_pack.clmt.adopt(tbl, items)) {   // cannot happen: closePack() dropped the previous one
                g_pack.pack.cltbl = nullptr;
                heap_caps_free(tbl);
                ESP_LOGW(TAG, "z%d: fast seek off (the previous link map was still held): seeking as before", z);
                return;
            }
            ESP_LOGI(TAG, "z%d: fast seek on, link map %u fragment(s), %u B", z,
                     (unsigned)om::map::clmtFragmentsForItems(tbl[0]), (unsigned)bytes);
            return;
        }
        // Too small: the driver wrote the size it needs into tbl[0]. Detach and read it before the free.
        g_pack.pack.cltbl = nullptr;
        const uint32_t           needed = tbl[0];
        heap_caps_free(tbl);
        const om::map::ClmtRetry again  = om::map::clmtRetry(needed, items);
        if (!again.retry) {
            ESP_LOGW(TAG, "z%d: fast seek off (f_lseek CREATE_LINKMAP: %d, %u DWORD(s) needed): seeking as before",
                     z, (int)fr, (unsigned)needed);
            return;
        }
        ESP_LOGI(TAG, "z%d: the pack needs a %u-DWORD link map (%u B); retrying once", z, (unsigned)again.items,
                 (unsigned)(again.items * sizeof(uint32_t)));
        items = again.items;
    }
    ESP_LOGW(TAG, "z%d: fast seek off after two tries: seeking as before", z);
}
#endif

// Open the pack that serves `z`, reusing it when it is already the open one. Single-owner: the worker task, or
// boot before any view exists. The FIL lives here and is closed in closePack() — the om-map worker is the only
// opener, which is what keeps the mount's open-file count at two.
bool ensurePack(int z) {
    if (g_pack.z == z && g_pack.idx != nullptr && g_pack.pack_open) return true;
    closePack();
    const char* dir = om::map::packDirForZoom(z);
    if (dir == nullptr) {
        ESP_LOGW(TAG, "z%d: no pack covers this zoom", z);
        return false;
    }
    const char drive = probeDrive(dir);
    if (drive == 0) return false;

    char idxPath[80], packPath[80];
    // S513 (Claude, review of EST-428): the index stays on STDIO, so it needs the VFS mount path. A FatFs drive
    // path ("0:/...") matches no VFS prefix (components/vfs/vfs.c:845 matches by path_prefix, here "/sdcard"),
    // so fopen() on it fails and EVERY pack would refuse to open. Only the pack goes through FatFs.
    snprintf(idxPath, sizeof(idxPath), "/sdcard/maps/%s/tiles.idx", dir);
    snprintf(packPath, sizeof(packPath), "%c:/maps/%s/tiles.pack", drive, dir);
    FILE* idx = fopen(idxPath, "rb");
    if (idx == nullptr) {
        ESP_LOGW(TAG, "%s: not on the card", idxPath);
        return false;
    }
    if (fseek(idx, 0, SEEK_END) != 0) {
        fclose(idx);
        return false;
    }
    const long size = ftell(idx);
    if (size <= 0 || (size % (long)om::map::kIdxRecordBytes) != 0) {
        ESP_LOGW(TAG, "%s: %ld bytes is not whole records", idxPath, size);
        fclose(idx);
        return false;
    }
    // Opened straight into the OpenPack, so there is no FIL copy to get wrong: one owner, closed in closePack().
    const FRESULT fr = f_open(&g_pack.pack, packPath, FA_READ);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "%s: f_open failed (%d)", packPath, (int)fr);
        g_pack.pack = FIL{};
        fclose(idx);
        return false;
    }
    g_pack.z         = z;
    g_pack.idx       = idx;
    g_pack.pack_open = true;
    g_pack.drive     = drive;
    g_pack.count     = (uint32_t)(size / (long)om::map::kIdxRecordBytes);

    // S514: size the block search's PSRAM. This is allocation only — opening a pack reads NO index records, so it
    // stays fast; the block keys fill lazily as searches visit them. If PSRAM is short, the pack still works: the
    // search falls back to the old binary search (correct, just more reads).
    const uint32_t blocks = om::map::blockCount(g_pack.count);
    g_pack.block_key_storage = (om::map::Entry*)heap_caps_malloc((size_t)blocks * sizeof(om::map::Entry),
                                                                 MALLOC_CAP_SPIRAM);
    g_pack.block_key_valid   = (uint8_t*)heap_caps_calloc(blocks, 1, MALLOC_CAP_SPIRAM);
    g_pack.block_lru_storage = (om::map::Entry*)heap_caps_malloc(
        (size_t)om::map::kBlockLruSlots * om::map::kBlockRecords * sizeof(om::map::Entry), MALLOC_CAP_SPIRAM);
    g_pack.block_lru_id    = (uint32_t*)heap_caps_calloc(om::map::kBlockLruSlots, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    g_pack.block_lru_got   = (uint32_t*)heap_caps_calloc(om::map::kBlockLruSlots, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    g_pack.block_lru_valid = (uint8_t*)heap_caps_calloc(om::map::kBlockLruSlots, 1, MALLOC_CAP_SPIRAM);
    if (g_pack.block_key_storage && g_pack.block_key_valid && g_pack.block_lru_storage && g_pack.block_lru_id &&
        g_pack.block_lru_got && g_pack.block_lru_valid) {
        g_pack.block.key         = g_pack.block_key_storage;
        g_pack.block.key_valid   = g_pack.block_key_valid;
        g_pack.block.key_cap     = blocks;
        g_pack.block.block       = g_pack.block_lru_storage;
        g_pack.block.block_id    = g_pack.block_lru_id;
        g_pack.block.block_got   = g_pack.block_lru_got;
        g_pack.block.block_valid = g_pack.block_lru_valid;
        g_pack.block.lru_slots   = om::map::kBlockLruSlots;
        g_pack.block.next        = 0;
        ESP_LOGI(TAG, "z%d: block index %u block(s), %u B keys + %u B LRU", z, (unsigned)blocks,
                 (unsigned)(blocks * sizeof(om::map::Entry)),
                 (unsigned)(om::map::kBlockLruSlots * om::map::kBlockRecords * sizeof(om::map::Entry)));
    } else {
        heap_caps_free(g_pack.block_key_storage);
        heap_caps_free(g_pack.block_key_valid);
        heap_caps_free(g_pack.block_lru_storage);
        heap_caps_free(g_pack.block_lru_id);
        heap_caps_free(g_pack.block_lru_got);
        heap_caps_free(g_pack.block_lru_valid);
        g_pack.block_key_storage = nullptr;
        g_pack.block_key_valid   = nullptr;
        g_pack.block_lru_storage = nullptr;
        g_pack.block_lru_id      = nullptr;
        g_pack.block_lru_got     = nullptr;
        g_pack.block_lru_valid   = nullptr;
        g_pack.block             = om::map::BlockCache{};
        ESP_LOGW(TAG, "z%d: no PSRAM for the block index; binary search per tile", z);
    }
    ESP_LOGI(TAG, "z%d: pack %s open on drive %c:, %u tiles, %lu B", z, dir, drive, (unsigned)g_pack.count,
             (unsigned long)f_size(&g_pack.pack));
#if FF_USE_FASTSEEK
    // S514/EST-452: last, so the open's own numbers are logged first and a slow map build cannot be mistaken for
    // a slow open. A failure here leaves the pack usable (slow seeks), which is exactly v48's behaviour.
    attachLinkMap(z);
#endif
    return true;
}

// A buffer the hardware codec may touch. Both directions go through the codec's own allocator, as its example
// does: the output must be cache-line aligned in address AND size (a plain heap_caps_malloc is not), and the
// input is what the codec's DMA reads back out of PSRAM.
void* allocDecoderMem(size_t bytes, jpeg_dec_buffer_alloc_direction_t dir) {
    jpeg_decode_memory_alloc_cfg_t mc = {};
    mc.buffer_direction               = dir;
    size_t got                        = 0;
    void*  p                          = jpeg_alloc_decoder_mem(bytes, &mc, &got);
    if (p == nullptr) {
        ESP_LOGE(TAG, "no PSRAM for %u bytes (%s buffer)", (unsigned)bytes,
                 dir == JPEG_DEC_ALLOC_OUTPUT_BUFFER ? "output" : "input");
    }
    return p;
}

uint16_t* allocTileBuffer() { return (uint16_t*)allocDecoderMem(kTileBytes, JPEG_DEC_ALLOC_OUTPUT_BUFFER); }

uint16_t* slotPixels(int slot) {
    if (slot < 0 || slot >= om::map::kCacheSlots) return nullptr;
    if (g_pixels[slot] == nullptr) g_pixels[slot] = allocTileBuffer();
    return g_pixels[slot];
}

// ---------------------------------------------------------------------------------------------- index lookup

int findRecord(uint8_t z, uint32_t x, uint32_t y) {
    // S514: the block search when its PSRAM is there, the old binary search otherwise. Same answer either way;
    // the host test pins that equality on a synthetic index.
    if (g_pack.block.key_cap > 0) {
        return om::map::findTileBlocks(g_pack.count, readRecord, readBlockRecords, g_pack.idx, g_pack.block, z, x, y);
    }
    return om::map::findTile(g_pack.count, readRecord, g_pack.idx, z, x, y);
}

bool lookupEntry(uint8_t z, uint32_t x, uint32_t y, om::map::Entry* out) {
    // S514: no column hint any more — the block search reads the block once and the key cache makes the tree top
    // free, so a hint's extra record read would only add card traffic (see the comment above findRecord).
    const int record = findRecord(z, x, y);
    if (record < 0) return false;   // a hole in the pack
    return readRecord(g_pack.idx, (uint32_t)record, out);
}

// Read the compressed bytes of one tile into a fresh PSRAM buffer. nullptr = the pack does not have it, or the card
// refused. The caller frees it.
//
// S513: the read is f_lseek/f_read on the FatFs FIL, NOT fseek/fread. picolibc's off_t is a signed 32-bit long, so
// `(long)e.offset` turned every z17 offset past 2^31-1 negative, fseek returned -1, and the tile became a silent
// hole in the failed ring (Maribor, Ptuj, Murska Sobota, Krsko). FSIZE_t here is a 32-bit UNSIGNED DWORD
// (ffconf.h:282 FF_FS_EXFAT 0 -> ff.h:82), so the whole 4 GiB - 1 is reachable; FF_FS_REENTRANT is 1
// (ffconf.h:326), so a direct call is safe next to the VFS.
uint8_t* readTileBytes(uint8_t z, uint32_t x, uint32_t y, uint32_t* out_len) {
    if (!ensurePack(z)) return nullptr;
    om::map::Entry e;
    if (!lookupEntry(z, x, y, &e)) return nullptr;

    om::map::ReadRange rr;
    if (!om::map::readRangeFor(e, &rr)) {
        // Cannot be a FAT32 range: say so with the numbers instead of seeking somewhere else on the card.
        if (!g_read_fail_logged) {
            g_read_fail_logged = true;
            ESP_LOGE(TAG, "z%d %u/%u: offset %llu + %u B is past 4 GiB - 1, not a FAT32 tile (logged once per fill)",
                     z, (unsigned)x, (unsigned)y, (unsigned long long)e.offset, (unsigned)e.length);
        }
        return nullptr;
    }

    uint8_t* raw = (uint8_t*)allocDecoderMem(rr.length, JPEG_DEC_ALLOC_INPUT_BUFFER);
    if (raw == nullptr) return nullptr;
    UINT          br = 0;
    const FRESULT seek = f_lseek(&g_pack.pack, (FSIZE_t)rr.offset);
    const FRESULT read = (seek == FR_OK) ? f_read(&g_pack.pack, raw, rr.length, &br) : seek;
    if (seek != FR_OK || read != FR_OK || br != rr.length) {
        // Once per fill: a hole must never be silent again, and a flood of the same line is not more information.
        if (!g_read_fail_logged) {
            g_read_fail_logged = true;
            ESP_LOGE(TAG, "z%d %u/%u: READ FAILED at offset %u (%u B, seek %d read %d got %u) on drive %c: "
                          "- logged once per fill",
                     z, (unsigned)x, (unsigned)y, (unsigned)rr.offset, (unsigned)rr.length, (int)seek, (int)read,
                     (unsigned)br, g_pack.drive);
        }
        heap_caps_free(raw);
        return nullptr;
    }
    *out_len = rr.length;
    return raw;
}

// ---------------------------------------------------------------------------------------------- decoders
// The software decoder is the REFERENCE: its pixels are what was confirmed on the glass in S503. The hardware
// codec is only allowed to draw tiles if it can reproduce them, which is decided at boot (verifyDecoders) rather
// than assumed. A fast map with wrong colours is worse than a slower correct one.
jpeg_dec_rgb_element_order_t g_hw_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
jpeg_yuv_rgb_conv_std_t     g_hw_conv  = JPEG_YUV_RGB_CONV_STD_BT601;

bool decodeHwWith(const uint8_t* raw, uint32_t len, uint16_t* out, jpeg_dec_rgb_element_order_t order,
                  jpeg_yuv_rgb_conv_std_t conv, bool quiet) {
    if (g_hw == nullptr) return false;
    jpeg_decode_cfg_t cfg = {};
    cfg.output_format     = JPEG_DECODE_OUT_FORMAT_RGB565;
    cfg.rgb_order         = order;
    cfg.conv_std          = conv;
    uint32_t        out_size = 0;
    const esp_err_t err =
        jpeg_decoder_process(g_hw, &cfg, raw, len, (uint8_t*)out, (uint32_t)kTileBytes, &out_size);
    if (err != ESP_OK) {
        if (!quiet) ESP_LOGW(TAG, "hardware decode failed (%d); software fallback", (int)err);
        return false;
    }
    if (out_size != kTileBytes) {
        if (!quiet) {
            ESP_LOGW(TAG, "hardware decode produced %u bytes, expected %u", (unsigned)out_size, (unsigned)kTileBytes);
        }
        return false;
    }
    return true;
}

bool decodeHw(const uint8_t* raw, uint32_t len, uint16_t* out) {
    return decodeHwWith(raw, len, out, g_hw_order, g_hw_conv, false);
}


bool decodeSw(const uint8_t* raw, uint32_t len, uint16_t* out, int* out_w, int* out_h) {
    esp_jpeg_image_cfg_t    cfg  = {};
    esp_jpeg_image_output_t info = {};
    cfg.indata                   = const_cast<uint8_t*>(raw);   // esp_jpeg takes a non-const pointer but only reads
    cfg.indata_size              = len;
    cfg.outbuf                   = (uint8_t*)out;
    cfg.outbuf_size              = (uint32_t)kTileBytes;
    cfg.out_format               = JPEG_IMAGE_FORMAT_RGB565;
    cfg.out_scale                = JPEG_IMAGE_SCALE_0;
    const esp_err_t err          = esp_jpeg_decode(&cfg, &info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "software decode failed (%d)", (int)err);
        return false;
    }
    if (info.width != kTileSide || info.height != kTileSide) {
        ESP_LOGW(TAG, "tile is %ux%u, not %d square", (unsigned)info.width, (unsigned)info.height, kTileSide);
    }
    if (out_w) *out_w = (int)info.width;
    if (out_h) *out_h = (int)info.height;
    return true;
}

// Read and decode one tile into `out`. Single-owner (the worker, or boot before any view exists).
bool decodeInto(uint8_t z, uint32_t x, uint32_t y, uint16_t* out, int* out_w, int* out_h, bool* used_hw) {
    uint32_t len = 0;
    uint8_t* raw = readTileBytes(z, x, y, &len);
    if (raw == nullptr) return false;
    bool ok = decodeHw(raw, len, out);
    if (used_hw) *used_hw = ok;
    if (ok) {
        if (out_w) *out_w = kTileSide;
        if (out_h) *out_h = kTileSide;
    } else {
        ok = decodeSw(raw, len, out, out_w, out_h);
    }
    heap_caps_free(raw);
    return ok;
}

void logFill() {
    if (g_fill_tiles == 0) return;
    const long total_ms = (long)((esp_timer_get_time() - g_fill_start_us) / 1000);
    // S514/EST-443 item 3: the zoom and the reads per tile, so the next glass test reads the improvement directly:
    // "fill z15: 12 tile(s) in 309 ms (73 card reads, 6.1/tile, hardware decode)".
    ESP_LOGI(TAG, "fill z%d: %d tile(s) in %ld ms (%u card reads, %.1f/tile, %s decode)", g_fill_z, g_fill_tiles,
             total_ms, (unsigned)g_card_reads, (double)g_card_reads / (double)g_fill_tiles,
             g_hw ? "hardware" : "software");
    g_fill_tiles = 0;
}

// ---------------------------------------------------------------------------------------------- the worker

void fillTask(void*) {
    for (;;) {
        xSemaphoreTake(g_wake, portMAX_DELAY);
        for (;;) {
            uint8_t   z    = 0;
            uint32_t  x    = 0;
            uint32_t  y    = 0;
            int       slot = -1;
            uint16_t* out  = nullptr;

            // Pick and reserve in one critical section, so a re-plan from the page can never pair a tile with the
            // wrong zoom's pack, or hand the same tile to a second decode.
            xSemaphoreTake(g_lock, portMAX_DELAY);
            while (g_req_i < g_req_n) {
                const om::map::TileRequest r = g_req[g_req_i++];
                if (g_cache.stateOf(r.z, r.x, r.y) != om::map::SlotState::Empty) continue;   // cached / in flight
                if (g_cache.isFailed(r.z, r.x, r.y)) continue;                              // a known hole
                slot = g_cache.take(r.z, r.x, r.y);
                if (slot >= 0) {
                    z   = r.z;
                    x   = r.x;
                    y   = r.y;
                    out = slotPixels(slot);
                }
                break;   // take() never fails with one worker, but if it did the list would be retried below
            }
            const bool drained = (g_req_i >= g_req_n);
            if (slot < 0 && drained) g_busy = false;
            xSemaphoreGive(g_lock);

            if (slot < 0) {
                if (drained) {
                    logFill();
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }

            const int64_t t0      = esp_timer_get_time();
            bool          used_hw = false;
            const bool    ok      = (out != nullptr) && decodeInto(z, x, y, out, nullptr, nullptr, &used_hw);
            const long    us      = (long)(esp_timer_get_time() - t0);

            xSemaphoreTake(g_lock, portMAX_DELAY);
            g_cache.commit(slot, ok);
            ++g_fill_tiles;
            g_landed = true;
            xSemaphoreGive(g_lock);

            if (ok) {
                ESP_LOGD(TAG, "z%d %u/%u decoded in %ld ms (%s)", z, (unsigned)x, (unsigned)y, us / 1000,
                         used_hw ? "hw" : "sw");
            }
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------- public API

void begin() {
    if (g_lock == nullptr) g_lock = xSemaphoreCreateMutex();
    if (g_wake == nullptr) g_wake = xSemaphoreCreateBinary();
    loadMapSettings();   // S514/EST-447: NVS is up by now (app_main initialises it before the card mount)

    // Once the boot check has refused the codec, it stays refused: begin() can be called again (card remount) and
    // must not quietly resurrect a decoder that was measured to draw the wrong picture.
    if (g_hw == nullptr && !g_hw_rejected) {
        jpeg_decode_engine_cfg_t cfg = {};
        cfg.intr_priority            = 0;
        cfg.timeout_ms               = 1000;
        const esp_err_t err          = jpeg_new_decoder_engine(&cfg, &g_hw);
        if (err != ESP_OK) {
            g_hw = nullptr;
            ESP_LOGW(TAG, "no hardware JPEG decoder (%d): software decode, about 57 ms a tile", (int)err);
        } else {
            ESP_LOGI(TAG, "jpeg: the P4's hardware decoder is up");
        }
    }

    if (g_task == nullptr && g_lock != nullptr && g_wake != nullptr) {
        if (xTaskCreate(fillTask, "om-map", kWorkerStack, nullptr, kWorkerPriority, &g_task) != pdPASS) {
            g_task = nullptr;
            ESP_LOGE(TAG, "no task for the tile worker: the map will not fill in");
        }
    }

    if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY);
    g_cache.reset();
    g_req_n = g_req_i = 0;
    g_busy            = false;
    g_landed          = false;
    if (g_lock) xSemaphoreGive(g_lock);
    g_ready = true;
}

bool ready() { return g_ready; }

bool drawTile(uint8_t z, uint32_t x, uint32_t y, int dx, int dy, TileBlit blit, void* ctx) {
    if (!g_ready || g_lock == nullptr || blit == nullptr) return false;
    // A short take, not portMAX_DELAY: if the worker ever held the lock for longer than a moment, the page skips
    // this tile for one frame instead of freezing the UI on a key press.
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const int slot = g_cache.find(z, x, y);
    if (slot < 0 || g_pixels[slot] == nullptr) {
        xSemaphoreGive(g_lock);
        return false;   // not decoded yet: the page leaves the void colour, the worker brings it
    }
    // The copy happens under the lock: one tile is a fraction of a millisecond of memcpy, and the worker must not
    // evict this buffer while it is being read.
    blit(g_pixels[slot], kTileSide, kTileSide, dx, dy, ctx);
    xSemaphoreGive(g_lock);
    return true;
}

bool drawTileScaled(uint8_t z, uint32_t x, uint32_t y, int sx, int sy, int sw, int sh, int dx, int dy, int dw,
                    int dh, TileBlitScaled blit, void* ctx) {
    if (!g_ready || g_lock == nullptr || blit == nullptr) return false;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return false;
    if (sx < 0 || sy < 0 || sx + sw > kTileSide || sy + sh > kTileSide) return false;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    // peek(), NOT find(): a placeholder must not touch the LRU and evict a tile the fill is about to use.
    const int slot = g_cache.peek(z, x, y);
    if (slot < 0 || g_pixels[slot] == nullptr) {
        xSemaphoreGive(g_lock);
        return false;
    }
    blit(g_pixels[slot], kTileSide, kTileSide, sx, sy, sw, sh, dx, dy, dw, dh, ctx);
    xSemaphoreGive(g_lock);
    return true;
}

int showView(uint8_t z, const om::map::ViewTiles& vt) {
    if (!g_ready || g_lock == nullptr) return 0;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    const int n = om::map::planTiles(vt, z, g_cache, g_req, om::map::kMaxRequests);
    if (n < 0) {
        xSemaphoreGive(g_lock);
        ESP_LOGW(TAG, "view needs %d tiles, more than the queue's %d", vt.count, (int)om::map::kMaxRequests);
        return -1;
    }
    g_req_n = n;
    g_req_i = 0;
    if (n > 0) {
        if (!g_busy || g_fill_z != z) {   // a new fill (or a zoom mid-fill): reset the accounting
            g_fill_tiles       = 0;
            g_fill_z           = z;
            g_card_reads       = 0;
            g_read_fail_logged = false;   // one read failure per fill is logged, not one per tile
            g_fill_start_us    = esp_timer_get_time();
        }
        g_busy = true;
    }
    xSemaphoreGive(g_lock);
    if (n > 0) xSemaphoreGive(g_wake);
    return n;
}

bool busy() { return g_busy; }

bool takeLanded() {
    if (!g_landed) return false;
    g_landed = false;
    return true;
}

// ---------------------------------------------------------------------------------------------- boot checks

// One probe point: read and decode a single tile at `z` and log the numbers. Shared by both probe points so the
// second one cannot drift from the first.
void probeOne(int z, double lat, double lon) {
    const uint32_t tx = om::map::tileXForLon(lon, z);
    const uint32_t ty = om::map::tileYForLat(lat, z);

    xSemaphoreTake(g_lock, portMAX_DELAY);
    const int       slot = g_cache.take((uint8_t)z, tx, ty);
    uint16_t* const out  = slotPixels(slot);
    xSemaphoreGive(g_lock);
    if (slot < 0 || out == nullptr) {
        ESP_LOGW(TAG, "z%d %u/%u: no cache slot", z, (unsigned)tx, (unsigned)ty);
        return;
    }

    const int64_t t0      = esp_timer_get_time();
    int           w = 0, h = 0;
    bool          used_hw = false;
    const bool    ok      = decodeInto((uint8_t)z, tx, ty, out, &w, &h, &used_hw);
    const long    us      = (long)(esp_timer_get_time() - t0);

    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_cache.commit(slot, ok);
    xSemaphoreGive(g_lock);

    if (!ok) {
        ESP_LOGW(TAG, "z%d %u/%u: not available", z, (unsigned)tx, (unsigned)ty);
        return;
    }
    ESP_LOGI(TAG, "z%d tile %u/%u -> %dx%d in %ld ms (%s), px0 0x%04X", z, (unsigned)tx, (unsigned)ty, w, h,
             us / 1000, used_hw ? "hw" : "sw", out[0]);
}

void probe(double lat, double lon) {
    if (!g_ready || g_lock == nullptr) return;
    g_read_fail_logged = false;   // a boot probe reports its own failure, even if a fill already logged one
    ESP_LOGI(TAG, "probe: %.5f, %.5f (%s decoder)", lat, lon, g_hw ? "hardware" : "software");
    for (int z = 16; z >= 15; z--) probeOne(z, lat, lon);

    // S513: the SECOND probe point, and the reason it exists — this one is z17 on the far side of the 2 GiB mark of
    // slo17 (offset ~2.49 GB), which the old signed-long fseek() could not read at all. If the boot log shows this
    // line with real pixels, the FatFs path reaches the whole pack; if it does not, the line says so with an offset.
    ESP_LOGI(TAG, "probe: %.5f, %.5f (east, past 2 GiB of slo17)", 46.5547, 15.6459);
    probeOne(17, 46.5547, 15.6459);
}

// Mean per-pixel channel distance between two RGB565 buffers, over a sample. Small (a few LSB) means "the same
// picture decoded by two implementations"; large means a different picture, or a different colour conversion.
int pixelDistance(const uint16_t* a, const uint16_t* b) {
    const size_t step = 16;   // 4096 samples: plenty for a colour decision, cheap at boot
    long         sum  = 0;
    int          n    = 0;
    for (size_t i = 0; i < (size_t)kTileSide * (size_t)kTileSide; i += step) {
        const uint16_t x = a[i];
        const uint16_t y = b[i];
        const int      dr = (int)((x >> 11) & 0x1F) - (int)((y >> 11) & 0x1F);
        const int      dg = (int)((x >> 5) & 0x3F) - (int)((y >> 5) & 0x3F);
        const int      db = (int)(x & 0x1F) - (int)(y & 0x1F);
        sum += (dr < 0 ? -dr : dr) + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db);
        ++n;
    }
    return n ? (int)(sum / n) : 9999;
}

// Above this mean channel distance the hardware codec is not producing the reference picture (the two decoders
// legitimately differ by a couple of LSB; a wrong colour conversion is tens).
constexpr int kMaxHwDistance = 12;

void verifyDecoders(double lat, double lon) {
    if (!g_ready || g_lock == nullptr) return;
    if (g_hw == nullptr) {
        ESP_LOGW(TAG, "decoder check: no hardware decoder, software only");
        return;
    }
    const int      z  = 16;
    const uint32_t tx = om::map::tileXForLon(lon, z);
    const uint32_t ty = om::map::tileYForLat(lat, z);

    uint32_t len = 0;
    uint8_t* raw = readTileBytes((uint8_t)z, tx, ty, &len);
    if (raw == nullptr) {
        ESP_LOGW(TAG, "decoder check: no tile to compare");
        return;
    }
    uint16_t* hw_px = allocTileBuffer();
    uint16_t* sw_px = allocTileBuffer();
    if (hw_px == nullptr || sw_px == nullptr) {
        if (hw_px) heap_caps_free(hw_px);
        if (sw_px) heap_caps_free(sw_px);
        heap_caps_free(raw);
        return;
    }

    // The tile's size from its own header. Its `sample_method` field is NOT trustworthy here: on this IDF build
    // jpeg_decoder_get_info left it unset and the first build logged 942682966, so only the size is reported.
    jpeg_decode_picture_info_t pic = {};
    if (jpeg_decoder_get_info(raw, len, &pic) == ESP_OK) {
        ESP_LOGI(TAG, "decoder check: tile %ux%u", (unsigned)pic.width, (unsigned)pic.height);
    }

    // The reference: the software decoder, the one whose pixels were confirmed on the glass.
    const int64_t t1    = esp_timer_get_time();
    const bool    sw_ok = decodeSw(raw, len, sw_px, nullptr, nullptr);
    const long    sw_us = (long)(esp_timer_get_time() - t1);
    if (!sw_ok) {
        ESP_LOGW(TAG, "decoder check: the software decoder failed; hardware left as configured");
        heap_caps_free(hw_px);
        heap_caps_free(sw_px);
        heap_caps_free(raw);
        return;
    }

    // Every colour option the driver offers, scored against the reference.
    struct Combo {
        jpeg_dec_rgb_element_order_t order;
        jpeg_yuv_rgb_conv_std_t      conv;
        const char*                  name;
    };
    const Combo combos[] = {
        {JPEG_DEC_RGB_ELEMENT_ORDER_RGB, JPEG_YUV_RGB_CONV_STD_BT601, "RGB/BT601"},
        {JPEG_DEC_RGB_ELEMENT_ORDER_RGB, JPEG_YUV_RGB_CONV_STD_BT709, "RGB/BT709"},
        {JPEG_DEC_RGB_ELEMENT_ORDER_BGR, JPEG_YUV_RGB_CONV_STD_BT601, "BGR/BT601"},
        {JPEG_DEC_RGB_ELEMENT_ORDER_BGR, JPEG_YUV_RGB_CONV_STD_BT709, "BGR/BT709"},
    };
    int  best_dist = 9999;
    int  best_i    = -1;
    long best_us   = 0;
    for (int i = 0; i < 4; i++) {
        const int64_t t0 = esp_timer_get_time();
        if (!decodeHwWith(raw, len, hw_px, combos[i].order, combos[i].conv, true)) {
            ESP_LOGW(TAG, "decoder check: %s -> decode refused", combos[i].name);
            continue;
        }
        const long us   = (long)(esp_timer_get_time() - t0);
        const int  dist = pixelDistance(hw_px, sw_px);
        ESP_LOGI(TAG, "decoder check: %s -> %ld ms, distance %d (px0 0x%04X vs 0x%04X)", combos[i].name, us / 1000,
                 dist, hw_px[0], sw_px[0]);
        if (dist < best_dist) {
            best_dist = dist;
            best_i    = i;
            best_us   = us;
        }
    }

    if (best_i >= 0 && best_dist <= kMaxHwDistance) {
        g_hw_order = combos[best_i].order;
        g_hw_conv  = combos[best_i].conv;
        ESP_LOGI(TAG, "decoder check: using hardware decode, %s (distance %d, %ld ms vs sw %ld ms)",
                 combos[best_i].name, best_dist, best_us / 1000, sw_us / 1000);
    } else {
        // Correct colours matter more than speed, and this is decided from a measurement, not a guess.
        jpeg_del_decoder_engine(g_hw);
        g_hw          = nullptr;
        g_hw_rejected = true;
        ESP_LOGW(TAG,
                 "decoder check: NO hardware colour option matches the reference (best %d > %d): hardware decode "
                 "DISABLED, software decode in use (about %ld ms a tile)",
                 best_dist, kMaxHwDistance, sw_us / 1000);
    }

    heap_caps_free(hw_px);
    heap_caps_free(sw_px);
    heap_caps_free(raw);
}

// ------------------------------------------------------------------------- S514/EST-447: the MAP settings
// NVS "mcset": mapstart (u8), mapzoom (u8), lastz/lastwx/lastwy. The choices and the write timing are
// core/map_settings.h; this file only persists them. The caches are filled by begin() (NVS is already up:
// app_main initialises it before the card is mounted) so a Settings getter never opens NVS on a draw.

namespace {
uint8_t            g_start_position     = om::map::kStartLjubljana;
uint8_t            g_start_zoom         = (uint8_t)om::map::kStartZoomDefault;
bool               g_map_settings_loaded = false;
om::map::SavedView g_last_view;
bool               g_last_view_loaded   = false;

void loadMapSettings() {
    if (g_map_settings_loaded) return;
    g_map_settings_loaded = true;
    nvs_handle_t h;
    if (nvs_open("mcset", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 0;
    if (nvs_get_u8(h, "mapstart", &v) == ESP_OK)
        g_start_position = om::map::startPositionFromStored(v, true);
    if (nvs_get_u8(h, "mapzoom", &v) == ESP_OK)
        g_start_zoom = (uint8_t)om::map::startZoomFromStored(v, true);
    nvs_close(h);
    ESP_LOGI(TAG, "map settings: start %s, zoom z%u", om::map::startPositionLabel(g_start_position),
             (unsigned)g_start_zoom);
}

bool writeU8(const char* key, uint8_t value) {
    nvs_handle_t h;
    if (nvs_open("mcset", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t r = nvs_set_u8(h, key, value);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) ESP_LOGE(TAG, "map settings: %s write failed (%s)", key, esp_err_to_name(r));
    return r == ESP_OK;
}

// A pack directory's cache slot, or -1 for a name this section does not show.
int packSlot(const char* dir) {
    if (dir == nullptr) return -1;
    if (strcmp(dir, "slo") == 0) return 0;
    if (strcmp(dir, "slo16") == 0) return 1;
    if (strcmp(dir, "slo17") == 0) return 2;
    return -1;
}
}  // namespace

uint8_t mapStartPosition() {
    loadMapSettings();
    return g_start_position;
}

bool setMapStartPosition(uint8_t value) {
    loadMapSettings();
    if (value != om::map::kStartLjubljana && value != om::map::kStartLast) return false;
    if (!writeU8("mapstart", value)) return false;
    g_start_position = value;
    ESP_LOGI(TAG, "map settings: start position -> %s", om::map::startPositionLabel(value));
    return true;
}

uint8_t mapStartZoom() {
    loadMapSettings();
    return g_start_zoom;
}

bool setMapStartZoom(uint8_t zoom) {
    loadMapSettings();
    if (zoom < om::map::kStartZoomMin || zoom > om::map::kStartZoomMax) return false;
    if (!writeU8("mapzoom", zoom)) return false;
    g_start_zoom = zoom;
    ESP_LOGI(TAG, "map settings: start zoom -> z%u", (unsigned)zoom);
    return true;
}

bool loadLastView(om::map::SavedView* out) {
    if (out == nullptr) return false;
    if (!g_last_view_loaded) {
        g_last_view_loaded = true;
        nvs_handle_t h;
        if (nvs_open("mcset", NVS_READONLY, &h) == ESP_OK) {
            uint8_t z  = 0;
            int32_t wx = 0;
            int32_t wy = 0;
            if (nvs_get_u8(h, "lastz", &z) == ESP_OK && nvs_get_i32(h, "lastwx", &wx) == ESP_OK &&
                nvs_get_i32(h, "lastwy", &wy) == ESP_OK && z >= om::map::kSavedZoomMin &&
                z <= om::map::kSavedZoomMax) {
                g_last_view.valid = true;
                g_last_view.z     = z;
                g_last_view.wx    = wx;
                g_last_view.wy    = wy;
            }
            nvs_close(h);
        }
    }
    *out = g_last_view;
    return g_last_view.valid;
}

bool saveLastView(const om::map::SavedView& view) {
    if (!view.valid) return false;
    nvs_handle_t h;
    if (nvs_open("mcset", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t r = nvs_set_u8(h, "lastz", view.z);
    if (r == ESP_OK) r = nvs_set_i32(h, "lastwx", view.wx);
    if (r == ESP_OK) r = nvs_set_i32(h, "lastwy", view.wy);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "map settings: last view write failed (%s)", esp_err_to_name(r));
        return false;
    }
    g_last_view        = view;
    g_last_view_loaded = true;
    ESP_LOGI(TAG, "map settings: last view saved (z%u, %ld,%ld)", (unsigned)view.z, (long)view.wx,
             (long)view.wy);
    return true;
}

bool packStatus(const char* dir, PackStatus* out) {
    if (out == nullptr) return false;
    const int slot = packSlot(dir);
    if (slot < 0) return false;
    static PackStatus cache[3];
    static bool       cached[3] = {false, false, false};
    if (!cached[slot]) {
        cached[slot] = true;
        PackStatus st;
        char       path[80];
        FILINFO    info;
        for (char d = '0'; d <= '9'; d++) {
            snprintf(path, sizeof(path), "%c:/maps/%s/tiles.pack", d, dir);
            if (f_stat(path, &info) == FR_OK && info.fsize > 0) {
                st.present = true;
                st.bytes   = (uint64_t)info.fsize;
                break;
            }
        }
        if (st.present) {
            // The index stays on stdio (the same path the map opens): its size / one record is the count,
            // with no scan and without touching the map's own open handles.
            snprintf(path, sizeof(path), "/sdcard/maps/%s/tiles.idx", dir);
            FILE* idx = fopen(path, "rb");
            if (idx != nullptr) {
                if (fseek(idx, 0, SEEK_END) == 0) {
                    const long size = ftell(idx);
                    if (size > 0) st.tiles = (uint32_t)((size_t)size / om::map::kIdxRecordBytes);
                }
                fclose(idx);
            }
        }
        cache[slot] = st;
        ESP_LOGI(TAG, "map settings: pack %s %s, %u tiles, %llu B", dir, st.present ? "present" : "absent",
                 (unsigned)st.tiles, (unsigned long long)st.bytes);
    }
    *out = cache[slot];
    return true;
}

}  // namespace maplink
}  // namespace om
