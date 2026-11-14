// SPDX-License-Identifier: MIT
// The app's own clock: where its time comes from and what it tells the rest of the device.
//
// The rules live in core/clock.h (host-tested). This file is the device glue: it reads the Tanmatsu's real RTC,
// keeps the last good time in NVS, accepts time from the mesh, and formats the status bar.
//
// ⚠️ The CH32 coprocessor and NVS are touched by ONE task at a time: the radio task only RECORDS a time it has
// seen (noteSeen) and the main task applies it (service). That is the same shape as the radio's single owner.
#pragma once

#include <stdint.h>

namespace om {
namespace time_source {

// At boot: set the system clock from the coprocessor's RTC, else from the stored floor, else leave it at 1970
// and say so. Also sets the timezone. Needs badge-bsp initialised and NVS mounted; call once.
void begin();

// The radio task: remember an authenticated timestamp we received (an advert's ts, or a channel text's ts).
// `what` is copied; pass a short literal ("advert", "text"). Cheap, no NVS, no coprocessor, safe per packet.
void noteSeen(uint32_t ts, const char* what);

// The main task, every couple of seconds: apply a recorded time (set the clock, correct the RTC, store the
// floor), and keep the stored floor fresh. Never called from the radio task.
void service();

// "22:31" in local time (central European time), or "--:--" while the clock is not plausible.
const char* hhmm();

// The clock for the rest of the shell. These two exist so that a translation unit which deals with PAGES and
// KEYS never has to include <time.h>: that header defines `struct tm`, and the core namespace is `tm`, so the
// two cannot live in one file (see core/clock.h). One place knows the time, every page asks it.
uint32_t epochNow();
void     hhmmOf(uint32_t epoch, char* out, size_t n);

// Is the clock plausible (i.e. worth showing and worth stamping a message with)?
bool haveTime();

// Where the boot time came from: "rtc", "stored" or "none". For the log; later for a Settings row.
const char* bootSource();

}  // namespace time_source
}  // namespace om
