// SPDX-License-Identifier: MIT
// The radio plan: what we ask the SX1262 for, in the units the Tanmatsu's own radio component speaks.
//
// Portable on purpose (PLAN §2 rule 1: only the core knows the radio's settings; the driver stays behind
// main/radio_link.*). The host test can therefore check every number before anything reaches the air —
// at 22 dBm a wrong field is not a cosmetic bug.
//
// Every value below was READ from a source line, not assumed. Two of them were wrong in my first pass
// and are the reason this comment exists:
//
//   * frequency / SF / CR / sync word / 22 dBm — `firmware/src/ui/settings_prefs.h:70` and
//     `firmware/src/radio.cpp:47` (the L2's own begin() call).
//   * **preamble = 32 for SF8** — MeshCore's own rule, `firmware/lib/meshcore/helpers/radiolib/
//     RadioLibWrappers.h:56`: `preambleLengthForSF(sf) { return sf <= 8 ? 32 : 16; }`. I first wrote 8
//     (the value in the L2's bring-up probe `radio.cpp`), which is NOT the MeshCore path the mesh runs on.
//   * **bandwidth is an INTEGER kHz switch** — `nicolaielectronics/tanmatsu-lora` 0.6.0 `lora.c:322`:
//     62 means the chip's 62.5 kHz step, and anything unmapped becomes 125 kHz with no error. Sending
//     62500 or 62.5 here would silently move us to another channel width.
//   * **ramp is microseconds, nearest step up** — `nicolaielectronics/sx126x` 0.3.0 `sx126x.c:438-445`
//     (0 → 10 us). 200 us is RadioLib's own SX126x default.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tm {

struct RadioPlan {
  uint32_t freq_hz;     // 869618000 = 869.618 MHz (the SLO preset)
  uint16_t bw_khz;      // 62 = the chip's 62.5 kHz step; see the note above
  uint8_t  sf;          // 5..12
  uint8_t  cr;          // 5..8 (5 = 4/5 ... 8 = 4/8); the SLO mesh runs 4/8
  uint8_t  sync_word;   // MeshCore's 0x12
  uint16_t preamble;    // symbols; 32 for SF<=8 per MeshCore
  uint8_t  power_dbm;   // 22 for the SX1262
  uint16_t ramp_us;     // PA ramp, mapped to the chip's nearest step
  bool     crc;
  bool     ldro;        // low data rate optimisation
  bool     rx_boost;    // +3 dB receive gain, ~2 mA
};

// The mesh we are joining: 869.618 MHz / BW 62.5 / SF8 / CR8 / sync 0x12 / preamble 32 / 22 dBm.
constexpr RadioPlan kSloPlan = {869618000u, 62u, 8u, 8u, 0x12u, 32u, 22u, 200u, true, false, true};

// Limits from the SX1262 range and the components' own header comments.
constexpr bool radio_plan_valid(const RadioPlan& p) {
  if (p.freq_hz < 150000000u || p.freq_hz > 960000000u) return false;
  if (p.sf < 5 || p.sf > 12) return false;
  if (p.cr < 5 || p.cr > 8) return false;
  if (p.power_dbm > 22) return false;           // sx126x.c:432 rejects >22 in high-power mode
  if (p.preamble == 0) return false;
  switch (p.bw_khz) {
    case 7: case 10: case 15: case 20: case 31:
    case 41: case 62: case 125: case 250: case 500:
      return true;
    default:
      return false;   // the component would quietly pick 125 kHz instead
  }
}

// LDRO is asked for when one symbol is longer than 16 ms (SX1262 datasheet rule of thumb).
// At SF8 / 62.5 kHz a symbol is 4.096 ms, so it stays off — asserted by a host test.
constexpr bool radio_ldro_recommended(const RadioPlan& p) {
  // 64-bit on purpose: SF12 at 7 kHz is 4.096e9 us, inside uint32 only by luck of rounding.
  const uint64_t symbol_us = ((uint64_t)(1u << p.sf) * 1000000u) / ((uint64_t)p.bw_khz * 1000u);
  return symbol_us > 16000u;
}

// ---- the step-3a beacon ----------------------------------------------------------------------------
// Deliberately NOT a MeshCore packet (that is step 3b): its job is to leave the antenna with a fixed,
// checkable shape so the transmit path can be proven end to end later. 11 bytes.
//
//   "OMLT" | version=1 | sequence (u32 LE) | battery millivolts (i16 LE, -1 = unknown)
constexpr size_t kBeaconBytes = 11;
constexpr uint8_t kBeaconVersion = 1;
constexpr int16_t kBeaconNoBattery = -1;

// Returns the number of bytes written, or 0 if the buffer is too small.
size_t radio_beacon_build(uint8_t* buf, size_t cap, uint32_t sequence, int16_t millivolts);

// True only for a well-formed beacon; outputs are untouched on failure.
bool radio_beacon_parse(const uint8_t* buf, size_t len, uint32_t* sequence, int16_t* millivolts);

}  // namespace tm
