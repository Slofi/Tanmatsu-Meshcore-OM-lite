// SPDX-License-Identifier: MIT
// RPTR part 1's page (S514/EST-453): the found list, the login sub-page and the STATUS view.
//
// The decisions (what is found, whether a login is still waiting, every unit) are in core/rptr_model.* and the
// packets are in core/rptr_wire.*; the radio and NVS are in main/rptr_link.*. This file only paints the view it
// is given. Nothing here reads NVS, the radio or the clock by itself, and a stored password is NEVER drawn: the
// login field shows one dot per character instead.
#pragma once

#include <stdint.h>

#include "font.h"
#include "rptr_model.h"
#include "screen.h"

namespace om {
namespace ui {

enum class RptrMode : uint8_t { List = 0, Login, Status };

constexpr int kRptrRowH = 58;

struct RptrView {
    // chrome
    const char* clock = "--:--";
    int         battery_pct = -1;
    bool        charging = false;

    RptrMode mode = RptrMode::List;

    // List (List and Login modes): the found rows, and which one owns the focus.
    const rptr::FoundList* found = nullptr;
    int                    focus = 0;
    bool                   scanning = false;

    // Login sub-page.
    const char*  target = "";          // the repeater's name (or its key prefix)
    const char*  password_dots = "";   // one '.' per typed character; NEVER the password itself
    size_t       password_len = 0;
    rptr::LoginState login_state = rptr::LoginState::Idle;
    bool         remember = true;      // the Settings switch, shown as a hint only
    const char*  note = "";            // one honest line: "sending...", "no answer", "remembered"

    // Status view.
    const rptr::Status* status = nullptr;
    bool         status_pending = false;
    uint32_t     status_age_s = 0;
};

void drawRptr(Screen& s, const RptrView& v);

}  // namespace ui
}  // namespace om
