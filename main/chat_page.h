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

#include "chat_layout.h"   // mc::Line / kMaxLines: the wrap the view carries (S503)
#include "mc_store.h"
#include "screen.h"
#include "tm_theme.h"

namespace om {
namespace ui {

constexpr int kChatRows = 8;   // message rows or channel rows the view can carry

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
    char        left[8] = "";      // "09:12" (messages) or the unread count (channels)
    const char* title = "";        // sender, or "# channel"
    const char* body = "";         // message text, or the channel's last line
    char        right[8] = "";     // age
    bool        outgoing = false;
    bool        unread = false;
};

struct ChatView {
    bool open = false;

    // ---- list mode
    ChatRowView rows[kChatRows];
    int         row_count = 0;
    int         focused = 0;
    int         channel_count = 0;
    int         unread_total = 0;

    // ---- open mode
    int  channel = 0;
    char channel_name[32] = "";
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
    bool        sending = false;   // a send is in flight (the log says when it lands)

    // ---- chrome
    const char* clock = "--:--";
    int         battery_pct = -1;
    bool        charging = false;
};

void drawChat(Screen& s, const ChatView& v);

}  // namespace ui
}  // namespace om
