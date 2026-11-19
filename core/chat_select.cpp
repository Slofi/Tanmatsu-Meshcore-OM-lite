// SPDX-License-Identifier: MIT
#include "chat_select.h"

#include <stdio.h>
#include <string.h>

#include "chat_meta.h"   // pathHopLabel: a hop's hash -> a name only when exactly one node matches

namespace om {
namespace ui {
namespace {

void setStr(char* out, size_t cap, const char* s) {
    if (out == nullptr || cap == 0) return;
    snprintf(out, cap, "%s", s != nullptr ? s : "");
}

void addRow(ChatMsgRow* out, int max, int* n, ChatMsgAction action, const char* label, const char* value,
            bool dim) {
    if (*n >= max) return;
    ChatMsgRow& r = out[*n];
    r.action      = action;
    setStr(r.label, sizeof(r.label), label != nullptr ? label : "");
    setStr(r.value, sizeof(r.value), value != nullptr ? value : "");
    r.dim         = dim;
    ++*n;
}

}  // namespace

int chatMsgRows(const ChatMsgContext& c, ChatMsgRow* out, int max) {
    if (out == nullptr || max <= 0) return 0;
    int n = 0;

    if (c.kind == ChatMsgKind::Own) {
        // Nothing writes from our own line: there is nobody here to reply to and no peer to open. Info only.
        addRow(out, max, &n, ChatMsgAction::Info, "Info", "", false);
        return n;
    }

    // Reply. A channel reply is the "@Name " mention in the draft; a DM's reply is just the composer, because
    // the thread already says who it is for (no bytes are added to the frame — the L2's own rule).
    addRow(out, max, &n, ChatMsgAction::Reply, "Reply", "", false);

    // The node behind the sender. Only the DM rows exist when the chat is NOT already that thread.
    if (!c.in_dm && c.kind == ChatMsgKind::IncomingChannel) {
        char label[40];
        snprintf(label, sizeof(label), "DM %s", (c.sender != nullptr && c.sender[0] != '\0') ? c.sender : "sender");
        addRow(out, max, &n, ChatMsgAction::DirectMessage, label, c.node_note, c.node_index < 0);
    }

    // Map and Node need the node, and Map also needs a position the map can draw ("no position" otherwise).
    const bool no_node = c.node_index < 0;
    addRow(out, max, &n, ChatMsgAction::Map, "Map",
           no_node ? (c.node_note != nullptr ? c.node_note : "") : (c.node_has_position ? "" : "no position"),
           no_node || !c.node_has_position);
    addRow(out, max, &n, ChatMsgAction::Node, "Node", c.node_note, no_node);

    addRow(out, max, &n, ChatMsgAction::Info, "Info", "", false);
    return n;
}

ChatNodeByName chatNodeByName(const om::mc::ContactStore& s, const char* name) {
    ChatNodeByName m;
    if (name == nullptr || name[0] == '\0' || s.c == nullptr) return m;
    for (size_t i = 0; i < s.count && i < om::mc::kMaxContacts; ++i) {
        const om::mc::Contact& c = s.c[i];
        if (om::mc::contactKeyIncomplete(c.pub)) continue;   // a prefix-only row is not a node
        if (c.name[0] == '\0' || strcmp(c.name, name) != 0) continue;
        ++m.matches;
        m.index = (int)i;
        if (m.matches > 1) break;   // two is already "never guess"
    }
    if (m.matches != 1) m.index = -1;
    return m;
}

const char* chatNodeNote(const ChatNodeByName& m, char* out, size_t cap) {
    if (m.matches == 1) return "";
    if (m.matches == 0) return "unknown node";
    if (out == nullptr || cap == 0) return "";
    snprintf(out, cap, "%d nodes share this name", m.matches);
    return out;
}

int chatSelEnter(int count) { return count > 0 ? count - 1 : -1; }

int chatSelIndexForSeq(const uint32_t* seqs, int count, uint32_t seq) {
    if (seqs == nullptr || count <= 0 || seq == 0) return -1;
    for (int i = 0; i < count; ++i) {
        if (seqs[i] == seq) return i;
    }
    return -1;
}

ChatSelMove chatSelMove(int index, int delta, int count) {
    ChatSelMove r;
    if (count <= 0) return r;
    if (index < 0) index = count - 1;
    if (index >= count) index = count - 1;

    const int want = index + delta;
    // Down past the newest leaves the selection (the ticket's second way out, besides Esc). Up at the oldest
    // stops: there is no history above it to move into.
    if (want >= count) return r;
    if (want < 0) {
        r.index  = 0;
        r.active = true;
        return r;
    }
    r.index  = want;
    r.active = true;
    return r;
}

int chatSelScroll(int index, int count) {
    if (count <= 0 || index < 0) return 0;
    if (index >= count) index = count - 1;
    return count - 1 - index;
}

size_t chatReplyPrefix(char* out, size_t cap, const char* sender) {
    if (out == nullptr || cap == 0) return 0;
    out[0] = '\0';
    if (sender == nullptr || sender[0] == '\0') return 0;
    const int w = snprintf(out, cap, "@%s ", sender);
    if (w <= 0) return 0;
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

void chatInfoHead(const ChatInfo& in, char* title, size_t title_cap, char* sub, size_t sub_cap) {
    const char* who = in.outgoing ? "you" : (in.sender[0] != '\0' ? in.sender : "unknown sender");
    setStr(title, title_cap, who);
    if (in.kind == ChatMsgKind::IncomingDm) {
        setStr(sub, sub_cap, "direct message");
    } else if (in.chat_name[0] != '\0') {
        char buf[40];
        snprintf(buf, sizeof(buf), "channel %s", in.chat_name);
        setStr(sub, sub_cap, buf);
    } else {
        setStr(sub, sub_cap, "channel");
    }
}

int chatInfoRows(const ChatInfo& in, const om::mc::ContactStore& s, InfoRow* out, int max) {
    if (out == nullptr || max <= 0) return 0;

    static const char* const kNotKept = "not kept after a restart";

    // How many rows this message WILL have, counted before building: when the sheet cannot hold them all it keeps
    // its last slot for "+N more", and that N has to be the truth even for a 64-hop path (MeshCore's kMaxPath).
    //   sent, received, route, hops [+ one per hop, or one "path not kept"], bytes, heard/repeated
    int hop_rows = 0;
    if (in.hops_known && in.hops > 0) {
        const bool path_ok = in.hash_size >= 1 && in.hash_size <= 3 &&
                             in.path_bytes >= (uint8_t)(in.hash_size * in.hops);
        hop_rows = path_ok ? (int)in.hops : 1;
    }
    const int total = 4 + hop_rows + 2;

    // Built into a local list first, so the fold below has something to cut. kBuildMax only has to be larger than
    // kMaxInfoRows: the rows past the fold are reasoned about by `total`, never materialised.
    constexpr int kBuildMax = 24;
    InfoRow tmp[kBuildMax];
    int     n = 0;
    auto add = [&](const char* label, const char* value) {
        if (n >= kBuildMax) return;
        setStr(tmp[n].label, sizeof(tmp[n].label), label);
        setStr(tmp[n].value, sizeof(tmp[n].value), value);
        ++n;
    };
    auto addFmt = [&](const char* label, const char* fmt, unsigned v) {
        if (n >= kBuildMax) return;
        setStr(tmp[n].label, sizeof(tmp[n].label), label);
        snprintf(tmp[n].value, sizeof(tmp[n].value), fmt, v);
        ++n;
    };

    // The chat and the sender are the sheet's HEAD (chatInfoHead), not rows: that is what keeps the body inside
    // 14 rows even for an eight-hop path. The body starts at the times.
    //
    // 1./2. the frame's timestamp, and our clock when it landed. A restored record stores ONE wall time and no
    //       arrival time, so "received" is not known after a restart and says so.
    add("Sent", in.sent[0] != '\0' ? in.sent : (in.restored ? kNotKept : "unknown"));
    add("Received", in.received[0] != '\0' ? in.received : kNotKept);

    // 3. route (MeshCore's Packet.h route field: flood, a scoped flood, or direct).
    const char* route = kNotKept;
    switch (in.route) {
        case om::mc::RouteType::Flood: route = "flood"; break;
        case om::mc::RouteType::ScopedFlood: route = "scoped"; break;
        case om::mc::RouteType::Direct: route = "direct"; break;
        case om::mc::RouteType::Unknown: route = in.restored ? kNotKept : "unknown"; break;
    }
    add("Route", route);

    // 4. hops, then one line per hop: its hash, and its name only when exactly one node matches (v58's
    //    pathHopLabel — never a guess between two contacts sharing a prefix).
    if (!in.hops_known) {
        add("Hops", kNotKept);
    } else if (in.hops == 0) {
        add("Hops", "direct");
    } else {
        addFmt("Hops", "%u hops", (unsigned)in.hops);
        const int hs = in.hash_size;
        if (hs < 1 || hs > 3 || in.path_bytes < (uint8_t)(hs * in.hops)) {
            // The count is honest but the bytes are not all there: say the path, not a made-up one.
            add("Path", kNotKept);
        } else {
            for (int h = 0; h < (int)in.hops; ++h) {
                const uint8_t* hash = in.path + h * hs;
                char           hex[8] = "";
                size_t         hn     = 0;
                for (int b = 0; b < hs && hn + 3 <= sizeof(hex); ++b) {
                    const int w = snprintf(hex + hn, sizeof(hex) - hn, "%02x", (unsigned)hash[b]);
                    if (w <= 0) break;
                    hn += (size_t)w;
                }
                char name[48] = "";
                char label[12];
                snprintf(label, sizeof(label), "Hop %d", h + 1);
                if (om::mc::pathHopLabel(s, hash, (uint8_t)hs, name, sizeof(name)) && name[0] != '\0') {
                    char val[60];
                    snprintf(val, sizeof(val), "%s (%s)", name, hex);
                    add(label, val);
                } else {
                    add(label, hex);
                }
            }
        }
    }

    // 5. the frame's size, and how many copies were heard. Both are live-radio facts: a restored record has
    //    neither, and a zero would read as a measurement rather than as "not known".
    if (in.frame_len > 0) addFmt("Bytes", "%u B", (unsigned)in.frame_len);
    else add("Bytes", kNotKept);

    // Copies: mc_store counts the EXTRA copies of an incoming frame (a frame heard once has copies == 0), while
    // for our own line it counts the echoes heard back. The short meta line (EST-462) already prints this number
    // as "heard N×" from two up, so the sheet keeps the same wording and only fills in the one-copy case.
    if (in.restored) {
        add(in.outgoing ? "Repeated" : "Heard", kNotKept);
    } else if (in.outgoing) {
        addFmt("Repeated", "%u\xC3\x97", (unsigned)in.copies);
    } else {
        addFmt("Heard", "%u\xC3\x97", (unsigned)in.copies + 1u);   // copies = EXTRA copies (Claude, S515)
    }

    // Everything fits: hand the rows over as built.
    if (total <= max) {
        const int n_out = n < total ? n : total;
        for (int i = 0; i < n_out; ++i) out[i] = tmp[i];
        return n_out;
    }

    // The path is longer than the sheet: keep the head of the list and say how many lines were left out. The last
    // row is the fold, so a sheet that ends mid-path never looks complete.
    if (max < 2) return 0;
    const int kept = max - 1;
    for (int i = 0; i < kept && i < n; ++i) out[i] = tmp[i];
    setStr(out[kept].label, sizeof(out[kept].label), "");
    snprintf(out[kept].value, sizeof(out[kept].value), "+%d more", total - kept);
    return kept + 1;
}

}  // namespace ui
}  // namespace om
