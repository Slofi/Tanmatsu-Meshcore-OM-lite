// SPDX-License-Identifier: MIT
// S516/EST-477: the Tanmatsu's LEDs: the DEVICE glue — NVS settings, the low-priority task, bsp_led_* and
// register 146 writes.
//
// The rule is core/message_led_rule.h (pure, host-tested). This file only publishes the inputs (screen off,
// unread total, the battery state, our C6 link), runs one low-priority task that steps the rule every 25 ms,
// and applies a frame only when the frame CHANGED. The LED calls are I2C through the coprocessor, so they are
// never made from a timer/ISR context and never from the main loop.
//
// The coprocessor driver serialises the I2C bus itself: tanmatsu_coprocessor.c:163-197 takes
// `configuration.concurrency_semaphore` around every transfer, so the LED task and the keyboard/interrupt task
// cannot interleave a frame. This file adds its own mutex only so restoreAutomatic() and the task cannot write
// pixels at the same time.
#pragma once

#include <stdint.h>

#include "message_led_rule.h"

namespace om {
namespace led {

// At app start: read the stored settings, migrate the old "msgled" key away, clear register 146 once, and make
// SURE the coprocessor is back in automatic mode, so a previous run cannot leave the system indicators dark.
void begin();

// Start the one low-priority task (priority 1, 3 KB stack). Idempotent.
void startTask();

// Settings › General › LED. Each setter writes NVS first and moves the live value only on success. False = not
// saved (the row keeps showing the old value).
bool powerOn();
bool setPowerOn(bool on);
bool radioOn();
bool setRadioOn(bool on);
ui::LedColour colour();
bool         setColour(ui::LedColour colour);

// Main loop, once per pass: the screen state (core/screen_power.h), the hint bar's unread total, the battery
// facts the power LED emulates, and whether our C6 link is up (om::radio::started()).
void publish(bool screen_off, uint32_t unread, bool battery_available, bool charging, bool radio_up);
void setScreenOff(bool off);   // the power button's own edge, for an immediate stop/wake

// Esc to the launcher: all pixels dark, register 146 cleared (all false), then automatic mode back, BEFORE the
// handover (like the backlight). Stops the task; the app is about to restart.
void restoreAutomatic();

}  // namespace led
}  // namespace om
