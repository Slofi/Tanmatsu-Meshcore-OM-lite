// SPDX-License-Identifier: MIT
// S515/EST-467: the NEW-MESSAGE POP-UP's decisions, as plain C++.
//
// Filip's words: "a new MSG should appear as a pop-up with info on what CH and a node name and a msg
// displayed. on any page, any button dismiss it and doesnt do anything else on that page."
//
// Everything that can be decided without a screen is here, so the host can pin it:
//   * an incoming channel text or DM raises ONE pop-up (the newest wins);
//   * a newer message while it is up REPLACES the text and counts the ones it replaced: "+N more";
//   * ANY key while it is up dismisses it and is SWALLOWED (the page must not see that key);
//   * it also goes by itself after kMsgPopupMs (10 s);
//   * it is NOT shown while that same chat is open on screen (you are already reading it);
//   * it is NOT shown while the screen is off: the sound still plays, but nothing is queued for later;
//   * our own messages never pop up.
//
// The caller supplies the channel/sender key of the message and of the chat on screen, so "same chat" is a
// comparison here, not a page lookup. The DRAWING and the task-safe slot live in main/chrome.cpp; this
// header has no display, no clock and no FreeRTOS (the time is passed in, like core/advert_rule.h).
#pragma once

#include <stdint.h>
#include <string.h>

namespace om {
namespace ui {

// 10 s: long enough to read a two-line message, short enough not to sit on the map forever.
constexpr uint32_t kMsgPopupMs = 10000;
constexpr uint32_t kMsgPopupMaxExtra = 99;

enum class PopupChat : uint8_t { None = 0, Channel = 1, Dm = 2 };

struct MsgPopup {
    bool      visible    = false;
    uint32_t  until_ms   = 0;   // esp_timer milliseconds, passed in by the caller
    uint32_t  extra      = 0;   // messages that arrived while this one was up ("+N more")
    PopupChat chat       = PopupChat::None;
    uint8_t   channel    = 0;
    uint8_t   peer[32]   = {0};

    void dismiss() {
        visible  = false;
        until_ms = 0;
        extra    = 0;
        chat     = PopupChat::None;
        channel  = 0;
        memset(peer, 0, sizeof(peer));
    }

    // Is this message the chat the user is already looking at? A channel is its index; a DM is its public
    // key. Channel 0 is a valid channel, so the KIND is checked first.
    bool sameChat(PopupChat c, uint8_t ch, const uint8_t* pub) const {
        if (c != chat) return false;
        if (c == PopupChat::Channel) return ch == channel;
        if (c == PopupChat::Dm) return pub != nullptr && memcmp(peer, pub, sizeof(peer)) == 0;
        return false;
    }

    // One arriving message. Returns true when the glass must change.
    //   screen_off      no pop-up, and NO queue for later: this message is dropped here
    //   own             our own echo never pops up (and never clears somebody else's pop-up)
    //   same_chat_open  that chat is on screen: do not show it (and leave any other pop-up alone)
    //   arrived         how many messages this call stands for (normally 1). The newest is the one shown; all the
    //                   others are counted into "+N more", and so is a pop-up this one replaces
    bool offer(uint32_t now_ms, bool screen_off, bool own, bool same_chat_open, PopupChat c,
               uint8_t ch, const uint8_t* pub, uint32_t arrived = 1) {
        if (screen_off) {
            const bool was = visible;
            dismiss();   // no queued pop-up for later
            return was;
        }
        if (own || same_chat_open) return false;

        const bool was_visible = visible;
        if (!was_visible) {
            // S516 (EST-473): `arrived` can be a BATCH — the radio task offers every message at once and the main
            // loop polls twice a second (main/chrome.cpp), so two messages can land inside one window. The newest
            // is the one on the plate; the others are what "+N more" counts. It used to read 0 here, so a batch
            // arriving with no pop-up up showed the newest and said nothing about the first one.
            extra = arrived > 0 ? arrived - 1 : 0;
        } else if (arrived > 0) {
            // A pop-up is already up: the message it was showing joins the ones not shown, so the batch counts
            // whole (and a single message still reads "+1 more").
            extra += arrived;
            if (extra > kMsgPopupMaxExtra) extra = kMsgPopupMaxExtra;
        }
        visible  = true;
        until_ms = now_ms + kMsgPopupMs;   // a newer message restarts the 10 s
        chat     = c;
        channel  = ch;
        if (pub != nullptr)
            memcpy(peer, pub, sizeof(peer));
        else
            memset(peer, 0, sizeof(peer));
        return true;
    }

    // Any key while the pop-up is up: dismiss it and tell the caller to SWALLOW the key. False when there
    // is no pop-up, so the page gets its key as usual.
    bool key() {
        if (!visible) return false;
        dismiss();
        return true;
    }

    // The 10 s timeout. `now_ms` may wrap (a uint32 monotonic clock), so the comparison is a signed
    // difference, exactly like the advert rule's windows.
    bool tick(uint32_t now_ms) {
        if (!visible) return false;
        if ((int32_t)(now_ms - until_ms) < 0) return false;
        dismiss();
        return true;
    }
};

}  // namespace ui
}  // namespace om
