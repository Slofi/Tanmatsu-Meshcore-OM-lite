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

    // EST-470: the SETTINGS section under the stats (part 2a's safe settings). Every VALUE here came from the
    // repeater's own answer; a row that has not been read yet draws "…", never a placeholder.
    const rptr::Settings* set = nullptr;
    int          set_focus = -1;        // -1 = not focused yet: the pane opens on the stats
    bool         set_pending = false;   // one CLI line is waiting for its answer
    const char*  set_answer = "";       // the repeater's last words ("OK", its error text, "no answer")
    bool         set_answer_bad = false;
    // The shared text field, open on a text row. `set_edit_text` is the DRAFT: the row behind keeps the value the
    // repeater answered until Apply, exactly as EST-422 requires.
    bool         set_edit = false;
    const char*  set_edit_label = "";
    const char*  set_edit_text = "";
    size_t       set_edit_len = 0;
    int          set_tx_draft = 0;      // the +/- draft on the TX power row
};

void drawRptr(Screen& s, const RptrView& v);

}  // namespace ui
}  // namespace om
