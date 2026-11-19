// SPDX-License-Identifier: MIT
// The chat "time row" (S515/EST-462): what the head line above one message can HONESTLY say about the way the
// frame travelled — hops, the last repeater, its size in bytes, and how many copies were heard.
//
// Everything here is pure and host-tested, for two reasons the ticket gives:
//   · the last-repeater resolver must never guess between two contacts whose keys share a prefix, so the rule
//     has to be pinned by a test, not by the glass;
//   · EST-463 will extend the resolver to the SD node archive — the same function, one more source of keys.
//
// What is NOT here: RSSI and SNR. The C6's numbers are unusable (docs/DEVICE.md § 5b), so they are never shown.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"

namespace om {
namespace mc {

// The worst honest line: "65535 hops via <24-byte name> · 65535 B · repeated 65535×" is far below this.
constexpr size_t kChatMetaCap = 96;

// The one line above a message, as the formatter sees it. Absent means UNKNOWN and is never invented:
// `bytes` and `copies` are live-radio facts the L2 record does not carry, and `hops_known` is false for a
// record written before this build (its hops/pathCount were fake).
struct MsgMeta {
    bool     outgoing   = false;
    bool     hops_known = false;
    uint8_t  hops       = 0;      // 0 = direct
    char     via[40]    = "";     // the resolved last repeater's label (a name or its hex)
    uint16_t bytes      = 0;      // frame length on the air; 0 = unknown (a restored message)
    uint16_t copies     = 0;      // incoming copies heard / echoes of our own frame heard back
};

// The last path hash -> a label. Its contact name when EXACTLY ONE full-key contact's key starts with those
// bytes; otherwise the lowercase hex of the hash itself ("86a8"). Two matches is not a near miss: it names
// nobody, because picking one would be a guess. Returns true when the label is a contact's name.
bool pathHopLabel(const ContactStore& s, const uint8_t* hash, uint8_t len, char* out, size_t cap);

// The dim text drawn after the sender (incoming) or left of "you" (our own), WITHOUT a leading separator:
//   incoming: "direct" / "3 hops via TEST-RPT-A" [+ " · 87 B"] [+ " · heard 2×"]
//   our own:  "87 B" [+ " · repeated 2×"]           (the delivery word stays after the time)
// "heard N×" and "repeated N×" appear only from two copies up (the ticket's test list); one copy is not worth
// the ink. An unknown field contributes nothing at all — a restored record cannot show a byte count.
void chatMetaText(const MsgMeta& m, char* out, size_t cap);

// ---- the head line's pixel arithmetic, shared by the page and the host test ------------------------------
// The meta must never reach the age column on the right of an incoming line, and never overlap "you" on one of
// ours. These are the only places the gap exists.
constexpr int kChatMetaGap = 10;

// Incoming: the meta starts after the sender's own width.
constexpr int metaIncomingX(int sender_x, int sender_width) {
    return sender_x + (sender_width > 0 ? sender_width : 0) + kChatMetaGap;
}
// Incoming: how much room is left before the age column (0 = nowhere honest to draw it).
constexpr int metaIncomingMaxW(int sender_x, int sender_width, int age_x) {
    const int x = metaIncomingX(sender_x, sender_width);
    return age_x > x + kChatMetaGap ? age_x - kChatMetaGap - x : 0;
}
// Our own: the meta is right-aligned to end just left of "you".
constexpr int metaOutgoingRight(int body_right, int you_width) {
    return body_right - (you_width > 0 ? you_width : 0) - kChatMetaGap;
}
constexpr int metaOutgoingMaxW(int body_x, int body_right, int you_width) {
    const int right = metaOutgoingRight(body_right, you_width);
    return right > body_x ? right - body_x : 0;
}

}  // namespace mc
}  // namespace om
