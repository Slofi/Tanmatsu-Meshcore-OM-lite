// SPDX-License-Identifier: MIT
// S515/EST-461 item 3: the power button turns the screen and the keyboard backlight OFF (Filip: "a way to turn
// the screen off, like clicking the power button just turns the screen/keyboard back light off").
//
// The whole DECISION is here, away from the BSP: main.cpp only performs it (remember the brightnesses, set
// both to 0, skip the panel blit, restore on wake). That split is why this file can be host-tested, and it is
// the same shape as core/advert_rule.h and core/own_top.h.
//
// The rules, exactly:
//   * the PRESS edge toggles; the RELEASE edge never does anything (the BSP sends both);
//   * while OFF, EVERY key (navigation, keyboard, scancode) is SWALLOWED and the screen STAYS OFF: only the power
//     button wakes it. Filip, S515 (Claude's review, replacing "any key wakes it"): "not any button, the power
//     button wakes the device. It is also a 'key-lock' for transport.." — keys pressed in a pocket must do nothing;
//   * a message arriving does NOT wake the screen (the sound still plays);
//   * nothing else changes: the radio, the chat and the sounds keep running.
#pragma once

#include <stdint.h>

namespace om {
namespace ui {

// One input fact. The caller classifies the BSP event; nothing here knows a key code.
enum class ScreenEv : uint8_t {
    PowerPress,     // the power button went down (args_action.state == true)
    PowerRelease,   // …and up. Never acted on
    Key,            // a navigation / keyboard / scancode event
    Message,        // a packet, a notice, a DM: NOT an input, and it must not light the panel
};

// What the caller must DO. `OnByPower` means "restore the backlights and repaint once"; `Swallow` means "the
// screen is locked: drop this key, change nothing" (the key-lock).
enum class ScreenAct : uint8_t { None, Off, OnByPower, Swallow };

struct ScreenPower {
    bool off = false;

    // One event in, one action out. The state moves only here, so a test drives exactly what the loop drives.
    ScreenAct onEvent(ScreenEv ev) {
        switch (ev) {
            case ScreenEv::PowerPress:
                if (off) {
                    off = false;
                    return ScreenAct::OnByPower;
                }
                off = true;
                return ScreenAct::Off;
            case ScreenEv::PowerRelease:
                return ScreenAct::None;   // the release edge is not a command
            case ScreenEv::Key:
                return off ? ScreenAct::Swallow : ScreenAct::None;   // locked: the key is dropped, still dark
            case ScreenEv::Message:
                return ScreenAct::None;   // a message never wakes the screen
        }
        return ScreenAct::None;
    }
};

}  // namespace ui
}  // namespace om
