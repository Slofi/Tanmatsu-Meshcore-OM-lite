// SPDX-License-Identifier: MIT
// The Chat page: the channel list, and an open channel with its composer (shots/2026-11-13-chat.png).
//
// The view is filled by main.cpp from the store under the store's lock, then drawn without one: a screen that
// held a lock for the length of a panel blit would be holding it far longer than the drawing needs.
//
// Two modes in one page, because that is how the L2 works and how Filip reads it: the list is where unread
// shows, the open chat is where typing happens. Esc goes back one level, never out of the app from inside a
// chat (the Launcher's Esc still does, which is where "back" ends).
#pragma once

#include <stdint.h>

#include "chat_cards.h"    // mc::CardGrid: the two-column card geometry and focus rules (S513)
#include "chat_layout.h"   // mc::Line / kMaxLines: the wrap the view carries (S503)
#include "mc_store.h"
#include "screen.h"
#include "tm_theme.h"

namespace om {
namespace ui {

constexpr int kChatRows = 8;   // cards per column the view can carry (more than the four a column shows)

// -------------------------------------------------------------------------------------- card geometry (S513)
// The list state is two columns of rounded cards, not one 64 px row per channel (Filip: "make channels like
// rectangles in two columns, so first column is channels, the second is DMs"). These are the ONE numbers the
// drawing and main.cpp's scroll decision share; core/chat_cards.* turns them into rects, focus moves and scrolls.
constexpr int kTmCardGutter  = 16;   // between the two columns; an odd leftover pixel stays there
constexpr int kTmCardHeaderH = 30;   // the "Channels" / "Direct messages" label band above the cards
constexpr int kTmCardH       = 68;   // one card: a 17 px name line and a 12 px preview line, comfortably
constexpr int kTmCardGap     = 10;   // vertical space between two cards in a column
constexpr int kTmCardRadius  = 6;    // rounded corners, on the card and on its focus ring
constexpr int kTmCardWorkTop = kTmStatusH + kTmHeadH;   // the head's line: where the columns start

// The grid this page draws in AND main.cpp scrolls by, built from the numbers above and `tm_theme.h`. One function,
// so a scroll decision and a draw cannot use two different geometries.
mc::CardGrid chatCardGrid();

// --------------------------------------------------------------------------------------- message geometry
// ONE source for the numbers the LAYOUT and the DRAWING both need: main.cpp wraps a message with these widths
// and heights (it has to know how tall each message is to decide which ones fit), and this page draws at them.
// Two copies of these numbers is exactly how a wrap and a draw drift apart and a line collides with the next.
constexpr int  kTmChatHeadH    = 22;                            // the time + sender line above a body
constexpr int  kTmChatLineH    = 22;                            // one wrapped body line
constexpr int  kTmChatGapH     = 8;                             // between two messages
constexpr int  kTmChatComposerH   = 52;
constexpr int  kTmChatBodyX0 = kTmPad + 58;                   // incoming text starts under the time column
constexpr int  kTmChatBodyX1 = kTmW - kTmPad - 46;           // outgoing text keeps the age column clear
constexpr int  kTmChatBodyW = kTmChatBodyX1 - kTmChatBodyX0;
constexpr int  kTmChatTop  = kTmStatusH + kTmHeadH + 36;    // status bar, page head, the channel title line
constexpr int  kTmChatBot  = kTmH - kTmHintH - kTmChatComposerH - 10;
constexpr int  kTmChatHeight = kTmChatBot - kTmChatTop;    // the height chatWindow() is budgeted
static_assert(kTmChatBodyW > 0 && kTmChatHeight > 0, "the chat geometry must leave room for text");

struct ChatRowView {
    char        left[8] = "";      // the unread count as drawn in the badge (list) or "09:12" (messages)
    const char* title = "";        // the channel / DM name (list), or the sender (messages)
    const char* body = "";         // "sender: text" (list), or the message text
    char        right[8] = "";     // the newest message's local HH:MM (list), or the age (messages)
    bool        outgoing = false;
    bool        unread = false;
};

struct ChatView {
    bool open = false;

    // ---- list mode: two columns of cards (S513/EST-430)
    // `rows` is the Channels column, `dms` the Direct messages column. The page draws BOTH from the model, so a
    // future DM slice only fills `dms`/`dm_count` and never touches the layout. Today `dm_count` is always 0
    // (this app has no TXT_MSG receive or send), and the page draws the L2's "No direct messages yet" line.
    ChatRowView rows[kChatRows];      // Channels (column 0)
    int         row_count = 0;
    ChatRowView dms[kChatRows];       // Direct messages (column 1), newest thread first
    int         dm_count = 0;
    int         focus_col = mc::kColChannels;   // an empty column cannot take focus
    int         focus_row = 0;
    int         scroll[mc::kCardCols] = {0, 0}; // each column scrolls by itself to keep its focused card visible
    int         channel_count = 0;
    int         unread_total = 0;

    // ---- open mode
    int  channel = 0;
    char channel_name[32] = "";
    bool dm_open = false;               // S513/EST-434: the open chat is a DM thread, not a channel
    int  msg_count = 0;
    // A channel's messages, oldest first, already windowed to what fits (S503: the window moves with `scroll`).
    struct Msg {
        char        hhmm[8];
        char        sender[mc::kNameBytes];
        char        text[mc::kTextBytes];
        char        age[8];
        bool        outgoing;
        // The wrap, as byte ranges into `text` (S503). The wrap itself is decided by core/chat_layout.*, so the
        // page only draws what it is given.
        uint8_t     line_count;
        bool        clipped;                  // the text did not fit: the last line is drawn with an ellipsis
        mc::Line    line[mc::kMaxLines];
    } msgs[kChatRows];
    // How much older history the window leaves out, for the two indicators. `hidden_below` is >0 only when the
    // user has scrolled back: the newest messages are then below the window, not shown.
    int hidden_above = 0;
    int hidden_below = 0;
    const char* composer = "";
    // S513: a transient line that replaces the head's right-hand status while it is set ("radio busy, not sent").
    // It lives in the same slot as "N messages" so the composer and the message list keep their geometry.
    const char* notice = nullptr;
    bool        sending = false;   // a send is in flight (the log says when it lands)

    // ---- chrome
    const char* clock = "--:--";
    int         battery_pct = -1;
    bool        charging = false;
};

void drawChat(Screen& s, const ChatView& v);

}  // namespace ui
}  // namespace om
