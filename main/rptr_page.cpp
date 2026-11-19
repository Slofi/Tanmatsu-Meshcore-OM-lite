// SPDX-License-Identifier: MIT
#include "rptr_page.h"

#include <stdio.h>
#include <string.h>

#include "chrome.h"
#include "launcher_page.h"
#include "nodes_page.h"   // drawTextClipped: one clipping rule with Nodes/Chat
#include "pages.h"
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"

namespace om {
namespace ui {
namespace {

const char* modeRight(const RptrView& v, char* buf, size_t cap) {
    if (v.mode == RptrMode::Status) {
        snprintf(buf, cap, "status%s", v.status_pending ? " · waiting" : "");
        return buf;
    }
    if (v.mode == RptrMode::Login) {
        snprintf(buf, cap, "login · %s", rptr::loginStateText(v.login_state));
        return buf;
    }
    if (v.found == nullptr) return "manual scan";
    char state[32];
    v.found->stateText(state, sizeof(state));
    snprintf(buf, cap, "manual scan · %s", state);
    return buf;
}

void hexPrefix(char* out, size_t cap, const uint8_t* key) {
    snprintf(out, cap, "%02x%02x%02x", key[0], key[1], key[2]);
}

// S514 (Claude; Filip: "RPTRs list doesn't scroll"): the list always drew from row 0 and stopped at the bottom edge,
// so a focused repeater below it was off screen. The first visible row now follows the focus the way the sound
// picker does (EST-451): the gold frame stops at the last visible row and the list moves under it, and the same at
// the top. Page-local state: the RPTR list is drawn by the main task only.
int s_rptr_first = 0;

void drawFoundRows(Screen& s, const RptrView& v, int x0, int x1, int top) {
    const int row_right = x1 - kTmRingInset;
    const int count     = v.found != nullptr ? v.found->count : 0;
    int       visible   = (kTmH - kTmHintH - top) / kRptrRowH;
    if (visible < 1) visible = 1;
    if (v.focus < s_rptr_first) s_rptr_first = v.focus;
    if (v.focus >= s_rptr_first + visible) s_rptr_first = v.focus - visible + 1;
    if (s_rptr_first > count - visible) s_rptr_first = count - visible;
    if (s_rptr_first < 0) s_rptr_first = 0;
    for (int i = s_rptr_first; v.found != nullptr && i < count; ++i) {
        const rptr::Found& f = v.found->rows[i];
        const int y0 = top + (i - s_rptr_first) * kRptrRowH;
        const int y1 = y0 + kRptrRowH;
        if (y1 > kTmH - kTmHintH) break;
        const bool focused = (i == v.focus && v.mode == RptrMode::List);
        if (focused) {
            s.fillRect(x0 - 8, y0, row_right - (x0 - 8), kRptrRowH, kSelBg);
            s.frame(x0 - 8, y0, row_right - (x0 - 8), kRptrRowH, 2, kAcc);
        } else {
            s.hline(x0, row_right, y1, kRowLine);
        }
        s.disc(x0 + 5, y0 + kRptrRowH / 2, 4, kGreen);
        char name[40];
        if (f.name[0] != '\0')
            snprintf(name, sizeof(name), "%s", f.name);
        else
            hexPrefix(name, sizeof(name), f.prefix);
        char meta[64];
        char snr[24] = "";
        if (f.hasSnrOut) rptr::snrText(snr, sizeof(snr), f.snrOut4);
        snprintf(meta, sizeof(meta), "%s · %s · %u B key", f.hops == 0 ? "direct" : "path",
                 snr[0] ? snr : "no snr", (unsigned)f.keyLen);
        drawTextClipped(s, Face::Row, x0 + 24, y0 + 22, row_right - x0 - 32, name,
                        focused ? kAccInk : kInk);
        drawTextClipped(s, Face::Meta, x0 + 24, y0 + 42, row_right - x0 - 32, meta, kDimmer);
        if (f.inContacts) {
            // A tiny "in contacts" mark, drawn as text: the faces carry no check glyph.
            drawText(s, Face::Hint, row_right - 44, y0 + 20, "saved", kAcc);
        }
    }
    // S514 (Claude; Filip: "add a slider (indicator) to the RPTR list, which is longer than the screen"): the
    // Nodes list's own scroll bar, same place, width and colours (nodes_page.cpp), so the two lists read alike.
    if (count > visible) {
        const int track_top = top, track_bot = kTmH - kTmHintH - 8;
        const int track_h   = track_bot - track_top;
        const int thumb_h   = track_h * visible / count;
        const int max_first = count - visible;
        const int thumb_y   = track_top + (max_first > 0 ? (track_h - thumb_h) * s_rptr_first / max_first : 0);
        s.fillRect(x1 - 6, track_top, 3, track_h, kPanel);
        s.fillRect(x1 - 6, thumb_y, 3, thumb_h < kTmScrollMin ? kTmScrollMin : thumb_h, kAcc);
    }
}

void drawListDetail(Screen& s, const RptrView& v, int x, int rw, int top) {
    int y = top + 16;
    drawText(s, Face::Title, x, y + 24, "Repeaters", kInk);
    y += 44;
    drawTextClipped(s, Face::Meta, x, y, rw,
                    "Scan asks every repeater in range (zero hop). Enter opens the login; a remembered "
                    "password is filled in for you.",
                    kDim);
    y += 26;
    if (v.found != nullptr && v.focus >= 0 && v.focus < v.found->count) {
        const rptr::Found& f = v.found->rows[v.focus];
        char line[80];
        s.hline(x, x + rw, y, kLine);
        y += 22;
        char name[40];
        if (f.name[0] != '\0')
            snprintf(name, sizeof(name), "%s", f.name);
        else
            hexPrefix(name, sizeof(name), f.prefix);
        drawText(s, Face::Row, x, y + 14, name, kInk);
        y += 34;
        snprintf(line, sizeof(line), "Key     %02x %02x %02x … %02x", f.prefix[0], f.prefix[1],
                 f.prefix[2], f.keyLen >= 32 ? f.key[31] : f.prefix[5]);
        drawText(s, Face::Kv, x, y, line, kDim);
        y += 20;
        char snrIn[24] = "n/a", snrOut[24] = "n/a";
        if (f.hasSnrIn) rptr::snrText(snrIn, sizeof(snrIn), f.snrIn4);
        if (f.hasSnrOut) rptr::snrText(snrOut, sizeof(snrOut), f.snrOut4);
        snprintf(line, sizeof(line), "SNR     in %s · out %s", snrIn, snrOut);
        drawText(s, Face::Kv, x, y, line, kDim);
        y += 20;
        snprintf(line, sizeof(line), "Hops    %u · %s", (unsigned)f.hops,
                 f.inContacts ? "in contacts" : "answered the scan");
        drawText(s, Face::Kv, x, y, line, kDim);
        y += 30;
        drawText(s, Face::Hint, x, y, "Enter = log in", kAcc);
    } else {
        drawText(s, Face::Body, x, y + 8,
                 v.found != nullptr && v.found->listening ? "Listening…" : "Press s to scan.", kDim);
    }
}

void drawLoginDetail(Screen& s, const RptrView& v, int x, int rw, int top) {
    int y = top + 16;
    drawText(s, Face::Title, x, y + 24, v.target != nullptr ? v.target : "Repeater", kInk);
    y += 44;
    drawText(s, Face::Meta, x, y, "Password (shown as dots; never logged, never shown again)", kDim);
    y += 14;
    const int field_h = 52;
    s.fillRect(x, y, rw, field_h, kFieldBg);
    s.frame(x, y, rw, field_h, 2, kAcc);
    char dots[24];
    const size_t n = v.password_len < sizeof(dots) - 1 ? v.password_len : sizeof(dots) - 1;
    for (size_t i = 0; i < n; ++i) dots[i] = '.';
    dots[n] = '\0';
    if (n == 0) {
        drawText(s, Face::Row, x + 14, textBaseline(s, Face::Row, y, field_h), "", kInk);
        drawText(s, Face::Meta, x + 14, textBaseline(s, Face::Meta, y, field_h), "type the password",
                 kDimmer);
    } else {
        drawText(s, Face::Row, x + 14, textBaseline(s, Face::Row, y, field_h), dots, kInk);
    }
    y += field_h + 16;
    char line[96];
    snprintf(line, sizeof(line), "%u / %d characters · Enter logs in · Esc back", (unsigned)v.password_len,
             rptr::kPasswordMax);
    drawText(s, Face::Meta, x, y, line, kDimmer);
    y += 30;
    if (v.note != nullptr && v.note[0] != '\0') {
        const uint16_t col = v.login_state == rptr::LoginState::NoAnswer ||
                                     v.login_state == rptr::LoginState::Refused
                                 ? (uint16_t)kWarn
                                 : (uint16_t)kAcc;
        drawText(s, Face::Body, x, y, v.note, col);
    }
    if (v.remember) {
        y += 24;
        drawText(s, Face::Hint, x, y, "Remember passwords is On", kDimmer);
    }
}

// EST-470: the SETTINGS section's geometry, one home. Ten rows at 24 px do not fit under six rows of stats in a
// 386 px pane, so the pane shows ONE of the two: the stats with the section's head under them (no focus, as v64), or
// the ten rows with their answer and foot line (a row focused). Both fit whole, so no half row is ever drawn.
// The numbers below are content-relative to the pane's first line.
constexpr int kSetRowH    = 24;
constexpr int kSetLabelY  = 300;   // "SETTINGS", right under the stats' clock line
constexpr int kSetRowsY   = 322;   // the first row
constexpr int kSetAnswerY = kSetRowsY + (int)om::rptr::kSetRowCount * kSetRowH + 10;
constexpr int kSetFootY   = kSetAnswerY + 22;

int s_status_scroll = 0;   // page-local, main task only, like s_rptr_first

// One settings row: label left, the repeater's OWN value right, the gold focus band when it is the focus. A value
// that has not been read yet is "…" - never a placeholder and never the L2's default.
void drawSetRow(Screen& s, const RptrView& v, int x, int rw, int row, int y0) {
    const int y1 = y0 + kSetRowH;
    const int right = x + rw - kTmRingInset;
    const bool focused = v.set_focus == row;
    if (y1 <= 0 || y0 >= kTmH - kTmHintH) return;
    if (focused) {
        s.fillRect(x - 8, y0, right - (x - 8), kSetRowH, kSelBg);
        s.frame(x - 8, y0, right - (x - 8), kSetRowH, 2, kAcc);
    } else {
        s.hline(x, right, y1, kRowLine);
    }
    drawText(s, Face::Body, x, y0 + 5, om::rptr::setRowLabel(row), focused ? kAccInk : kInk);

    char value[64] = "\xe2\x80\xa6";   // U+2026: "not read yet", the ticket's own glyph
    if (v.set != nullptr) {
        if (row == (int)om::rptr::SetRow::TxPower && focused && v.set_tx_draft >= (int)om::rptr::kTxMin) {
            snprintf(value, sizeof(value), "%d dBm", v.set_tx_draft);   // the +/- draft, before the confirm
        } else if (om::rptr::setRowIsAction(row)) {
            snprintf(value, sizeof(value), "%s", row == (int)om::rptr::SetRow::Reboot ? "reboot" : "send");
        } else if (!om::rptr::settingsRowValue(*v.set, row, value, sizeof(value))) {
            snprintf(value, sizeof(value), "\xe2\x80\xa6");
        }
    }
    drawTextClipped(s, Face::Kv, x + 150, y0 + 6, right - (x + 150) - 6, value, focused ? kAcc : kDim);
}

void drawStatusDetail(Screen& s, const RptrView& v, int x, int rw, int top) {
    const int y0       = top + 16;
    const int pane_bot = kTmH - kTmHintH - 8;

    // Two panes in one. With NO focus the pane opens on the stats exactly as v64 did and the SETTINGS section
    // starts under them; the moment a row is focused the pane moves to the section (10 x 24 + the answer + the foot
    // line is 334 px of the 386 available).
    const bool settings_pane = v.set_focus >= 0;
    s_status_scroll = settings_pane ? kSetLabelY - 12 : 0;

    char line[96];
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "%s", v.target != nullptr ? v.target : "Repeater");
    if (!settings_pane) drawText(s, Face::Title, x, y0 + 24 - s_status_scroll, title_buf, kInk);

    if (!settings_pane && v.status != nullptr && v.status->valid) {
        const rptr::Status& st = *v.status;
        drawText(s, Face::Meta, x, y0 + 40 - s_status_scroll, "The repeater's own counters", kDim);
        // Two columns of label/value, every value with its unit.
        char up[24], batt[24], snr[24], noise[24], rssi[24];
        rptr::uptimeText(up, sizeof(up), st.uptimeSec);
        rptr::batteryVoltsText(batt, sizeof(batt), st.battMv);
        const int pct = rptr::batteryPercent(st.battMv);
        char batt2[48];
        if (pct >= 0)
            snprintf(batt2, sizeof(batt2), "%s (%d%%)", batt, pct);
        else
            snprintf(batt2, sizeof(batt2), "%s", batt);
        rptr::snrText(snr, sizeof(snr), st.lastSnr4);
        snprintf(noise, sizeof(noise), "%d dBm", (int)st.noiseFloor);
        snprintf(rssi, sizeof(rssi), "%d dBm", (int)st.lastRssi);
        struct Cell {
            const char* label;
            const char* value;
        } cells[] = {
            {"UPTIME", up},          {"BATTERY", batt2},        {"NOISE FLOOR", noise},
            {"LAST RSSI", rssi},     {"LAST SNR", snr},         {"TX QUEUE", ""},
            {"PACKETS RX", ""},      {"PACKETS TX", ""},        {"AIRTIME", ""},
            {"RX AIRTIME", ""},      {"EVENTS", ""},            {"CRC ERRORS", ""},
        };
        char vals[14][32];
        snprintf(vals[0], sizeof(vals[0]), "%u", (unsigned)st.txQueue);
        snprintf(vals[1], sizeof(vals[1]), "%lu", (unsigned long)st.rxPackets);
        snprintf(vals[2], sizeof(vals[2]), "%lu", (unsigned long)st.txPackets);
        snprintf(vals[3], sizeof(vals[3]), "%lu s", (unsigned long)st.airtimeSec);
        snprintf(vals[4], sizeof(vals[4]), "%lu s", (unsigned long)st.rxAirtimeSec);
        snprintf(vals[5], sizeof(vals[5]), "%u", (unsigned)st.errEvents);
        snprintf(vals[6], sizeof(vals[6]), "%lu", (unsigned long)st.rxCrcErrors);
        cells[5].value = vals[0];
        cells[6].value = vals[1];
        cells[7].value = vals[2];
        cells[8].value = vals[3];
        cells[9].value = vals[4];
        cells[10].value = vals[5];
        cells[11].value = vals[6];
        const int col_w = rw / 2;
        for (int i = 0; i < 12; ++i) {
            const int cy = 64 + (i / 2) * 34;
            if (y0 + cy + 34 > pane_bot) continue;   // the pane is 386 px: a short one drops the last row
            const int cx = x + (i % 2) * col_w;
            const int yy = y0 + cy - s_status_scroll;
            drawText(s, Face::Hint, cx, yy, cells[i].label, kDimmer);
            drawText(s, Face::Body, cx, yy + 15, cells[i].value, kInk);
        }
        const int clock_y = y0 + 274 - s_status_scroll;
        if (clock_y > 0 && clock_y < pane_bot) {
            if (st.clockValid) {
                snprintf(line, sizeof(line), "Clock %lu (repeater's own) · RTT %lu ms", (unsigned long)st.clock,
                         (unsigned long)(st.haveRtt ? st.rttMs : 0));
                drawText(s, Face::Meta, x, clock_y, line, kDim);
            } else {
                drawText(s, Face::Meta, x, clock_y, "Clock not set on the repeater", kDim);
            }
        }
    } else if (!settings_pane && v.status_pending) {
        drawText(s, Face::Body, x, y0 + 64 - s_status_scroll, "Reading the repeater's status…", kAcc);
    } else if (!settings_pane) {
        drawText(s, Face::Body, x, y0 + 64 - s_status_scroll, "No status yet. Press r to ask.", kDim);
    }

    // ---- EST-470: the SETTINGS section. Enter edits, and every change goes through the shared confirm sheet.
    const int lab_y = y0 + kSetLabelY - s_status_scroll;
    if (lab_y > 0 && lab_y < pane_bot) {
        drawText(s, Face::Meta, x, lab_y, "SETTINGS", kDim);
        if (!settings_pane) drawText(s, Face::Hint, x + rw - kTmRingInset - 62, lab_y, "↓ open", kAcc);
    }
    for (int row = 0; row < (int)om::rptr::kSetRowCount; ++row) {
        const int cy = y0 + kSetRowsY + row * kSetRowH - s_status_scroll;
        if (cy + kSetRowH <= y0 || cy >= pane_bot) continue;
        drawSetRow(s, v, x, rw, row, cy);
    }
    // The repeater's last words: OK, its own error text, or "no answer". A change is never shown as done here
    // without an answer, and the ROW's value always comes from a read.
    const int ans_y = y0 + kSetAnswerY - s_status_scroll;
    if (ans_y > 0 && ans_y + 16 < pane_bot) {
        if (v.set_pending) {
            drawText(s, Face::Hint, x, ans_y, "waiting for the repeater…", kAcc);
        } else if (v.set_answer != nullptr && v.set_answer[0] != '\0') {
            drawTextClipped(s, Face::Hint, x, ans_y, rw - kTmRingInset, v.set_answer,
                            v.set_answer_bad ? (uint16_t)kWarn : (uint16_t)kAcc);
        }
    }
    const int foot_y = y0 + kSetFootY - s_status_scroll;
    if (foot_y > 0 && foot_y + 16 < pane_bot) {
        snprintf(line, sizeof(line), "%lu s ago · r reads the values again · Esc logs out",
                 (unsigned long)v.status_age_s);
        drawText(s, Face::Hint, x, foot_y, line, kDimmer);
    }

    // The shared text field, drawn OVER the pane's bottom when it is open (the row behind keeps the old value).
    if (v.set_edit) {
        const int field_h = 56;
        const int fy      = pane_bot - field_h - 26;
        s.fillRect(x - 8, fy, rw, field_h, kFieldBg);
        s.frame(x - 8, fy, rw, field_h, 2, kAcc);
        snprintf(line, sizeof(line), "%s", v.set_edit_label != nullptr ? v.set_edit_label : "");
        drawText(s, Face::Hint, x - 2, fy - 16, line, kDim);
        char shown[80];
        snprintf(shown, sizeof(shown), "%s_", v.set_edit_text != nullptr ? v.set_edit_text : "");
        drawTextClipped(s, Face::Row, x + 6, textBaseline(s, Face::Row, fy, field_h), rw - 20, shown, kInk);
        drawText(s, Face::Hint, x - 2, fy + field_h + 4, "Enter sends · Esc cancels",
                 kDimmer);
    }
}

}  // namespace

void drawRptr(Screen& s, const RptrView& v) {
    const int W = kTmW, H = kTmH;
    s.clear((uint16_t)kBg);
    drawStatusBar(s, v.clock, v.battery_pct, v.charging);

    char right[64];
    const int head_bot = drawPageHead(s, "RPTR", modeRight(v, right, sizeof(right)));
    const int work_top = head_bot;
    const int list_x0 = kTmPad;
    const int list_x1 = kTmPad + kTmListW;

    // The list stays visible in every mode: the login and status pages open BESIDE it, so Esc has somewhere
    // to go back to and the user always sees which repeater is open.
    drawFoundRows(s, v, list_x0, list_x1, work_top + 44);
    drawText(s, Face::Meta, list_x0, work_top + 18, "REPEATERS", kDim);
    {
        // S514 (Claude; Filip: the Scan button had "an extra s at the end, outside the border, and the text is not
        // centred"): the text was drawn LEFT-aligned from the box's centre, so "SCAN  s" ran out of the box. It is
        // measured and centred now, and the key is left to the hint bar ("s scan"), where every other key lives.
        const char* chip = v.scanning ? "SCAN…" : "SCAN";
        const int   fw   = 90;
        const int   bx   = list_x1 - kTmRingInset - fw;
        s.fillRect(bx, work_top + 4, fw, 24, kPanel);
        s.frame(bx, work_top + 4, fw, 24, 1, kLine);
        const Font* f  = fontFor(Face::Meta);
        const int   tw = (f != nullptr && f->ready()) ? f->textWidth(chip) : fw - 16;
        const int   tx = tw < fw ? bx + (fw - tw) / 2 : bx + 4;
        drawText(s, Face::Meta, tx, textBaseline(s, Face::Meta, work_top + 4, 24), chip, kDim);
    }
    if (v.found != nullptr && v.found->count == 0 && !v.found->listening) {
        drawText(s, Face::Meta, list_x0, work_top + 70, "none answered", kDimmer);
    }

    s.vline(list_x1 + 8, work_top, H - kTmHintH - 8, kLine);
    const int x = list_x1 + 8 + 24;
    const int rw = W - kTmPad - x;

    if (v.mode == RptrMode::Login)
        drawLoginDetail(s, v, x, rw, work_top);
    else if (v.mode == RptrMode::Status)
        drawStatusDetail(s, v, x, rw, work_top);
    else
        drawListDetail(s, v, x, rw, work_top);

    HintItem hints[4];
    int n = 0;
    hints[n++] = {"up/dn", "move"};
    if (v.mode == RptrMode::List) {
        hints[n++] = {"Enter", "log in"};
        hints[n++] = {"s", "scan"};
        hints[n++] = {"esc", "back"};
    } else if (v.mode == RptrMode::Login) {
        hints[n++] = {"Enter", "send"};
        hints[n++] = {"esc", "back"};
    } else {
        // EST-470: the status view's settings rows. Left/Right is the TX power draft, the only stepper here.
        if (v.set_edit) {
            hints[n++] = {"Enter", "send"};
            hints[n++] = {"bksp", "delete"};
        } else if (v.set_focus == (int)om::rptr::SetRow::TxPower) {
            hints[n++] = {"l/r", "dBm"};
            hints[n++] = {"Enter", "send"};
        } else {
            hints[n++] = {"Enter", "change"};
            hints[n++] = {"r", "re-read"};
        }
        hints[n++] = {"esc", v.set_edit ? "cancel" : "log out"};
    }
    drawHintBar(s, tm::Page::Rptr, hints, n);
}

}  // namespace ui
}  // namespace om
