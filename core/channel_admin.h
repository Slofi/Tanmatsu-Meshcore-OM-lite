// SPDX-License-Identifier: MIT
// Channel administration (S514/EST-446): the rules behind adding, renaming and removing a channel from
// the device, as plain C++ so the host tests drive the same code the panel runs on the glass.
//
// Ported from the L2, not re-invented:
//   · hashtag normalisation: firmware/src/channels.cpp:424-441 (mcChannelAddHashtag) — one '#', no
//     spaces, A-Z lowercased, at most 31 bytes including the '#', and a bare '#' is refused. The NAME of
//     a hashtag channel is the normalised tag itself; the key is sha256(tag)[0..15].
//   · base64: firmware/src/channels.cpp:76-85 + 388-410 (b64Value/mcChannelAddFromB64) — spaces and tabs
//     are skipped, '=' ends the text, any other non-base64 byte is refused, and the key must decode to
//     exactly 16 bytes. ONE deliberate tightening: more than 16 decoded bytes is refused, where the L2
//     silently ignores the extra bytes; a longer string is far more likely a pasted private key than a
//     channel key.
//   · duplicate rule: firmware/src/channels.cpp:328-351 (mcChannelAdd) — the same key twice returns the
//     existing slot instead of adding a second receiver for the same traffic; here it is REFUSED with a
//     notice, because a Settings action that appears to add nothing is worse than a visible refusal.
//   · the store: NVS namespace "mcch", `cnt`, `n<i>`, `k<i>` (this app already loads it in
//     main/mc_link.cpp) and `s<hh>` per-channel scope by the LIVE 1-byte hash (S513/EST-440).
//
// The rewrite ORDER is the ticket's safety rule: new entries first, `cnt` last, cleanup after. A power cut
// between any two steps must leave a table that loads (the old one before `cnt`, the new one after it),
// never a counted slot without a key. The host test cuts the plan after EVERY step and loads the result.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_channels.h"

namespace meshcore {

// The L2's tag buffer: 31 bytes + NUL, so 30 characters after the '#'.
constexpr size_t kChannelTagMax = 31;
// McChannel::name[32] on the L2; our NVS loads 31 bytes + NUL.
constexpr size_t kChannelLabelMax = 31;

// Normalise a typed "#Slovenija" / "slovenia" / " # SLO venija " into the ONE tag MeshCore hashes.
// Writes an always-NUL-terminated string to `out` and returns its length (>= 2), or -1 when there is
// nothing but a '#' (or the result does not fit). Nothing is written on a refusal.
int channelTagCanonical(const char* typed, char* out, size_t cap);

// sha256(tag)[0..15]. False for an empty/NULL tag.
bool channelTagKey(const char* tag, uint8_t out[kKeyBytes]);

// 16 bytes -> 24 base64 characters + NUL. False when cap < 25.
bool channelKeyToBase64(const uint8_t key[kKeyBytes], char* out, size_t cap);

// The L2's decoder. False on a bad character, a short key, a key longer than 16 bytes, or a NULL text.
bool channelKeyFromBase64(const char* text, uint8_t out[kKeyBytes]);

// A local label: 1..31 bytes of printable ASCII, no leading/trailing space. (The L2 only refuses an
// empty name; the editor's own rules keep it printable, and this is the check a pasted/restored value
// needs.)
bool channelLabelValid(const char* name);

// The same key already in the table? (Duplicate refusal.)
bool channelKeyKnown(const ChannelSet& set, const uint8_t key[kKeyBytes]);

// The Settings row's right-hand text, the ticket's exact words: "default" / "no scope" / "scope #si".
// A stored region that is no longer legal reads as "default", exactly as scopePlanFor resolves it.
void channelScopeRowText(const char* stored, char* out, size_t cap);

// ------------------------------------------------------------------------------------- the safe rewrite
// One NVS write in the order the store must accept it. `index` is the mcch slot; `hash` is the LIVE
// 1-byte channel hash for EraseScope.
enum class ChannelNvsOp : uint8_t {
    None = 0,
    SetKey,      // k<index> <- after[index].secret (first 16 bytes)
    SetName,     // n<index> <- after[index].name
    SetCount,    // cnt     <- after.count (LAST table write)
    EraseKey,    // k<index> gone (slot beyond the new count)
    EraseName,   // n<index> gone
    EraseScope,  // s<hh> gone (hash no remaining channel carries)
};

struct ChannelNvsStep {
    ChannelNvsOp op = ChannelNvsOp::None;
    uint8_t index = 0;
    uint8_t hash = 0;
};

constexpr int kChannelNvsMaxSteps = 8 * (int)kMaxChannels;

// Plan the NVS writes that turn `before` into `after`.
//
// Order, and why:
//   1. for every slot whose key changed (including every NEW slot): SetKey, then SetName. A cut here
//      leaves the old `cnt`, which still counts only OLD slots, so the table loads as it was.
//   2. SetCount. After this the new table is the one that loads; the slots it no longer counts are
//      stale but unreachable.
//   3. Erase the tail slots' key and name (housekeeping; `cnt` already hides them).
//   4. Erase a removed channel's s<hh> ONLY when no channel in `after` still has that 1-byte hash.
// A removal compacts FROM the removed index upward, so after the last compacted slot the new table is
// already live while `cnt` still counts it; the stale tail is then hidden by the new `cnt`.
int channelRewritePlan(const ChannelSet& before, const ChannelSet& after, ChannelNvsStep* out, int cap);

}  // namespace meshcore
