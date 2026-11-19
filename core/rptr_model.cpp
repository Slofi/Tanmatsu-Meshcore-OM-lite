// SPDX-License-Identifier: MIT
// RPTR part 1's pure rules, ported from the L2's firmware/src/rptr_model.cpp (S514/EST-453).
#include "rptr_model.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace rptr {
namespace {

bool samePrefix(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

}  // namespace

// ------------------------------------------------------------------------------------------ password
bool passwordChar(char c) {
  const unsigned char u = (unsigned char)c;
  return u >= 0x20 && u <= 0x7E;
}

int passwordFilter(const char* src, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (src == nullptr) return 0;
  size_t n = 0;
  for (const char* p = src; *p != '\0' && n + 1 < cap && (int)n < kPasswordMax; ++p) {
    if (!passwordChar(*p)) continue;
    out[n++] = *p;
  }
  out[n] = '\0';
  return (int)n;
}

// ---------------------------------------------------------------------------------------------- found
void FoundList::begin(uint32_t tagSeed, uint32_t nowMs) {
  count = 0;
  listening = true;
  startedMs = nowMs;
  tag = tagSeed;
  ++serial;
}

int FoundList::indexOf(const uint8_t* prefix6) const {
  if (prefix6 == nullptr) return -1;
  for (int i = 0; i < count; ++i) {
    if (samePrefix(rows[i].prefix, prefix6)) return i;
  }
  return -1;
}

int FoundList::addAnswer(const uint8_t* key, int keyLen, const char* name, bool inContacts, bool hasIn,
                         int8_t in4, bool hasOut, int8_t out4, uint8_t hops, uint32_t nowMs) {
  if (key == nullptr || keyLen <= 0) return -1;
  int at = indexOf(key);   // compares the first six bytes, whatever the key length
  if (at < 0) {
    if (count >= kMaxFound) return -1;
    at = count++;
    rows[at] = Found();
    memcpy(rows[at].prefix, key, 6);
  }
  Found& f = rows[at];
  // The name and the contact flag are ours (the answer carries only the key); a later answer for the same key
  // must not clear what we already know.
  if (name != nullptr && name[0] != '\0') {
    snprintf(f.name, sizeof(f.name), "%s", name);
    f.inContacts = inContacts;
  }
  // Keep the whole key when the answer carried one: the anonymous requests need it for the shared secret.
  if (keyLen >= kPubKeyBytes) {
    memcpy(f.key, key, kPubKeyBytes);
    f.keyLen = (uint8_t)kPubKeyBytes;
  } else if (keyLen > (int)f.keyLen) {
    memcpy(f.key, key, (size_t)keyLen);
    f.keyLen = (uint8_t)keyLen;
  }
  f.hasSnrIn = hasIn;
  if (hasIn) f.snrIn4 = in4;
  f.hasSnrOut = hasOut;
  if (hasOut) f.snrOut4 = out4;
  f.hops = hops;
  f.heardMs = nowMs;
  ++serial;
  return at;
}

void FoundList::tick(uint32_t nowMs) {
  if (listening && (uint32_t)(nowMs - startedMs) >= kScanWindowMs) {
    listening = false;
    ++serial;
  }
}

void FoundList::stateText(char* out, size_t cap) const {
  if (out == nullptr || cap == 0) return;
  if (listening) {
    if (count == 0)
      snprintf(out, cap, "listening");
    else
      snprintf(out, cap, "%d found, listening", count);
  } else if (count == 0) {
    snprintf(out, cap, "none answered");
  } else {
    snprintf(out, cap, "%d found", count);
  }
}

// ---------------------------------------------------------------------------------------------- login
bool loginParse(const uint8_t* buf, int len, LoginAnswer* out) {
  if (buf == nullptr || out == nullptr || len <= 0) return false;
  *out = LoginAnswer();
  // A legacy repeater answers "OK" (v1 and earlier); MyMesh maps that to is_admin = false, but only an admin
  // could log in to those repeaters at all, so admin is the honest reading.
  if (len >= 2 && buf[0] == 'O' && buf[1] == 'K') {
    out->ok = true;
    out->legacy = true;
    out->admin = true;
    return true;
  }
  // Current MeshCore: [0] = 0 (RESP_SERVER_LOGIN_OK), [2] = is_admin, [3] = permissions, [8] = fw level.
  if (buf[0] == 0 && len >= 9) {
    out->ok = true;
    out->admin = (buf[2] & 1) != 0;
    out->permissions = buf[3];
    out->fwLevel = buf[8];
    return true;
  }
  // A reply that is neither shape: the repeater said something, and it was not a login.
  out->ok = false;
  return true;
}

const char* loginStateText(LoginState s) {
  switch (s) {
    case LoginState::Idle: return "not logged in";
    case LoginState::Sending: return "sending";
    case LoginState::Admin: return "admin";
    case LoginState::Guest: return "guest";
    case LoginState::Refused: return "refused";
    case LoginState::NoAnswer: return "no answer";
    default: return "";
  }
}

void LoginMachine::begin(uint32_t nowMs, uint32_t estTimeoutMs) {
  state = LoginState::Sending;
  sentMs = nowMs;
  deadlineMs = nowMs + estTimeoutMs + kLoginExtraMs;
  rttMs = 0;
  haveRtt = false;
  answer = LoginAnswer();
}

void LoginMachine::succeed(const LoginAnswer& a, uint32_t nowMs) {
  answer = a;
  if (!a.ok) {
    state = LoginState::Refused;
  } else {
    state = a.admin ? LoginState::Admin : LoginState::Guest;
  }
  rttMs = (uint32_t)(nowMs - sentMs);
  haveRtt = true;
}

void LoginMachine::refuse() {
  state = LoginState::Refused;
  haveRtt = false;
}

bool LoginMachine::tick(uint32_t nowMs) {
  if (state != LoginState::Sending) return false;
  // A signed difference: millis() wraps after 49 days and the deadline must still work.
  if ((int32_t)(nowMs - deadlineMs) < 0) return false;
  state = LoginState::NoAnswer;
  return true;
}

void LoginMachine::reset() {
  state = LoginState::Idle;
  sentMs = deadlineMs = rttMs = 0;
  haveRtt = false;
  answer = LoginAnswer();
}

// ---------------------------------------------------------------------------------------------- status
bool statusParse(const uint8_t* buf, int len, Status* out) {
  if (buf == nullptr || out == nullptr) return false;
  if (len < kRepeaterStatsBytes) return false;
  const uint8_t* p = buf;
  auto u16 = [&](int off) {
    uint16_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
  };
  auto i16 = [&](int off) {
    int16_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
  };
  auto u32 = [&](int off) {
    uint32_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
  };
  Status s;
  s.battMv = u16(0);
  s.txQueue = u16(2);
  s.noiseFloor = i16(4);
  s.lastRssi = i16(6);
  s.rxPackets = u32(8);
  s.txPackets = u32(12);
  s.airtimeSec = u32(16);
  s.uptimeSec = u32(20);
  s.sentFlood = u32(24);
  s.sentDirect = u32(28);
  s.recvFlood = u32(32);
  s.recvDirect = u32(36);
  s.errEvents = u16(40);
  s.lastSnr4 = i16(42);
  s.directDups = u16(44);
  s.floodDups = u16(46);
  s.rxAirtimeSec = u32(48);
  s.rxCrcErrors = u32(52);
  s.valid = true;
  *out = s;
  return true;
}

// ------------------------------------------------------------------------------------------ formatting
int uptimeText(char* out, size_t cap, uint32_t seconds) {
  if (out == nullptr || cap == 0) return 0;
  const uint32_t d = seconds / 86400u;
  const uint32_t h = (seconds / 3600u) % 24u;
  const uint32_t m = (seconds / 60u) % 60u;
  const uint32_t s = seconds % 60u;
  int n;
  if (d > 0)
    n = snprintf(out, cap, "%ud %02u:%02u", (unsigned)d, (unsigned)h, (unsigned)m);
  else if (h > 0)
    n = snprintf(out, cap, "%uh %02um", (unsigned)h, (unsigned)m);
  else
    n = snprintf(out, cap, "%um %02us", (unsigned)m, (unsigned)s);
  return n > 0 ? n : 0;
}

bool batteryPlausible(uint16_t milliVolts) {
  return milliVolts >= kBatteryMinMv && milliVolts <= kBatteryMaxMv;
}

int batteryVoltsText(char* out, size_t cap, uint16_t milliVolts) {
  if (out == nullptr || cap == 0) return 0;
  if (!batteryPlausible(milliVolts)) {
    snprintf(out, cap, "n/a");
    return (int)strlen(out);
  }
  const int n = snprintf(out, cap, "%u.%02u V", (unsigned)(milliVolts / 1000),
                         (unsigned)((milliVolts % 1000) / 10));
  return n > 0 ? n : 0;
}

int batteryPercent(uint16_t milliVolts) {
  // A single Li-ion cell: 3.0 V empty, 4.2 V full, linear (the same rough reading the status bar uses).
  if (milliVolts < 3000 || milliVolts > 4300) return -1;
  int pct = (int)(((int)milliVolts - 3000) * 100 / 1200);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

int snrText(char* out, size_t cap, int8_t snr4) {
  if (out == nullptr || cap == 0) return 0;
  const bool neg = snr4 < 0;
  const int a = neg ? -(int)snr4 : (int)snr4;
  const int n = snprintf(out, cap, "%s%d.%02d dB", neg ? "-" : "", a / 4, (a % 4) * 25);
  return n > 0 ? n : 0;
}

}  // namespace rptr
}  // namespace om
