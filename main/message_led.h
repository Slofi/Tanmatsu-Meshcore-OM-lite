// SPDX-License-Identifier: MIT
// S515/EST-468: the message LED's DEVICE glue: NVS choice, the low-priority task, and bsp_led_* writes.
//
// The rule is core/message_led_rule.h (pure, host-tested). This file only publishes the inputs (screen off,
// unread total, the chosen LED), runs one low-priority task that steps the rule every 25 ms, and applies a
// frame through the BSP only when the frame CHANGED. The LED calls are I2C through the coprocessor, so they
// are never made from a timer/ISR context and never from the main loop.
#pragma once

#include <stdint.h>

namespace om {
namespace led {

// At app start: read the stored choice (NVS "omled"/"msgled", default Off) and make SURE the coprocessor is
// back in automatic mode, so a previous run cannot leave the system indicators dark.
void begin();

// Start the one low-priority task (priority 1, 3 KB stack). Idempotent.
void startTask();

// Settings › General › LED. 1..6 is a real choice; Off is 0. Writes NVS first and moves the live value only
// on success. False = not saved (the row keeps showing the old value).
uint8_t choice();
bool    setChoice(uint8_t led);

// Ask the task for the three-blink preview of `led` (Settings does this while the row is being changed).
void requestPreview(uint8_t led);

// Main loop, once per pass: the hint bar's unread total and the screen state (core/screen_power.h).
void publish(bool screen_off, uint32_t unread);
void setScreenOff(bool off);   // the power button's own edge, for an immediate stop/wake

// Esc to the launcher: all LEDs dark, then automatic mode back, BEFORE the handover (like the backlight).
// Stops the task; the app is about to restart.
void restoreAutomatic();

}  // namespace led
}  // namespace om
