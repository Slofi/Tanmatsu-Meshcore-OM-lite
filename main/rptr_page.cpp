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

void drawFoundRows(Screen& s, const RptrView& v, int x0, int x1, int top) {
    const int row_right = x1 - kTmRingInset;
    for (int i = 0; v.found != nullptr && i < v.found->count; ++i) {
        const rptr::Found& f = v.found->rows[i];
        const int y0 = top + i * kRptrRowH;
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

void drawStatusDetail(Screen& s, const RptrView& v, int x, int rw, int top) {
    int y = top + 16;
    drawText(s, Face::Title, x, y + 24, v.target != nullptr ? v.target : "Repeater", kInk);
    y += 40;
    char line[96];
    if (v.status_pending) {
        drawText(s, Face::Body, x, y, "Reading the repeater's status…", kAcc);
    } else if (v.status != nullptr && v.status->valid) {
        const rptr::Status& st = *v.status;
        drawText(s, Face::Meta, x, y, "The repeater's own counters", kDim);
        y += 24;
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
            const int cx = x + (i % 2) * col_w;
            const int cy = y + (i / 2) * 34;
            drawText(s, Face::Hint, cx, cy, cells[i].label, kDimmer);
            drawText(s, Face::Body, cx, cy + 15, cells[i].value, kInk);
        }
        y += 6 * 34 + 6;
        if (st.clockValid) {
            snprintf(line, sizeof(line), "Clock %lu (repeater's own) · RTT %lu ms",
                     (unsigned long)st.clock, (unsigned long)(st.haveRtt ? st.rttMs : 0));
            drawText(s, Face::Meta, x, y, line, kDim);
        } else {
            drawText(s, Face::Meta, x, y, "Clock not set on the repeater", kDim);
        }
        y += 22;
        snprintf(line, sizeof(line), "%lu s ago · r refreshes · Esc logs out", (unsigned long)v.status_age_s);
        drawText(s, Face::Hint, x, y, line, kDimmer);
    } else {
        drawText(s, Face::Body, x, y, "No status yet. Press r to ask.", kDim);
        y += 24;
        drawText(s, Face::Hint, x, y, "Esc logs out", kDimmer);
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
        const char* chip = v.scanning ? "SCAN…" : "SCAN  s";
        const int fw = 90;
        s.fillRect(list_x1 - kTmRingInset - fw, work_top + 4, fw, 24, kPanel);
        s.frame(list_x1 - kTmRingInset - fw, work_top + 4, fw, 24, 1, kLine);
        drawText(s, Face::Meta, list_x1 - kTmRingInset - fw / 2, textBaseline(s, Face::Meta, work_top + 4, 24),
                 chip, kDim);
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
        hints[n++] = {"ret", "log in"};
        hints[n++] = {"s", "scan"};
        hints[n++] = {"esc", "back"};
    } else if (v.mode == RptrMode::Login) {
        hints[n++] = {"ret", "send"};
        hints[n++] = {"esc", "back"};
    } else {
        hints[n++] = {"r", "refresh"};
        hints[n++] = {"esc", "log out"};
    }
    drawHintBar(s, tm::Page::Rptr, hints, n);
}

}  // namespace ui
}  // namespace om
