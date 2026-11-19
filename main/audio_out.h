// SPDX-License-Identifier: MIT
// S514/EST-450: ONE owner of the Tanmatsu's I2S channel and the sound task.
//
// The BSP brings the ES8156 codec and I2S up as part of bsp_device_initialize() (common/
// badge_bsp_device.c:84), so this module does NOT initialise them a second time: it takes the handle
// the BSP already published (bsp_audio_get_i2s_handle) and only falls back to bsp_audio_initialize()
// when that handle is missing. Everything else (the codec, the amplifier, the I2S clock) stays the
// BSP's; this file is the single writer of the channel.
//
// Playback is one low-priority task with its own small queue. The caller only copies a request into
// the queue (zero timeout): a full queue DROPS the request, so the radio task or the UI task can never
// be blocked by a chime. The task renders the L2's shared phrase (firmware/src/sound_synth.cpp) into a
// PSRAM buffer and streams it as interleaved 16-bit stereo, switching the amplifier on for the phrase
// and off afterwards.
#pragma once

#include <stdint.h>

#include "sound_events.h"

namespace om {
namespace audioout {

// Start the audio task after the SD card and the C6 link are up. Returns false (and logs ONE line) when
// the codec/I2S/buffer/task cannot be had; the caller then simply runs silent. Never throws, never
// blocks boot beyond the BSP's own initialisation.
bool begin();

bool ready();

// The per-event door: the stored choice, the master switch, the New-node notice gate and the 400 ms
// rate limit are all applied here. `newnode_notice_enabled` is only read for Event::NewNode.
bool playEvent(tm::soundcfg::Event ev, bool newnode_notice_enabled = true);

// Settings picker preview: the chosen sound now, bypassing the 400 ms event gate (it is an explicit
// user action) but NOT the master switch. Off is not previewable and returns false.
bool preview(uint8_t soundId);

// 0..100, the BSP's codec volume.
void setVolume(uint8_t percent);

uint32_t playedCount();
uint32_t droppedCount();

}  // namespace audioout
}  // namespace om
