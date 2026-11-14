// SPDX-License-Identifier: MIT
// The step-3a beacon: the only thing we put on the air until MeshCore arrives (step 3b).
//
// Plain C++, no IDF: the host test builds this file with g++ so the byte layout is proven before a
// packet ever leaves the antenna.
#include "radio_plan.h"

namespace tm {
namespace {

constexpr uint8_t kMagic[4] = {'O', 'M', 'L', 'T'};

void put_u32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

uint32_t get_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

}  // namespace

size_t radio_beacon_build(uint8_t* buf, size_t cap, uint32_t sequence, int16_t millivolts) {
  if (buf == nullptr || cap < kBeaconBytes) return 0;
  for (size_t i = 0; i < sizeof(kMagic); ++i) buf[i] = kMagic[i];
  buf[4] = kBeaconVersion;
  put_u32(buf + 5, sequence);
  buf[9]  = (uint8_t)((uint16_t)millivolts & 0xFFu);
  buf[10] = (uint8_t)(((uint16_t)millivolts >> 8) & 0xFFu);
  return kBeaconBytes;
}

bool radio_beacon_parse(const uint8_t* buf, size_t len, uint32_t* sequence, int16_t* millivolts) {
  if (buf == nullptr || len != kBeaconBytes) return false;   // exact: a longer frame is not ours
  for (size_t i = 0; i < sizeof(kMagic); ++i) {
    if (buf[i] != kMagic[i]) return false;
  }
  if (buf[4] != kBeaconVersion) return false;
  if (sequence != nullptr) *sequence = get_u32(buf + 5);
  if (millivolts != nullptr) *millivolts = (int16_t)(uint16_t)(buf[9] | (buf[10] << 8));
  return true;
}

}  // namespace tm
