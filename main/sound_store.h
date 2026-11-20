// SPDX-License-Identifier: MIT
// S514/EST-450: the sound settings' one NVS home.
//
// The FORMAT and every rule are in core/sound_events.h (host-tested). This file only reads and writes
// that byte blob in NVS namespace "mcsnd". Values are library ids exactly as the L2 stores them, so a
// choice means the same sound on both devices and a retired id still resolves through the L2's kRedirect.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sound_events.h"

namespace om {
namespace soundstore {

// Read "mcsnd"/"cfg". A missing or corrupt blob is NOT an error: the defaults (Sound On, volume 60,
// Boot Off, the L2's per-event defaults) are used and one line says which case it was.
bool begin();

const tm::soundcfg::SoundSettings& settings();

// Each setter writes the whole blob first and only then moves the live value, so a failed NVS write
// leaves the panel showing the value the device will really use after a reboot. False = not saved.
bool setMaster(bool on);
bool setVolume(uint8_t volume);
bool setChoice(tm::soundcfg::Event ev, uint8_t id);

// S516/EST-478: the volume keys change the LIVE value on every press but write NVS at most once, after
// ~2 s with no presses (core/volume_popup.h::VolumeSave owns the arithmetic). volumeTick() is called once
// per main-loop pass with esp_timer milliseconds; it writes the whole blob when the window has passed and
// logs ONE line per actual write.
bool setVolumeLive(uint8_t volume, uint32_t now_ms);
void volumeTouch(uint32_t now_ms);
void volumeTick(uint32_t now_ms);

// True when the namespace opened and the last read/write succeeded at least once.
bool available();

}  // namespace soundstore
}  // namespace om
