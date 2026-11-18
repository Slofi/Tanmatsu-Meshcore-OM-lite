// SPDX-License-Identifier: MIT
#include "sd_history.h"

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "mc_history.h"
#include "../../firmware/src/history/history_log.h"   // findWindowStart: the L2's own window rule

namespace om {
namespace sdhistory {
namespace {

const char* TAG = "tm/hist";

// The L2 keeps its logs at /omlite/history; ours sit beside the app's own directory so the two never collide on
// a shared card, and the record format is what makes them readable by either device.
constexpr const char* kDir = "/sdcard/om-lite/history";

// How much of a log is read at boot. The ring keeps 64 messages and 64 full-size records are about 13 KB, so
// 32 KB is several times what the ring can hold. It is allocated for the read and freed again rather than parked
// in .bss: internal RAM is the scarce kind on this board, PSRAM is not.
constexpr long kReadBytes = 32 * 1024;

bool g_ready = false;

void pathFor(char* out, size_t cap, uint8_t hash, const char* name) {
    char file[64];
    mc::historyFileName(hash, name, file, sizeof(file));
    snprintf(out, cap, "%s/%s", kDir, file);
}

// S513/EST-434: the L2's DM id, dm-<pubkey[0..3] 8 hex>. The name is the thread's identity on the card; only the
// first four bytes of the key fit in it, which is exactly why a restored thread starts with a prefix-only row.
void pathForDm(char* out, size_t cap, const uint8_t pub[32]) {
    snprintf(out, cap, "%s/dm-%02x%02x%02x%02x.log", kDir, pub[0], pub[1], pub[2], pub[3]);
}

// One record appended: open, write, flush, close. The same rule for a message and for a seen point, so a power
// cut costs at most the record being written, and the L2's torn-tail rule drops that one cleanly on the next read.
bool appendRecord(const char* path, const uint8_t* rec, int n) {
    FILE* f = fopen(path, "ab");
    if (f == nullptr) {
        ESP_LOGW(TAG, "history: cannot open %s", path);
        return false;
    }
    const size_t wrote = fwrite(rec, 1, (size_t)n, f);
    fflush(f);
    fclose(f);
    if (wrote != (size_t)n) {
        ESP_LOGW(TAG, "history: short write to %s (%u of %d)", path, (unsigned)wrote, n);
        return false;
    }
    return true;
}

// The newest kReadBytes of a log, malloc'd. nullptr means "nothing to read" (no file yet, empty file, no memory):
// one answer for all three, because the caller treats them the same way.
uint8_t* readTail(const char* path, long* out_size) {
    *out_size = 0;
    FILE* f   = fopen(path, "rb");
    if (f == nullptr) return nullptr;   // no history for this channel yet: not an error
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return nullptr;
    }
    const long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return nullptr;
    }
    // Only the tail is read: the ring keeps 64 messages and the newest ones are at the end of the file.
    const long take = size > kReadBytes ? kReadBytes : size;
    if (fseek(f, size - take, SEEK_SET) != 0) {
        fclose(f);
        return nullptr;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)take);
    if (buf == nullptr) {
        fclose(f);
        ESP_LOGW(TAG, "history: no memory for a %ld-byte read", (long)take);
        return nullptr;
    }
    const size_t got = fread(buf, 1, (size_t)take, f);
    fclose(f);
    if (got == 0) {
        free(buf);
        return nullptr;
    }
    *out_size = (long)got;
    return buf;
}

// Where the first whole record starts in a window taken from the tail (the L2's own resynchronisation rule).
int windowStart(const char* path, const uint8_t* buf, long size) {
    struct stat st;
    const bool  atEnd = (stat(path, &st) == 0 && (long)st.st_size == size);
    return om::history::findWindowStart(buf, (int)size, atEnd, nullptr);
}

}  // namespace

bool begin() {
    struct stat st;
    if (stat("/sdcard", &st) != 0) {
        ESP_LOGW(TAG, "history: no card mounted, history stays in RAM");
        g_ready = false;
        return false;
    }
    // mkdir on FATFS creates ONE level, so the parent must exist first. It did not on the first install and the
    // whole feature stayed off with a single console warning (S502, found in the v27 log: "cannot create").
    static const char* kDirs[] = {"/sdcard/om-lite", kDir};
    for (const char* dir : kDirs) {
        if (mkdir(dir, 0777) != 0 && stat(dir, &st) != 0) {
            ESP_LOGW(TAG, "history: cannot create %s (%s); history stays in RAM", dir, strerror(errno));
            g_ready = false;
            return false;
        }
    }
    g_ready = true;
    ESP_LOGI(TAG, "history: ON at %s", kDir);
    return true;
}

bool ready() { return g_ready; }

bool append(uint8_t channel, uint8_t hash, const char* name, const mc::Message& m, uint32_t log_id) {
    if (!g_ready) return false;
    uint8_t   rec[512];
    const int n = mc::historyEncode(m, log_id, rec, (int)sizeof(rec));
    if (n <= 0) {
        ESP_LOGW(TAG, "history: record does not fit (%d)", n);
        return false;
    }
    char path[96];
    pathFor(path, sizeof(path), hash, name);
    if (!appendRecord(path, rec, n)) {
        g_ready = false;   // a card that disappeared mid-run: stop trying on every message
        return false;
    }
    (void)channel;   // the file IS the channel
    return true;
}

// The seen point (the format's Meta record): what makes unread survive a restart. Written only when the value
// changed, so a channel left open does not append a record on every two-second refresh.
bool appendSeen(uint8_t channel, uint8_t hash, const char* name, uint32_t seen_log_id, uint16_t unread) {
    if (!g_ready) return false;
    uint8_t   rec[32];
    const int n = mc::historyEncodeMeta(seen_log_id, unread, rec, (int)sizeof(rec));
    if (n <= 0) return false;
    char path[96];
    pathFor(path, sizeof(path), hash, name);
    (void)channel;
    return appendRecord(path, rec, n);
}

size_t load(uint8_t channel, uint8_t hash, const char* name, mc::Message* out, size_t max_out,
            uint32_t* out_max_log_id) {
    if (!g_ready || out == nullptr || max_out == 0) return 0;
    char path[96];
    pathFor(path, sizeof(path), hash, name);

    long     size = 0;
    uint8_t* buf  = readTail(path, &size);
    if (buf == nullptr) return 0;

    const int start = windowStart(path, buf, size);
    if (start < 0) {
        ESP_LOGW(TAG, "history: %s has no readable records in its last %ld bytes", path, (long)size);
        free(buf);
        return 0;
    }
    const size_t n = mc::historyLoad(buf + start, (size_t)size - (size_t)start, channel, out, max_out,
                                     out_max_log_id);
    free(buf);
    ESP_LOGI(TAG, "history: %u message(s) from %s", (unsigned)n, path);
    return n;
}

mc::SeenPoint loadSeen(uint8_t channel, uint8_t hash, const char* name) {
    mc::SeenPoint sp;
    if (!g_ready) return sp;
    char path[96];
    pathFor(path, sizeof(path), hash, name);

    long     size = 0;
    uint8_t* buf  = readTail(path, &size);
    if (buf == nullptr) return sp;

    const int start = windowStart(path, buf, size);
    if (start >= 0) sp = mc::historyReadSeen(buf + start, (size_t)size - (size_t)start);
    free(buf);
    (void)channel;
    return sp;
}

// --------------------------------------------------------------------------------- DM logs (S513/EST-434)
bool appendDm(const uint8_t pub[32], const mc::DmMessage& m, uint32_t log_id) {
    if (!g_ready || pub == nullptr) return false;
    uint8_t   rec[512];
    const int n = mc::historyEncodeDm(m, log_id, rec, (int)sizeof(rec));
    if (n <= 0) {
        ESP_LOGW(TAG, "history: DM record does not fit (%d)", n);
        return false;
    }
    char path[96];
    pathForDm(path, sizeof(path), pub);
    if (!appendRecord(path, rec, n)) {
        g_ready = false;
        return false;
    }
    return true;
}

size_t loadDm(const uint8_t pub[32], const char* peer_name, mc::DmMessage* out, size_t max_out,
              uint32_t* out_max_log_id) {
    if (!g_ready || pub == nullptr || out == nullptr || max_out == 0) return 0;
    char path[96];
    pathForDm(path, sizeof(path), pub);

    long     size = 0;
    uint8_t* buf  = readTail(path, &size);
    if (buf == nullptr) return 0;
    const int start = windowStart(path, buf, size);
    if (start < 0) {
        free(buf);
        return 0;
    }
    const size_t n =
        mc::historyLoadDm(buf + start, (size_t)size - (size_t)start, pub, peer_name, out, max_out, out_max_log_id);
    free(buf);
    ESP_LOGI(TAG, "history: %u DM(s) from %s", (unsigned)n, path);
    return n;
}

bool appendDmSeen(const uint8_t pub[32], uint32_t seen_log_id, uint16_t unread) {
    if (!g_ready || pub == nullptr) return false;
    uint8_t   rec[32];
    const int n = mc::historyEncodeMeta(seen_log_id, unread, rec, (int)sizeof(rec));
    if (n <= 0) return false;
    char path[96];
    pathForDm(path, sizeof(path), pub);
    return appendRecord(path, rec, n);
}

bool appendDmState(const uint8_t pub[32], uint32_t log_id, om::ui::DmState state, uint32_t ack_wall) {
    if (!g_ready || pub == nullptr || log_id == 0) return false;
    uint8_t   rec[64];
    const int n = mc::historyEncodeDmState(log_id, state, ack_wall, rec, (int)sizeof(rec));
    if (n <= 0) return false;
    char path[96];
    pathForDm(path, sizeof(path), pub);
    if (!appendRecord(path, rec, n)) {
        g_ready = false;
        return false;
    }
    return true;
}

mc::SeenPoint loadDmSeen(const uint8_t pub[32]) {
    mc::SeenPoint sp;
    if (!g_ready || pub == nullptr) return sp;
    char path[96];
    pathForDm(path, sizeof(path), pub);
    long     size = 0;
    uint8_t* buf  = readTail(path, &size);
    if (buf == nullptr) return sp;
    const int start = windowStart(path, buf, size);
    if (start >= 0) sp = mc::historyReadSeen(buf + start, (size_t)size - (size_t)start);
    free(buf);
    return sp;
}

size_t restoreDmThreads(mc::DmRing& ring, mc::ContactStore& contacts) {
    if (!g_ready) return 0;
    DIR* d = opendir(kDir);
    if (d == nullptr) {
        ESP_LOGW(TAG, "history: cannot scan %s (%s)", kDir, strerror(errno));
        return 0;
    }
    static mc::DmMessage loaded[mc::kMaxDms];   // one thread at a time; the main task only, before the radio task
    size_t restored = 0;
    struct dirent* e = nullptr;
    while ((e = readdir(d)) != nullptr) {
        const char* name = e->d_name;
        // Exactly the L2's DM id: dm-<8 hex>.log.
        if (strlen(name) != 15 || strncmp(name, "dm-", 3) != 0 || strcmp(name + 11, ".log") != 0) continue;
        unsigned b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        if (sscanf(name + 3, "%2x%2x%2x%2x", &b0, &b1, &b2, &b3) != 4) continue;

        uint8_t pub[32] = {0};   // prefix only: the file name is all the identity the card keeps
        pub[0] = (uint8_t)b0;
        pub[1] = (uint8_t)b1;
        pub[2] = (uint8_t)b2;
        pub[3] = (uint8_t)b3;
        char label[24];
        snprintf(label, sizeof(label), "%.*s", 8, name + 3);

        // The label is the thread's own fallback name; dmRestoreThread() replaces it with the first incoming
        // record's name when there is one, and computes the file's next id itself.
        const size_t n = loadDm(pub, label, loaded, mc::kMaxDms, nullptr);
        if (n == 0) continue;
        // The Meta record names the last READ log id. It is read before the messages are pushed: core/mc_history
        // maps that log id onto the seq dmPush gives the records.
        const mc::SeenPoint sp = loadDmSeen(pub);
        // S513/EST-441: this creates a THREAD, never a contact. Four bytes out of a file name are not a key: v41
        // upserted them, and that invented row was then used for the shared secret and as the only RX candidate —
        // a DM ERA-3 could not open, and a MAC that could not match its real frames. The thread keeps its prefix
        // (named from its own records) until a verified advert rebinds it, or until a full-key row already in the
        // table matches the prefix uniquely.
        mc::RestoredDm r;
        (void)mc::dmRestoreThread(ring, contacts, pub, loaded, n, sp, &r);
        restored++;
        ESP_LOGI(TAG, "history: restored DM thread %s (%u message(s), %s%s)", r.label, (unsigned)r.messages,
                 r.seen_found ? "with a seen point" : "no seen point",
                 r.rebound ? ", bound to a known contact" : ", waiting for its advert");
    }
    closedir(d);
    return restored;
}

}  // namespace sdhistory
}  // namespace om
