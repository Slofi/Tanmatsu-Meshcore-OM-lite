// SPDX-License-Identifier: MIT
// S516/EST-478: the VOLUME KEYS' rules and their on-screen pop-up, as plain C++.
//
// Filip (2026-11-20): "can you utilize the volume buttons for, well, volume control? With a pop-up
// displaying current level and changes?"
//
// The shell (main/main.cpp) owns the NVS store and the audio codec; this header owns the decisions, so the
// host test drives exactly what the loop drives:
//   * Up/Down change the SAME stored volume the Settings › General › Volume row shows, by 10, clamped
//     0..100 (soundcfg::stepVolume — the row's own rule);
//   * the pop-up reads "Volume 70 %", "Muted" at 0, "Sound off" while the master switch is Off (the
//     volume still changes then);
//   * it stays kPopupMs (1.5 s) after the last press and is re-armed by every press, even at a clamp;
//   * NVS is written ONCE after kSaveIdleMs (2 s) with no presses: a held key or a burst coalesces into
//     one write, never one per press;
//   * a locked screen (key-lock) ignores the keys entirely: only the power button acts there.
//
// Placement: the panel is kPopupW x kPopupH, centred, just above the hint bar. The message pop-up is
// centred and at most 150 px tall (main/chrome.cpp), so its lowest edge is ~315 px and this panel starts at
// 390 px on the 800x480 glass: both fit and neither hides the other. That is why the volume pop-up does not
// need to take the message pop-up's place.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "sound_events.h"

namespace tm {
namespace volume {

// The Settings row's own step (soundcfg::stepVolume), named here so the key and the row cannot drift.
constexpr int kStep = 10;

// 1.5 s after the last press (Filip's ticket).
constexpr uint32_t kPopupMs = 1500;

// One NVS write after two seconds of no presses. Every press re-arms this window.
constexpr uint32_t kSaveIdleMs = 2000;

// The key's own repeat is not faster than ~6/s. The BSP currently sends one edge per press, so in practice
// this gate is never hit; it is the ceiling the ticket names, not an invented repeat.
constexpr uint32_t kStepMinGapMs = 160;

// The panel's geometry (mirrored by tools/preview_tanmatsu.py).
constexpr int kPopupW = 420;
constexpr int kPopupH = 54;
constexpr int kPopupGapAboveBar = 12;

// One press: the row's clamped step.
inline uint8_t step(uint8_t current, int delta) { return soundcfg::stepVolume(current, delta); }

// What the pop-up says. "Sound off" WINS over "Muted": the master switch is the bigger fact, and the bar
// still shows the level the volume key just changed.
inline void popupText(char* out, size_t cap, uint8_t level, bool master_on) {
  if (out == nullptr || cap == 0) return;
  if (!master_on)
    snprintf(out, cap, "Sound off");
  else if (level == 0)
    snprintf(out, cap, "Muted");
  else
    snprintf(out, cap, "Volume %u %%", (unsigned)level);
}

// The bar's filled share, 0..100. The level itself, even while Sound is off: the next Sound On must show
// what the keys set. A corrupt stored value is clamped for the drawing only.
inline uint8_t barPercent(uint8_t level) { return level > 100 ? 100 : level; }

// Where the panel goes: centred, just above the hint bar. `hint_h` is kTmHintH on the glass.
inline void popupRect(int screen_w, int screen_h, int hint_h, int* x, int* y) {
  if (x != nullptr) *x = (screen_w - kPopupW) / 2;
  if (y != nullptr) *y = screen_h - hint_h - kPopupGapAboveBar - kPopupH;
}

// The live pop-up. `until_ms` is an esp_timer millisecond count; the comparison is a signed difference so
// the 32-bit wrap is measured correctly.
struct VolumePopup {
  bool     visible  = false;
  uint32_t until_ms = 0;
  uint8_t  level    = 0;

  // Every press re-arms the 1.5 s and redraws, even at a clamp (no change in level).
  void press(uint32_t now_ms, uint8_t new_level) {
    visible  = true;
    until_ms = now_ms + kPopupMs;
    level    = new_level;
  }

  bool showing(uint32_t now_ms) const {
    return visible && (int32_t)(now_ms - until_ms) < 0;
  }

  // True once when the 1.5 s ran out, so the caller repaints the page underneath.
  bool tick(uint32_t now_ms) {
    if (!showing(now_ms)) {
      const bool was = visible;
      visible = false;
      return was;
    }
    return false;
  }
};

// The coalesced NVS write. `changed` is a real level change; `touch` is any volume-key press (a clamp
// included), which must push the write later without scheduling a redundant one.
struct VolumeSave {
  bool     pending = false;
  uint8_t  value   = 0;
  uint32_t last_ms = 0;

  void changed(uint8_t new_level, uint32_t now_ms) {
    pending = true;
    value   = new_level;
    last_ms = now_ms;
  }
  void touch(uint32_t now_ms) {
    if (pending) last_ms = now_ms;
  }
  bool due(uint32_t now_ms) const { return pending && (uint32_t)(now_ms - last_ms) >= kSaveIdleMs; }

  // True once when the write is due; clears the pending flag and returns the value.
  bool take(uint32_t now_ms, uint8_t* out) {
    if (!due(now_ms)) return false;
    pending = false;
    if (out != nullptr) *out = value;
    return true;
  }
  void clear() { pending = false; }
};

// The ~6/s ceiling for a repeating key. The first press always passes; `last_ms` starts wherever.
struct StepGate {
  bool     ever    = false;
  uint32_t last_ms = 0;

  bool due(uint32_t now_ms) {
    if (!ever) {
      ever    = true;
      last_ms = now_ms;
      return true;
    }
    if ((uint32_t)(now_ms - last_ms) < kStepMinGapMs) return false;
    last_ms = now_ms;
    return true;
  }
};

// The key-lock: a locked screen ignores every key, the volume keys included (only the power button acts).
inline bool volumeKeyActs(bool screen_off) { return !screen_off; }

}  // namespace volume
}  // namespace tm
