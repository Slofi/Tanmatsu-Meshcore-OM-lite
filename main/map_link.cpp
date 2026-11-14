// SPDX-License-Identifier: MIT
#include "map_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "jpeg_decoder.h"   // espressif/esp_jpeg
#include "map_pack.h"
#include "map_view.h"

namespace om {
namespace maplink {

const char* TAG = "tm/map";

namespace {

constexpr int kTileSide = om::map::kTilePx;

// The pack currently open. One at a time: zooming across a band boundary closes the old pair and opens the new
// one, which keeps the mount's open-file count at two instead of six.
struct OpenPack {
    int   z = -1;
    FILE* idx = nullptr;
    FILE* pack = nullptr;
    uint32_t count = 0;
};

OpenPack  g_pack;
om::map::TileCache g_cache;
bool      g_ready = false;
uint32_t  g_hits = 0, g_misses = 0;   // since the last takeStats(), for one page compose

// 8 decoded tiles, 256x256 RGB565: 1 MB in PSRAM, the same budget the L2 gives its cache.
uint16_t* g_pixels[om::map::kCacheSlots] = {nullptr};

bool readRecord(void* ctx, uint32_t i, om::map::Entry* out) {
    FILE* f = static_cast<FILE*>(ctx);
    if (f == nullptr) return false;
    if (fseek(f, (long)i * (long)om::map::kIdxRecordBytes, SEEK_SET) != 0) return false;
    uint8_t rec[om::map::kIdxRecordBytes];
    if (fread(rec, 1, sizeof(rec), f) != sizeof(rec)) return false;
    return om::map::decodeEntry(rec, out);
}

void closePack() {
    if (g_pack.idx) fclose(g_pack.idx);
    if (g_pack.pack) fclose(g_pack.pack);
    g_pack = OpenPack{};
}

// Open the pack that serves `z`, reusing it when it is already the open one.
bool ensurePack(int z) {
    if (g_pack.z == z && g_pack.idx != nullptr) return true;
    closePack();
    const char* dir = om::map::packDirForZoom(z);
    if (dir == nullptr) {
        ESP_LOGW(TAG, "z%d: no pack covers this zoom", z);
        return false;
    }
    char idxPath[72], packPath[72];
    snprintf(idxPath, sizeof(idxPath), "/sdcard/maps/%s/tiles.idx", dir);
    snprintf(packPath, sizeof(packPath), "/sdcard/maps/%s/tiles.pack", dir);
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
    FILE* pf = fopen(packPath, "rb");
    if (pf == nullptr) {
        ESP_LOGW(TAG, "%s: not on the card", packPath);
        fclose(idx);
        return false;
    }
    g_pack.z     = z;
    g_pack.idx   = idx;
    g_pack.pack  = pf;
    g_pack.count = (uint32_t)(size / (long)om::map::kIdxRecordBytes);
    ESP_LOGI(TAG, "z%d: pack %s open, %u tiles", z, dir, (unsigned)g_pack.count);
    return true;
}

uint16_t* slotPixels(int slot) {
    if (slot < 0 || slot >= om::map::kCacheSlots) return nullptr;
    if (g_pixels[slot] == nullptr) {
        g_pixels[slot] = (uint16_t*)heap_caps_malloc((size_t)kTileSide * kTileSide * 2, MALLOC_CAP_SPIRAM);
        if (g_pixels[slot] == nullptr) {
            ESP_LOGE(TAG, "no PSRAM for the tile cache (slot %d)", slot);
        }
    }
    return g_pixels[slot];
}

}  // namespace

void begin() {
    g_cache.reset();
    g_ready = true;
}

bool ready() { return g_ready; }

void takeStats(uint32_t* hits, uint32_t* misses) {
    if (hits) *hits = g_hits;
    if (misses) *misses = g_misses;
    g_hits = g_misses = 0;
}

void forget() {
    closePack();
    g_cache.reset();
    g_ready = false;
}

const uint16_t* tile(uint8_t z, uint32_t x, uint32_t y, int* out_w, int* out_h) {
    if (!g_ready) return nullptr;
    const int hit = g_cache.find(z, x, y);
    if (hit >= 0) {
        ++g_hits;
        if (out_w) *out_w = kTileSide;
        if (out_h) *out_h = kTileSide;
        return g_pixels[hit];
    }
    if (!ensurePack(z)) return nullptr;

    om::map::Entry e;
    const int      record = om::map::findTile(g_pack.count, readRecord, g_pack.idx, z, x, y);
    if (record < 0 || !readRecord(g_pack.idx, (uint32_t)record, &e)) return nullptr;   // a hole in the pack

    uint8_t* raw = (uint8_t*)heap_caps_malloc(e.length, MALLOC_CAP_SPIRAM);
    if (raw == nullptr) return nullptr;
    if (fseek(g_pack.pack, (long)e.offset, SEEK_SET) != 0 || fread(raw, 1, e.length, g_pack.pack) != e.length) {
        heap_caps_free(raw);
        return nullptr;
    }

    ++g_misses;
    const int       slot = g_cache.take(z, x, y);
    uint16_t* const out  = slotPixels(slot);
    if (out == nullptr) {
        heap_caps_free(raw);
        return nullptr;
    }
    esp_jpeg_image_cfg_t    cfg  = {};
    esp_jpeg_image_output_t info = {};
    cfg.indata      = raw;
    cfg.indata_size = e.length;
    cfg.outbuf      = (uint8_t*)out;
    cfg.outbuf_size = (uint32_t)kTileSide * kTileSide * 2;
    cfg.out_format  = JPEG_IMAGE_FORMAT_RGB565;
    cfg.out_scale   = JPEG_IMAGE_SCALE_0;
    const int64_t   t0  = esp_timer_get_time();
    const esp_err_t err = esp_jpeg_decode(&cfg, &info);
    const long      us  = (long)(esp_timer_get_time() - t0);
    heap_caps_free(raw);
    if (err != ESP_OK) {
        // Do not leave a half-decoded tile in the cache: drop the key so the next look tries again.
        g_cache.key[slot].valid = false;
        ESP_LOGW(TAG, "z%d %u/%u: decode failed (%d)", z, (unsigned)x, (unsigned)y, (int)err);
        return nullptr;
    }
    if (info.width != kTileSide || info.height != kTileSide) {
        ESP_LOGW(TAG, "z%d %u/%u: tile is %ux%u, not %d square", z, (unsigned)x, (unsigned)y, (unsigned)info.width,
                 (unsigned)info.height, kTileSide);
    }
    if (out_w) *out_w = info.width;
    if (out_h) *out_h = info.height;
    ESP_LOGD(TAG, "z%d %u/%u decoded in %ld ms (slot %d)", z, (unsigned)x, (unsigned)y, us / 1000, slot);
    return out;
}

void probe(double lat, double lon) {
    ESP_LOGI(TAG, "probe: %.5f, %.5f", lat, lon);
    for (int z = 16; z >= 15; z--) {
        const uint32_t tx = om::map::tileXForLon(lon, z);
        const uint32_t ty = om::map::tileYForLat(lat, z);
        int            w = 0, h = 0;
        const int64_t  t0  = esp_timer_get_time();
        const uint16_t* px = tile((uint8_t)z, tx, ty, &w, &h);
        const long      us = (long)(esp_timer_get_time() - t0);
        if (px == nullptr) {
            ESP_LOGW(TAG, "z%d %u/%u: not available", z, (unsigned)tx, (unsigned)ty);
            continue;
        }
        ESP_LOGI(TAG, "z%d tile %u/%u -> %dx%d in %ld ms, px0 0x%04X", z, (unsigned)tx, (unsigned)ty, w, h, us / 1000,
                 px[0]);
    }
}

}  // namespace maplink
}  // namespace om
