// SPDX-License-Identifier: MIT
// The channel administration rules' one implementation (see channel_admin.h for the L2 sources).
#include "channel_admin.h"

#include <stdio.h>
#include <string.h>

#include "channel_scope.h"
#include "mc_crypto.h"

namespace meshcore {
namespace {

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

int channelTagCanonical(const char* typed, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return -1;
    out[0] = 0;
    if (typed == nullptr) return -1;

    const char* p = typed;
    while (*p == ' ') ++p;
    if (*p == '#') ++p;   // one leading '#' is the expected form, not a character to keep twice
    while (*p == ' ') ++p;

    char tag[kChannelTagMax + 1];
    size_t n = 0;
    tag[n++] = '#';       // the tag MeshCore hashes always starts with exactly one '#'
    for (; *p != 0; ++p) {
        char c = *p;
        if (c == '#' || c == ' ') continue;             // normalise: one '#', no spaces
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);   // the key is derived from the LOWERCASED tag
        if (n + 1 > kChannelTagMax) return -1;          // the L2's tag[32] cap, refused not truncated
        tag[n++] = c;
    }
    tag[n] = 0;
    if (n < 2) return -1;   // a bare '#' is not a channel
    if (n + 1 > cap) return -1;
    memcpy(out, tag, n + 1);
    return (int)n;
}

bool channelTagKey(const char* tag, uint8_t out[kKeyBytes]) {
    if (tag == nullptr || out == nullptr || tag[0] == 0) return false;
    uint8_t full[32];
    if (!sha256((const uint8_t*)tag, strlen(tag), full)) return false;
    memcpy(out, full, kKeyBytes);
    return true;
}

bool channelKeyToBase64(const uint8_t key[kKeyBytes], char* out, size_t cap) {
    if (out == nullptr || cap < 25) {
        if (out != nullptr && cap > 0) out[0] = 0;
        return false;
    }
    if (key == nullptr) return false;
    size_t o = 0;
    for (int i = 0; i < 15; i += 3) {   // five whole groups = 15 bytes, then the last byte with padding
        const uint32_t v = ((uint32_t)key[i] << 16) | ((uint32_t)key[i + 1] << 8) | key[i + 2];
        out[o++] = kB64[(v >> 18) & 63];
        out[o++] = kB64[(v >> 12) & 63];
        out[o++] = kB64[(v >> 6) & 63];
        out[o++] = kB64[v & 63];
    }
    const uint32_t v = ((uint32_t)key[15] << 16);
    out[o++] = kB64[(v >> 18) & 63];
    out[o++] = kB64[(v >> 12) & 63];
    out[o++] = '=';
    out[o++] = '=';
    out[o] = 0;
    return true;
}

bool channelKeyFromBase64(const char* text, uint8_t out[kKeyBytes]) {
    if (text == nullptr || out == nullptr) return false;
    uint8_t secret[kKeyBytes] = {0};
    int     bits = 0, total = 0, o = 0;
    for (const char* p = text; *p != 0; ++p) {
        if (*p == '=') break;
        const int v = b64Value(*p);
        if (v < 0) {
            if (*p == ' ' || *p == '\t') continue;   // the L2 skips pasted air
            return false;
        }
        bits = (bits << 6) | v;
        total += 6;
        if (total >= 8) {
            total -= 8;
            if (o >= (int)kKeyBytes) return false;   // more than 16 bytes: refused, never truncated
            secret[o++] = (uint8_t)((bits >> total) & 0xFF);
        }
    }
    if (o != (int)kKeyBytes) return false;   // only 16-byte (128-bit) channel keys exist here
    memcpy(out, secret, kKeyBytes);
    return true;
}

bool channelLabelValid(const char* name) {
    if (name == nullptr) return false;
    size_t n = 0;
    while (name[n] != 0) {
        const unsigned char u = (unsigned char)name[n];
        if (u < 0x20 || u > 0x7E) return false;
        ++n;
    }
    if (n == 0 || n > kChannelLabelMax) return false;
    if (name[0] == ' ' || name[n - 1] == ' ') return false;
    return true;
}

bool channelKeyKnown(const ChannelSet& set, const uint8_t key[kKeyBytes]) {
    if (key == nullptr) return false;
    return channelsFindByKey(set, key) >= 0;
}

void channelScopeRowText(const char* stored, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    switch (scopeModeFor(stored)) {
        case ScopeMode::None:
            snprintf(out, cap, "no scope");
            break;
        case ScopeMode::Region: {
            char canon[kScopeNameMax + 1];
            if (scopeNameCanonical(stored, canon, (int)sizeof(canon)) > 0)
                snprintf(out, cap, "scope #%s", canon);
            else
                snprintf(out, cap, "default");
            break;
        }
        default:
            snprintf(out, cap, "default");
            break;
    }
}

int channelRewritePlan(const ChannelSet& before, const ChannelSet& after, ChannelNvsStep* out, int cap) {
    if (out == nullptr || cap <= 0) return 0;
    int n = 0;
    auto put = [&](ChannelNvsOp op, int index, int hash) {
        if (n >= cap) return;
        out[n].op    = op;
        out[n].index = (uint8_t)index;
        out[n].hash  = (uint8_t)hash;
        ++n;
    };

    // 1. every slot that is new or whose key changed: KEY first, then the name. A cut in the middle of
    //    this loop leaves the OLD count, which still counts only the old slots.
    for (size_t i = 0; i < after.count; ++i) {
        const bool new_slot = i >= before.count;
        const bool key_diff = new_slot || memcmp(before.ch[i].secret, after.ch[i].secret, kKeyBytes) != 0;
        if (key_diff) put(ChannelNvsOp::SetKey, (int)i, 0);
        const bool name_diff = new_slot || key_diff || strcmp(before.ch[i].name, after.ch[i].name) != 0;
        if (name_diff) put(ChannelNvsOp::SetName, (int)i, 0);
    }

    // 2. the commit point: from here the NEW table is what loads.
    if (after.count != before.count) put(ChannelNvsOp::SetCount, 0, 0);

    // 3. housekeeping AFTER the commit: the tail slots the new count no longer reaches.
    if (after.count < before.count) {
        for (size_t i = after.count; i < before.count; ++i) {
            put(ChannelNvsOp::EraseKey, (int)i, 0);
            put(ChannelNvsOp::EraseName, (int)i, 0);
        }
    }

    // 4. a removed channel's scope record goes only when no SURVIVING channel carries the same 1-byte
    //    hash (the key of s<hh> is the hash, not the slot).
    for (size_t i = 0; i < before.count; ++i) {
        const uint8_t h = before.ch[i].hash;
        bool survives = false;
        for (size_t j = 0; j < after.count; ++j) {
            if (after.ch[j].hash == h) {
                survives = true;
                break;
            }
        }
        if (survives) continue;
        bool already = false;   // two removed slots can carry one hash: erase its record once
        for (int k = 0; k < n; ++k) {
            if (out[k].op == ChannelNvsOp::EraseScope && out[k].hash == h) {
                already = true;
                break;
            }
        }
        if (!already) put(ChannelNvsOp::EraseScope, 0, (int)h);
    }
    return n;
}

}  // namespace meshcore
