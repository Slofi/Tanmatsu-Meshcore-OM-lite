// SPDX-License-Identifier: MIT
// Sounds on the Tanmatsu, S514/EST-450: the five notification events, their L2 defaults, the 400 ms
// rate gate and the stored settings, as plain C++.
//
// The sound ITSELF is not here. The L2's shared phrase library is the one source for both devices:
// `firmware/src/sound_synth.h/.cpp` (EST-361/369, plain C++, no allocation) is included/compiled
// straight across the tree, exactly as `core/mc_history.*` already uses the L2's history format.
// That means a Tanmatsu sound and an L2 sound cannot drift into two different waveforms.
//
// What this header owns is the POLICY the ticket names:
//   * the L2's event names and default sounds (`sound::kEvents` in sound_synth.cpp);
//   * Boot is OFF on the Tanmatsu (the L2's own default is Bell TK-TK);
//   * never a sound for our own echo, a duplicate or a restored history line;
//   * at most one sound per kSoundMinGapMs (400 ms), with the time passed in so the host test can
//     drive exact instants and the 32-bit millisecond clock's wrap is pinned;
//   * the settings' byte format (a library id per event, kSoundOff for Off), and the L2's kRedirect
//     rule for a stored id whose sound was retired.
//
// No Arduino, no IDF, no NVS. `main/sound_store.*` is the device's NVS wrapper and `main/audio_out.*`
// is the one owner of I2S; the host tests drive this header and the real `sound_synth.cpp`.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../firmware/src/sound_synth.h"

namespace tm {
namespace soundcfg {

// The events this device sounds. The order is this file's own; the L2's ids/names are used for the
// five shared events, and Boot is the sixth because the settings page still offers it.
enum class Event : uint8_t {
  Boot = 0,
  Message,
  Dm,
  Sent,
  Error,
  NewNode,
  Count,
};
constexpr int kEventCount = (int)Event::Count;

// The ticket's floor: a burst of adverts must not machine-gun the speaker or the audio queue.
constexpr uint32_t kSoundMinGapMs = 400;

// The L2's volume default (`sound_events.cpp`: getUChar("vol", 60)); the settings row walks it by 10.
constexpr uint8_t kDefaultVolume = 60;

// S514 (Claude; Filip: "I have it set to 100% and it is still barely hearable"). The volume CURVE is the L2's
// (firmware/src/audio.cpp:279-285): the codec's DAC volume register runs -95.5 dB (0x00) .. +32 dB (0xFF) in
// 0.5 dB steps on both the L2's ES8311 and the Tanmatsu's ES8156 (es8156.c:796-798), and the L2 maps
// 1..100 % onto 0x60 (-47.5 dB) .. 0xFF (+32 dB), so 60 % is 0 dB (register 0xBF) and 0 % mutes (0x00).
// The Tanmatsu BSP instead maps its percentage linearly over the whole -95.5..+32 dB span after a 0.70 factor
// (badge_bsp_audio.c:16,100), so 100 % reached only -6.25 dB and 60 % about -42 dB.
constexpr uint8_t volumeRegister(uint8_t pct) {
  if (pct == 0) return 0x00;
  const uint32_t p     = pct > 100 ? 100u : pct;
  constexpr uint32_t kFloor = 0x60;
  return (uint8_t)(kFloor + ((uint32_t)(0xFF - kFloor) * p) / 100u);
}

// The percentage to hand bsp_audio_set_volume() so the codec lands on volumeRegister(pct): the BSP multiplies by
// 0.70 and es8156_set_volume_percentage() maps 0..100 linearly onto the 255 register steps with lroundf, so the
// BSP value is reg * (100 / 255) / 0.70. Values above 100 are fine: the codec driver clamps its own input.
constexpr float bspVolumeForRegister(uint8_t reg) { return ((float)reg * 100.0f / 255.0f) / 0.70f; }

constexpr const char* eventKey(Event ev) {
  switch (ev) {
    case Event::Boot:    return "boot";
    case Event::Message: return "message";
    case Event::Dm:      return "dm";
    case Event::Sent:    return "sent";
    case Event::Error:   return "error";
    case Event::NewNode: return "newnode";
    default:             return "message";
  }
}

constexpr const char* eventLabel(Event ev) {
  switch (ev) {
    case Event::Boot:    return "Boot";
    case Event::Message: return "Message";
    case Event::Dm:      return "Direct message";
    case Event::Sent:    return "Sent";
    case Event::Error:   return "Error";
    case Event::NewNode: return "New node";
    default:             return "Message";
  }
}

// The L2 event a Tanmatsu event maps to. Boot maps to Boot; the other five are the L2's own events.
constexpr om::sound::Event l2Event(Event ev) {
  switch (ev) {
    case Event::Boot:    return om::sound::Event::Boot;
    case Event::Message: return om::sound::Event::Message;
    case Event::Dm:      return om::sound::Event::Dm;
    case Event::Sent:    return om::sound::Event::Sent;
    case Event::Error:   return om::sound::Event::Error;
    case Event::NewNode: return om::sound::Event::NewNode;
    default:             return om::sound::Event::Message;
  }
}

// The L2's default sound for the event — EXCEPT Boot, which the ticket sets Off here (Filip removed the
// boot advert; a boot chime would be a sound for something that no longer happens). The L2's own Boot
// default is Bell TK-TK and its welcome sound is enabled by default.
inline uint8_t defaultSoundFor(Event ev) {
  if (ev == Event::Boot) return om::sound::kSoundOff;
  return om::sound::defaultSound(l2Event(ev));
}

// A stored id as the sound it means now: the L2's kRedirect for a retired EST-361 id, the event's
// default for a corrupt/out-of-range id, and Off exactly as stored.
inline uint8_t resolveStoredSound(Event ev, uint8_t id) {
  if (id == om::sound::kSoundOff) return om::sound::kSoundOff;
  const uint8_t kept = om::sound::resolveSound(id);
  return kept != om::sound::kSoundOff ? kept : defaultSoundFor(ev);
}

// The row/panel name of a stored choice. "Off" for kSoundOff (the L2's picker row's own word).
inline const char* choiceName(uint8_t id) {
  if (id == om::sound::kSoundOff) return "Off";
  const char* name = om::sound::soundName(id);
  return name != nullptr ? name : "Off";
}

// --------------------------------------------------------------------------------------------- settings
// What the device stores: a master switch, a volume 0..100, and one library id per event.
struct SoundSettings {
  bool    master = true;          // the ticket's default: Sound On
  uint8_t volume = kDefaultVolume;  // the L2's 60
  uint8_t ids[kEventCount] = {0};
};

constexpr uint8_t kSettingsVersion = 1;
// [version][master][volume][ids ...]: 3 + 6 = 9 bytes.
constexpr size_t kSettingsBytes = 3 + (size_t)kEventCount;

inline SoundSettings defaultSettings() {
  SoundSettings s;
  for (int i = 0; i < kEventCount; ++i) s.ids[i] = defaultSoundFor((Event)i);
  return s;
}

// The one setter: a retired id becomes the kept sound it means, Off stays Off, and a value that cannot
// be a sound at all falls back to the event's default. The L2 does the same on load.
inline void setChoice(SoundSettings& s, Event ev, uint8_t id) {
  const int i = (int)ev;
  if (i < 0 || i >= kEventCount) return;
  s.ids[i] = resolveStoredSound(ev, id);
}

// Encode into a caller's buffer. Returns the number of bytes, or 0 when the buffer is too small.
inline size_t encodeSettings(const SoundSettings& s, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kSettingsBytes) return 0;
  uint8_t volume = s.volume > 100 ? 100 : s.volume;
  out[0] = kSettingsVersion;
  out[1] = s.master ? 1 : 0;
  out[2] = volume;
  for (int i = 0; i < kEventCount; ++i) out[3 + i] = s.ids[i];
  return kSettingsBytes;
}

// Decode what the device stored. A wrong length/version is refused (the caller then keeps the defaults),
// a volume over 100 is clamped, and every id is put through the L2's redirect rule. `settings` is only
// written on success, so a corrupt blob can never half-apply.
inline bool decodeSettings(const uint8_t* in, size_t len, SoundSettings* out) {
  if (in == nullptr || out == nullptr || len != kSettingsBytes) return false;
  if (in[0] != kSettingsVersion) return false;
  SoundSettings s = defaultSettings();
  s.master = in[1] != 0;
  s.volume = in[2] > 100 ? 100 : in[2];
  for (int i = 0; i < kEventCount; ++i) s.ids[i] = resolveStoredSound((Event)i, in[3 + i]);
  *out = s;
  return true;
}

// Enter on the Volume row: 0, 10, … 100, then back to 0.
inline uint8_t nextVolume(uint8_t v) {
  if (v >= 100) return 0;
  return (uint8_t)(((v / 10) + 1) * 10 > 100 ? 0 : ((v / 10) + 1) * 10);
}

// S514/EST-451: '+' / '-' on the Volume row, ten at a time and CLAMPED at both ends (no wrap: 100 then
// '+' stays 100, 0 then '-' stays 0). Enter keeps nextVolume() above. A corrupt stored value is treated
// as 100 before the step, so the row can always be recovered with '-'.
inline uint8_t stepVolume(uint8_t v, int delta) {
  int n = (int)(v > 100 ? 100 : v) + delta;
  if (n < 0) n = 0;
  if (n > 100) n = 100;
  return (uint8_t)n;
}

// --------------------------------------------------------------------------------------------- policy
// A stored choice that can actually make a sound: master on, not Off, a real library sound.
inline bool choiceAudible(const SoundSettings& s, Event ev) {
  const int i = (int)ev;
  if (i < 0 || i >= kEventCount) return false;
  const uint8_t id = s.ids[i];
  return id != om::sound::kSoundOff && om::sound::validSound(id);
}

// The whole decision in one place, with the three "never" flags passed in:
//   own_echo   — our own frame heard back (the seen ring's `ours`),
//   duplicate  — a second copy of a flood (the seen ring's `seen`),
//   restored   — a line restored from history/NVS, not a live arrival.
// `notice_enabled` is the NODES section's New-node notice: the ticket asks the new-node SOUND to use the
// SAME condition as that notice, so turning the notice off silences the chime too.
inline bool shouldPlay(const SoundSettings& s, Event ev, bool own_echo, bool duplicate, bool restored,
                       bool notice_enabled) {
  if (own_echo || duplicate || restored) return false;
  if (!s.master) return false;
  if (ev == Event::NewNode && !notice_enabled) return false;
  return choiceAudible(s, ev);
}

// At most one sound per kSoundMinGapMs. `lastMs` is the previous accepted play; set it to
// `nowMs - kSoundMinGapMs` at boot (or use the `ever` form below) so the first event is not delayed.
// The subtraction is unsigned, so a wrap of the 32-bit millisecond clock is measured as the real gap.
inline bool rateDue(uint32_t lastMs, uint32_t nowMs) {
  return (uint32_t)(nowMs - lastMs) >= kSoundMinGapMs;
}

}  // namespace soundcfg
}  // namespace tm
