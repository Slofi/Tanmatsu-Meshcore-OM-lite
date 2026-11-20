// SPDX-License-Identifier: MIT
// Selecting a message in an open chat, the menu it opens, and what that menu's actions need (S515/EST-469).
//
// Filip, S515: *"when 'scrolling' up in the chat, add a selection box, and pressing enter opens up a 'pop-up menu'
// for reply, map, DM, info, etc.. pressing Esc closes the selection box and enables to type again"*.
//
// Everything here is arithmetic and string work, so it is host-tested and the page only draws what it is handed:
//   · the selection INDEX rules (enter at the newest, move, leave on Esc or Down past the newest, and stay on the
//     SAME message — by seq — when a new one arrives while selecting);
//   · the MENU ROWS a message gets, which differ by kind (incoming channel / incoming DM / our own);
//   · the name-to-node rule: a channel message carries only the sender's NAME, so DM/Map/Node may use a node
//     only when EXACTLY ONE row in the table carries that name — the same "never guess" rule as chat_meta's
//     pathHopLabel, and a dim row that says why when it cannot;
//   · the reply prefix, in the MeshCore apps' own form ("@Name ", firmware/src/main.cpp:1175);
//   · the Info sheet's lines, honest about what a restored record does NOT keep.
//
// No RSSI and no SNR anywhere: the C6's numbers are unusable (docs/DEVICE.md § 5b).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"

namespace om {
namespace ui {

// -------------------------------------------------------------------------------------- the message's kind
// Three kinds, because that is exactly what changes the menu: a channel message can be replied to on the channel
// and its sender reached elsewhere; a DM's sender is already this thread; our own line can only be looked at.
enum class ChatMsgKind : uint8_t { IncomingChannel = 0, IncomingDm = 1, Own = 2 };

enum class ChatMsgAction : uint8_t { None = 0, Reply, DirectMessage, Map, Node, Info };

// Five is the real maximum (an incoming channel message: Reply, DM, Map, Node, Info). A bound, so a future row
// cannot silently overrun a caller's array.
constexpr int kMaxChatMenuRows = 5;

// A row of the chat menu. The LABEL and the VALUE are copied into the row: the DM row's label carries the
// sender's name, and a `const char*` would have to point at a buffer that outlives the builder.
struct ChatMsgRow {
    ChatMsgAction action = ChatMsgAction::None;
    char          label[40] = "";
    char          value[28] = "";   // why the row is dim: "unknown node" / "2 nodes share this name" / "no position"
    bool          dim = false;
};

// One message's facts, as the shell sees them. The NODE is resolved by the caller (chatNodeByName for a channel
// message's sender, contactsFind for a DM's peer) because that is where the store is read; `node_note` is the
// reason the row is dim, in the helper's own words.
struct ChatMsgContext {
    ChatMsgKind kind = ChatMsgKind::IncomingChannel;
    const char* sender = "";          // the sender's name as the frame carried it
    int         node_index = -1;      // the sender's row in the contact table, -1 = not resolved
    const char* node_note = "";       // "unknown node" / "2 nodes share this name" (see chatNodeNote)
    bool        node_has_position = false;
    bool        in_dm = false;        // the open chat IS the sender's DM thread
};

// The rows in the ticket's order, restricted to what this message kind can offer:
//   incoming channel: Reply, "DM <sender>", Map, Node, Info
//   incoming DM:      Reply, Map, Node, Info
//   our own:          Info
// Returns 0..kMaxChatMenuRows. A row whose node cannot be resolved is DIM and carries the reason; Enter on a
// dim row runs nothing (the caller's rule, mirrored here as `dim`).
int chatMsgRows(const ChatMsgContext& c, ChatMsgRow* out, int max);

// ------------------------------------------------------------------------------- the name-to-node rule
// A channel message carries only the sender's NAME, so this is the whole resolution:
//   · a full-key contact whose name matches EXACTLY counts (a prefix-only row is not a node, mc_store's rule);
//   · exactly one match resolves; zero says "unknown node", two or more say "N nodes share this name" and name
//     nobody — picking one would be a guess, the same rule pathHopLabel follows for a path hash.
struct ChatNodeByName {
    int index   = -1;   // the matching row, only when matches == 1
    int matches = 0;    // how many full-key rows carry that name
};

ChatNodeByName chatNodeByName(const om::mc::ContactStore& s, const char* name);

// The dim row's right-hand reason: "" when the name resolved, "unknown node", or "<N> nodes share this name".
// `out` may be null when cap is 0 (then only the constant form is returned).
const char* chatNodeNote(const ChatNodeByName& m, char* out, size_t cap);

// ---------------------------------------------------------------------------------------- selection rules
// The selection is kept by SEQ, never by index: messages arrive while the box is up, and the frame must stay on
// the SAME message rather than jumping to whatever now occupies that slot.
int  chatSelEnter(int count);            // the newest message's index, -1 when there is nothing to select
int  chatSelIndexForSeq(const uint32_t* seqs, int count, uint32_t seq);   // -1 when that message is gone

// Up/Down inside the selection. Up at the oldest stops there (the window is already at the top of the history);
// Down PAST THE NEWEST leaves the selection, which is the ticket's second way out besides Esc.
struct ChatSelMove {
    int  index  = -1;
    bool active = false;
};
ChatSelMove chatSelMove(int index, int delta, int count);

// How many messages the window must skip from the NEWEST end so the selected one is on screen. The selected
// message is the bottom of the window: moving up through history scrolls the view with it, and a message that
// arrives above it moves neither. 0 = nothing selected or the newest is selected.
int chatSelScroll(int index, int count);

// --------------------------------------------------------------------------------------------- reply
// "@[<sender>] " — the MeshCore apps' reply form (S516/EST-481; OverMesh's replyToMc writes exactly this, and
// 1,044 of the 4,811 messages it stored carry it). A channel text has no reply field on the wire, so the mention
// IS the reply. The name is cleaned as OverMesh cleans it ([ ] CR LF removed, trimmed) and a name that cleans to
// nothing writes nothing; the implementation is core/chat_mentions.cpp::mentionReplyPrefix, host-tested there.
// A DM has one reader, so its reply adds no prefix at all. Returns the bytes written.
size_t chatReplyPrefix(char* out, size_t cap, const char* sender);

// -------------------------------------------------------------------------------------------- Info sheet
// What the Info sheet is built from. Times arrive as drawn strings ("09:12") because the clock is device data and
// this file must stay pure; "" means NOT KNOWN and is never invented.
struct ChatInfo {
    ChatMsgKind  kind      = ChatMsgKind::IncomingChannel;
    bool         outgoing  = false;
    char         sender[om::mc::kNameBytes] = "";
    char         chat_name[24] = "";    // the open channel's name, "" for a DM thread
    char         sent[8]     = "";      // the frame's own timestamp
    char         received[8] = "";      // our clock when it arrived (local)
    // A record read back from the card keeps the path and the wall time, but NOT the route, the frame's bytes or
    // the copies: those are live-radio facts (mc_history.cpp leaves them absent). `restored` is what makes the
    // sheet say "not kept after a restart" instead of printing a zero that would read as a measurement.
    bool         restored   = false;
    om::mc::RouteType route = om::mc::RouteType::Unknown;
    bool         hops_known = false;
    uint8_t      hops       = 0;
    uint8_t      hash_size  = 0;
    uint8_t      path_bytes = 0;
    uint8_t      path[meshcore::kMaxPath] = {0};
    uint16_t     frame_len  = 0;
    uint16_t     copies     = 0;        // duplicates heard (incoming) / echoes of our frame (ours)
};

constexpr int kMaxInfoRows = 14;   // what fits a sheet above the hint bar at this row height
struct InfoRow {
    char label[12] = "";
    char value[60] = "";
};

// The sheet's head: who, and where. `title` is the sender ("you" for our own line), `sub` is "channel <name>" /
// "direct message".
void chatInfoHead(const ChatInfo& in, char* title, size_t title_cap, char* sub, size_t sub_cap);

// The lines, in the ticket's order: chat, sender, sent, received, route, hops, one line per hop (its hash, and
// its name only when exactly one node matches — pathHopLabel), bytes, heard/repeated. Returns the number written;
// when the path is long enough to overflow the sheet, the LAST row is "+N more" rather than a silent cut.
int chatInfoRows(const ChatInfo& in, const om::mc::ContactStore& s, InfoRow* out, int max);

}  // namespace ui
}  // namespace om
