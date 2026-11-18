// SPDX-License-Identifier: MIT
// Per-channel flood scope, what MeshCore calls a Region / scope (S513/EST-440).
//
// Ported from the L2, not re-invented: firmware/src/ui/channel_scope.h/.cpp (S509), with every rule
// citing the L2 line it comes from. The L2's own header states what a scope IS; this is the Tanmatsu
// copy of the same table, because the Tanmatsu sends every channel text unscoped today.
//
//   · modes: Default (follow the node default) / No scope / Region ('#tag'). Stored value:
//     "" = Default, "!none" = No scope (OverMesh's sentinel), anything else = the region name.
//     (L2 ui/channel_scope.h:23-47)
//   · name rules: drop leading spaces and one '#', drop trailing spaces, refuse a leading '$',
//     1..30 chars through MeshCore's RegionMap::is_name_char. Case matters: "#si" != "#SI".
//     (L2 ui/channel_scope.cpp:5-30; L2 rptr_model.h:64-75 = MeshCore RegionMap::is_name_char)
//   · resolution: channel -> node default -> unscoped (L2 ui/channel_scope.cpp:32-63).
//   · region key: sha256("#name") first 16 bytes (MeshCore helpers/TransportKeyStore.cpp:40-55
//     getAutoKeyFor, called with the '#name' by helpers/RegionMap.cpp:182-203).
//   · transport code: HMAC-SHA256(key, payload type || payload)'s first 2 bytes, read little-endian
//     as finalizeHMAC(..., &code, 2) does, with 0x0000/0xFFFF reserved
//     (helpers/TransportKeyStore.cpp:4-17).
//   · frame: ROUTE_TYPE_TRANSPORT_FLOOD (0x00), TWO 16-bit codes, then path_len, path, payload
//     (Packet.h:14, Mesh.cpp:664-677, Packet.cpp:37-51). Codes are {this region, 0}, as the L2's
//     sendChannelText() and upstream sendFloodScoped() put them.
//
// Plain C++, no IDF, no Arduino: the host tests drive this exact file.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_wire.h"

namespace meshcore {

// The stored value that means "No scope" (L2 ui/channel_scope.h:43).
constexpr const char* kScopeNone = "!none";
// RegionEntry::name[31], so 30 characters plus the terminator (L2 rptr_model.h:28).
constexpr int kScopeNameMax = 30;

enum class ScopeMode : uint8_t { Default = 0, None, Region };

// "" = Default, "!none" = No scope, anything else = Region. constexpr so a host test can pin it.
constexpr bool scopeIsNone(const char* stored) {
    if (stored == nullptr) return false;
    const char* none = kScopeNone;
    int i = 0;
    for (; none[i] != 0; ++i) {
        if (stored[i] != none[i]) return false;
    }
    return stored[i] == 0;
}

constexpr ScopeMode scopeModeFor(const char* stored) {
    if (stored == nullptr || stored[0] == 0) return ScopeMode::Default;
    if (scopeIsNone(stored)) return ScopeMode::None;
    return ScopeMode::Region;
}

// MeshCore's own region-name character rule (RegionMap::is_name_char), the same one the L2 uses:
// '-', '$', '#', digits, or any byte at or above 'A' (L2 rptr_model.h:68-75).
constexpr bool scopeNameChar(char c) {
    const unsigned char u = (unsigned char)c;
    return u == '-' || u == '$' || u == '#' || (u >= '0' && u <= '9') || u >= 'A';
}

// The canonical stored form of a typed name, or -1 when it is not a region name. Nothing is written
// on a refusal, so a half-copied name can never look like a shorter region.
int scopeNameCanonical(const char* typed, char* out, int cap);

// What one send resolves to. `region` is the name WITHOUT the leading '#'; "" = unscoped.
// `inheritsDefault` says the name came from the node default rather than from the channel.
struct ScopePlan {
    ScopeMode mode = ScopeMode::Default;
    char      region[kScopeNameMax + 1] = "";
    bool      inheritsDefault = false;
};

// Resolution order: channel setting -> node default -> unscoped.
ScopePlan scopePlanFor(const char* stored, const char* nodeDefault);

// The row texts, ported so Settings later needs no second rule: "default" / "none" / "#si", and
// what Default resolves to right now ("#si" or "unscoped").
void scopeSettingText(const char* stored, char* out, int cap);
void scopeDefaultText(const char* nodeDefault, char* out, int cap);
void scopeRegionText(const char* region, char* out, int cap);

// The open channel's head text, the ticket's exact words: "scope #si" / "no scope", or "" when the
// plan is unscoped (a Default with no node default, i.e. the state today).
void scopeHeadText(const ScopePlan& plan, char* out, int cap);

// A region's MeshCore key: sha256("#name")[0..15]. False for a refused/empty name.
bool scopeRegionKey(const char* region, uint8_t out[16]);

// The 2-byte code MeshCore puts in transport_codes[0]: HMAC-SHA256(key, type || payload)[0..1]
// little-endian, with 0x0000 -> 0x0001 and 0xFFFF -> 0xFFFE.
bool scopeTransportCode(const uint8_t key[16], uint8_t payload_type, const uint8_t* payload, size_t len,
                        uint16_t* out);

// Build a complete channel-text frame. A plan with no region builds the ordinary flood through
// buildGroupText (route 1, no codes); a region builds ROUTE_TYPE_TRANSPORT_FLOOD with both codes.
// `default_key` (16 bytes) is used only when plan.inheritsDefault is true; null means derive the
// key from the public name. Returns the frame length, or 0 when it cannot fit.
size_t buildScopedChannelText(const Channel& ch, uint32_t timestamp, const char* sender, const char* text,
                              uint8_t hash_size, const ScopePlan& plan, const uint8_t* default_key,
                              uint8_t* out, size_t out_size);

}  // namespace meshcore
