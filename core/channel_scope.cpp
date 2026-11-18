// SPDX-License-Identifier: MIT
// The Region / scope model's one implementation for the Tanmatsu (see channel_scope.h for the sources).
#include "channel_scope.h"

#include <stdio.h>
#include <string.h>

#include "mc_crypto.h"

namespace meshcore {

int scopeNameCanonical(const char* typed, char* out, int cap) {
    if (out == nullptr || cap <= 0) return -1;
    out[0] = 0;
    if (typed == nullptr) return -1;

    const char* p = typed;
    while (*p == ' ') ++p;   // a pasted name carries air; the keyboard itself cannot type it
    if (*p == '#') ++p;      // '#si' and 'si' are one region: MeshCore hashes '#name' either way
    while (*p == ' ') ++p;
    if (*p == '$') return -1;   // a private region's key cannot be derived from its name
    if (*p == 0) return -1;

    // The name ends at its last non-space character, so trailing air is trimmed, not refused.
    const char* end = p + strlen(p);
    while (end > p && end[-1] == ' ') --end;

    // Validate the WHOLE name before writing a byte (L2 ui/channel_scope.cpp:22-29).
    const int n = (int)(end - p);
    if (n <= 0 || n > kScopeNameMax || n + 1 > cap) return -1;
    for (int i = 0; i < n; ++i) {
        if (!scopeNameChar(p[i])) return -1;
    }
    memcpy(out, p, (size_t)n);
    out[n] = 0;
    return n;
}

ScopePlan scopePlanFor(const char* stored, const char* nodeDefault) {
    ScopePlan p;
    p.mode = scopeModeFor(stored);

    if (p.mode == ScopeMode::Region) {
        // A stored value that is no longer legal falls back to Default; it is never truncated into a
        // DIFFERENT region (L2 ui/channel_scope.cpp:38-46).
        if (scopeNameCanonical(stored, p.region, (int)sizeof(p.region)) > 0) return p;
        p.region[0] = 0;
        p.mode      = ScopeMode::Default;
    }
    if (p.mode == ScopeMode::None) return p;

    p.mode            = ScopeMode::Default;
    p.inheritsDefault = true;
    if (nodeDefault != nullptr && nodeDefault[0] != 0) {
        char canon[kScopeNameMax + 1];
        if (scopeNameCanonical(nodeDefault, canon, (int)sizeof(canon)) > 0) {
            snprintf(p.region, sizeof(p.region), "%s", canon);
        }
    }
    return p;
}

void scopeRegionText(const char* region, char* out, int cap) {
    if (out == nullptr || cap <= 0) return;
    if (region == nullptr || region[0] == 0) {
        snprintf(out, (size_t)cap, "not set");
        return;
    }
    snprintf(out, (size_t)cap, "#%s", region[0] == '#' ? region + 1 : region);
}

void scopeSettingText(const char* stored, char* out, int cap) {
    if (out == nullptr || cap <= 0) return;
    switch (scopeModeFor(stored)) {
        case ScopeMode::None: snprintf(out, (size_t)cap, "none"); break;
        case ScopeMode::Region: scopeRegionText(stored, out, cap); break;
        default: snprintf(out, (size_t)cap, "default"); break;
    }
}

void scopeDefaultText(const char* nodeDefault, char* out, int cap) {
    if (out == nullptr || cap <= 0) return;
    if (nodeDefault != nullptr && nodeDefault[0] != 0) scopeRegionText(nodeDefault, out, cap);
    else snprintf(out, (size_t)cap, "unscoped");
}

void scopeHeadText(const ScopePlan& plan, char* out, int cap) {
    if (out == nullptr || cap <= 0) return;
    if (plan.region[0] != 0) {
        snprintf(out, (size_t)cap, "scope #%s", plan.region[0] == '#' ? plan.region + 1 : plan.region);
    } else if (plan.mode == ScopeMode::None) {
        snprintf(out, (size_t)cap, "no scope");
    } else {
        out[0] = 0;   // Default with no node default: unscoped, nothing to say
    }
}

bool scopeRegionKey(const char* region, uint8_t out[16]) {
    if (region == nullptr || out == nullptr) return false;
    char canon[kScopeNameMax + 1];
    if (scopeNameCanonical(region, canon, (int)sizeof(canon)) <= 0) return false;
    char tag[kScopeNameMax + 2];
    snprintf(tag, sizeof(tag), "#%s", canon);
    uint8_t full[32];
    if (!sha256((const uint8_t*)tag, strlen(tag), full)) return false;
    memcpy(out, full, 16);
    return true;
}

bool scopeTransportCode(const uint8_t key[16], uint8_t payload_type, const uint8_t* payload, size_t len,
                        uint16_t* out) {
    if (key == nullptr || out == nullptr) return false;
    if (len > 0 && payload == nullptr) return false;

    // The HMAC input is the payload TYPE byte then the payload (TransportKeyStore.cpp:7-9). The
    // output is read little-endian, exactly as finalizeHMAC(..., &code, 2) lays it out in memory.
    if (len > kMaxPayload) return false;
    uint8_t mac[32];
    uint8_t buf[1 + kMaxPayload];
    buf[0] = payload_type;
    if (len > 0) memcpy(&buf[1], payload, len);
    if (!hmacSha256(key, 16, buf, 1 + len, mac)) return false;
    uint16_t code = (uint16_t)((uint16_t)mac[0] | ((uint16_t)mac[1] << 8));
    if (code == 0) {
        code = 1;   // reserve 0x0000
    } else if (code == 0xFFFF) {
        code = 0xFFFE;   // reserve 0xFFFF
    }
    *out = code;
    return true;
}

size_t buildScopedChannelText(const Channel& ch, uint32_t timestamp, const char* sender, const char* text,
                              uint8_t hash_size, const ScopePlan& plan, const uint8_t* default_key,
                              uint8_t* out, size_t out_size) {
    if (hash_size < 1 || hash_size > 3) return 0;
    if (out == nullptr) return 0;

    uint8_t key[16] = {0};
    bool    scoped  = false;
    if (plan.region[0] != 0) {
        if (plan.inheritsDefault && default_key != nullptr) {
            memcpy(key, default_key, sizeof(key));
        } else if (!scopeRegionKey(plan.region, key)) {
            return 0;
        }
        bool any = false;
        for (size_t i = 0; i < sizeof(key); ++i) any = any || key[i] != 0;
        scoped = any;   // a null key sends unscoped, as the L2 does
    }
    if (!scoped) return buildGroupText(ch, timestamp, sender, text, hash_size, out, out_size);

    uint8_t plain[kMaxPlain];
    const size_t plain_len = buildPlain(plain, sizeof(plain), timestamp, sender, text);
    if (plain_len == 0) return 0;

    uint8_t payload[kMaxPayload];
    const size_t sealed = sealPayload(ch.secret, &payload[1], sizeof(payload) - 1, plain, plain_len);
    if (sealed == 0) return 0;
    payload[0] = ch.hash;   // createGroupDatagram: channel hash then MAC+ciphertext
    const size_t payload_len = 1 + sealed;

    uint16_t code = 0;
    if (!scopeTransportCode(key, kTypeGrpTxt, payload, payload_len, &code)) return 0;

    // Packet::writeTo: header, BOTH transport codes, path_len, path, payload.
    const size_t need = 1 + 4 + 1 + payload_len;
    if (out_size < need) return 0;
    out[0] = (uint8_t)((kTypeGrpTxt << 2) | kRouteTransportFlood);
    out[1] = (uint8_t)(code & 0xFF);
    out[2] = (uint8_t)(code >> 8);
    out[3] = 0;   // codes[1] = 0: we carry no home region (L2 mesh_link.cpp sendChannelText)
    out[4] = 0;
    out[5] = (uint8_t)((hash_size - 1) << 6);   // flood starts with count 0
    memcpy(&out[6], payload, payload_len);
    return need;
}

}  // namespace meshcore
