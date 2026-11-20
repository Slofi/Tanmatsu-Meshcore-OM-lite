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

// ====================================================================================== part 2a: settings
namespace {

const char* const kLabels[kSetRowCount] = {"Name",      "Owner info", "Position", "Repeat",   "Power save",
                                           "TX power",  "Advert local", "Advert flood", "Clock sync", "Reboot"};

// A tiny strict parser: digits, one optional '.', digits. No signs, no exponents. Used by the coordinate and the
// TX-power readers, so a stray answer ("OK", "Error...") is refused rather than read as a number.
bool parseUnsigned(const char* s, unsigned long* whole, unsigned long* frac, int* frac_digits) {
  if (s == nullptr) return false;
  unsigned long w = 0, f = 0;
  int           digits = 0, fd = 0;
  bool          dot = false;
  while (*s) {
    if (*s == '.' && !dot) {
      dot = true;
      s++;
      continue;
    }
    if (*s < '0' || *s > '9') return false;
    if (!dot) {
      if (++digits > 9) return false;
      w = w * 10 + (unsigned long)(*s - '0');
    } else {
      if (++fd > 9) return false;
      f = f * 10 + (unsigned long)(*s - '0');
    }
    s++;
  }
  if (digits == 0 && fd == 0) return false;   // "" and "." are not numbers
  *whole = w;
  *frac = f;
  *frac_digits = fd;
  return true;
}

// The seven reads, in the order the link sends them: one value per row, Position taking two.
const char* const kReads[kSetReadCount] = {kCliGetName, kCliGetOwner,  kCliGetLat, kCliGetLon,
                                           kCliGetRepeat, kCliPowerSavingRead, kCliGetTx};

}  // namespace

const char* setRowLabel(int row) {
  if (row < 0 || row >= kSetRowCount) return "";
  return kLabels[row];
}

bool setRowIsText(int row) {
  return row == (int)SetRow::Name || row == (int)SetRow::Owner || row == (int)SetRow::Position;
}
bool setRowIsToggle(int row) {
  return row == (int)SetRow::Repeat || row == (int)SetRow::PowerSave;
}
bool setRowIsStepper(int row) { return row == (int)SetRow::TxPower; }
bool setRowIsAction(int row) {
  return row == (int)SetRow::AdvertLocal || row == (int)SetRow::AdvertFlood ||
         row == (int)SetRow::ClockSync || row == (int)SetRow::Reboot;
}

const char* setRowReadCmd(int row) {
  switch ((SetRow)row) {
    case SetRow::Name: return kCliGetName;
    case SetRow::Owner: return kCliGetOwner;
    case SetRow::Position: return kCliGetLat;
    case SetRow::Repeat: return kCliGetRepeat;
    case SetRow::PowerSave: return kCliPowerSavingRead;
    case SetRow::TxPower: return kCliGetTx;
    default: return nullptr;   // the actions have nothing to read
  }
}

const char* setRowReadCmd2(int row) { return row == (int)SetRow::Position ? kCliGetLon : nullptr; }

const char* setRowActionCmd(int row) {
  switch ((SetRow)row) {
    case SetRow::AdvertLocal: return kCliAdvertLocal;
    case SetRow::AdvertFlood: return kCliAdvertFlood;
    case SetRow::ClockSync: return kCliClockSync;
    case SetRow::Reboot: return kCliReboot;
    default: return nullptr;
  }
}

const char* setReadCmd(int i) {
  if (i < 0 || i >= kSetReadCount) return nullptr;
  return kReads[i];
}

int setReadRow(const char* cmd) {
  if (cmd == nullptr) return -1;
  if (strcmp(cmd, kCliGetName) == 0) return (int)SetRow::Name;
  if (strcmp(cmd, kCliGetOwner) == 0) return (int)SetRow::Owner;
  if (strcmp(cmd, kCliGetLat) == 0 || strcmp(cmd, kCliGetLon) == 0) return (int)SetRow::Position;
  if (strcmp(cmd, kCliGetRepeat) == 0) return (int)SetRow::Repeat;
  if (strcmp(cmd, kCliPowerSavingRead) == 0) return (int)SetRow::PowerSave;
  if (strcmp(cmd, kCliGetTx) == 0) return (int)SetRow::TxPower;
  return -1;
}

int degreesText(char* out, size_t cap, int32_t e6) {
  if (out == nullptr || cap == 0) return 0;
  const bool neg = e6 < 0;
  const int64_t v = neg ? -(int64_t)e6 : (int64_t)e6;
  const int64_t hundred_thousandths = (v + 5) / 10;   // e6 -> 1e-5 units, rounded
  const int n = snprintf(out, cap, "%s%lld.%05lld", neg ? "-" : "", (long long)(hundred_thousandths / 100000),
                         (long long)(hundred_thousandths % 100000));
  return n > 0 ? n : 0;
}

bool settingsRowValue(const Settings& s, int row, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  switch ((SetRow)row) {
    case SetRow::Name:
      if (!s.nameOk) return false;
      snprintf(out, cap, "%s", s.name);
      return true;
    case SetRow::Owner:
      if (!s.ownerOk) return false;
      snprintf(out, cap, "%s", s.owner);
      return true;
    case SetRow::Position: {
      if (!s.positionOk()) return false;
      char lat[24], lon[24];
      degreesText(lat, sizeof(lat), s.latE6);
      degreesText(lon, sizeof(lon), s.lonE6);
      snprintf(out, cap, "%s, %s", lat, lon);
      return true;
    }
    case SetRow::Repeat:
      if (!s.repeatOk) return false;
      snprintf(out, cap, "%s", s.repeat ? "on" : "off");
      return true;
    case SetRow::PowerSave:
      if (!s.psOk) return false;
      snprintf(out, cap, "%s", s.powerSave ? "on" : "off");
      return true;
    case SetRow::TxPower:
      if (!s.txOk) return false;
      snprintf(out, cap, "%u dBm", (unsigned)s.tx);
      return true;
    default:
      return false;
  }
}

bool settingsApplyRead(Settings* s, const char* read_cmd, const char* reply) {
  if (s == nullptr || read_cmd == nullptr || reply == nullptr) return false;
  if (strcmp(read_cmd, kCliGetName) == 0) {
    char v[kNameMax + 1];
    if (!cliStripPrefix(reply, v, sizeof(v))) return false;
    snprintf(s->name, sizeof(s->name), "%s", v);
    s->nameOk = true;
    return true;
  }
  if (strcmp(read_cmd, kCliGetOwner) == 0) {
    char v[kOwnerMax + 1];
    if (!cliStripPrefix(reply, v, sizeof(v))) return false;
    snprintf(s->owner, sizeof(s->owner), "%s", v);
    s->ownerOk = true;
    return true;
  }
  if (strcmp(read_cmd, kCliGetLat) == 0 || strcmp(read_cmd, kCliGetLon) == 0) {
    int32_t e6 = 0;
    if (!cliParseCoord(reply, &e6)) return false;
    if (strcmp(read_cmd, kCliGetLat) == 0) {
      if (e6 < -90000000 || e6 > 90000000) return false;
      s->latE6 = e6;
      s->latOk = true;
    } else {
      if (e6 < -180000000 || e6 > 180000000) return false;
      s->lonE6 = e6;
      s->lonOk = true;
    }
    return true;
  }
  if (strcmp(read_cmd, kCliGetRepeat) == 0) {
    bool on = false;
    if (!cliParseOnOff(reply, &on)) return false;
    s->repeat = on;
    s->repeatOk = true;
    return true;
  }
  if (strcmp(read_cmd, kCliPowerSavingRead) == 0) {
    bool on = false;
    if (!cliParseOnOff(reply, &on)) return false;
    s->powerSave = on;
    s->psOk = true;
    return true;
  }
  if (strcmp(read_cmd, kCliGetTx) == 0) {
    uint8_t dbm = 0;
    if (!cliParseTx(reply, &dbm)) return false;
    s->tx = dbm;
    s->txOk = true;
    return true;
  }
  return false;
}

// ------------------------------------------------------------------------------------- the CLI line builders
int cliName(char* out, size_t cap, const char* name, bool positionShared) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (name == nullptr || name[0] == '\0') return 0;
  const size_t len = strlen(name);
  const size_t max = positionShared ? (size_t)kNameSharedMax : (size_t)kNameMax;
  if (len > max) return 0;
  // The repeater itself refuses the seven characters its own isValidName forbids (CommonCLI.cpp:27-33); anything
  // outside printable ASCII would be a JSON/UTF-8 surprise in its prefs, so the field's rules are enforced here.
  for (size_t i = 0; i < len; i++) {
    const char c = name[i];
    if (c < 0x20 || c > 0x7E) return 0;
    if (c == '[' || c == ']' || c == '\\' || c == ':' || c == ',' || c == '?' || c == '*') return 0;
  }
  const int n = snprintf(out, cap, "set name %s", name);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliOwner(char* out, size_t cap, const char* owner) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (owner == nullptr || owner[0] == '\0') return 0;   // an empty owner is refused, the L2's rule
  // '|' is how the repeater stores a newline in this field (CommonCLI.cpp:585-594 turns it back into '\n'), so a
  // typed newline is sent as '|' and the round trip is lossless. The value must fit owner_info[120].
  char   value[kOwnerMax + 1];
  size_t o = 0;
  for (const char* p = owner; *p; ++p) {
    if (o >= (size_t)kOwnerMax) return 0;
    value[o++] = (*p == '\n') ? '|' : *p;
  }
  value[o] = '\0';
  if (o == 0) return 0;
  const int n = snprintf(out, cap, "set owner.info %s", value);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliLat(char* out, size_t cap, int32_t latE6) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (latE6 < -90000000 || latE6 > 90000000) return 0;
  char deg[24];
  degreesText(deg, sizeof(deg), latE6);
  const int n = snprintf(out, cap, "set lat %s", deg);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliLon(char* out, size_t cap, int32_t lonE6) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (lonE6 < -180000000 || lonE6 > 180000000) return 0;
  char deg[24];
  degreesText(deg, sizeof(deg), lonE6);
  const int n = snprintf(out, cap, "set lon %s", deg);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliPosition(char* out, size_t cap, int32_t latE6, int32_t lonE6) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  char a[40], b[40];
  if (cliLat(a, sizeof(a), latE6) == 0 || cliLon(b, sizeof(b), lonE6) == 0) return 0;
  const int n = snprintf(out, cap, "%s \xC2\xB7 %s", a, b);   // two lines, cliRadioApply's separator
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliRepeat(char* out, size_t cap, bool on) {
  if (out == nullptr || cap == 0) return 0;
  const int n = snprintf(out, cap, "set repeat %s", on ? "on" : "off");
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliPowerSaving(char* out, size_t cap, bool on) {
  if (out == nullptr || cap == 0) return 0;
  // NO "set " prefix: CommonCLI.cpp:439-462 matches "powersaving on"/"powersaving off" in the TOP-LEVEL handler.
  // "set powersaving on" would reach handleSetCmd and be answered "unknown config: powersaving on".
  const int n = snprintf(out, cap, "powersaving %s", on ? "on" : "off");
  return n > 0 && (size_t)n < cap ? n : 0;
}

int cliTx(char* out, size_t cap, unsigned dbm) {
  if (out == nullptr || cap == 0) return 0;
  unsigned v = dbm;
  if (v < (unsigned)kTxMin) v = kTxMin;
  if (v > (unsigned)kTxMax) v = kTxMax;
  const int n = snprintf(out, cap, "set tx %u", v);
  return n > 0 && (size_t)n < cap ? n : 0;
}

// -------------------------------------------------------------------------------------- the answer parsers
bool cliStripPrefix(const char* reply, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  // Every `get` reply CommonCLI writes is "> <value>" (CommonCLI.cpp:701-..., CommonRadioPrefs.cpp:137-139).
  if (reply == nullptr || reply[0] != '>') return false;
  const char* p = reply + 1;
  while (*p == ' ') p++;
  if (*p == '\0') return false;
  snprintf(out, cap, "%s", p);
  return true;
}

bool cliParseOnOff(const char* reply, bool* out) {
  if (reply == nullptr || out == nullptr) return false;
  const char* p = reply;
  if (*p == '>') {
    p++;
    while (*p == ' ') p++;
  }
  if (p[0] == 'o' && p[1] == 'n' && (p[2] == '\0' || p[2] == ' ' || p[2] == '-')) {
    *out = true;
    return true;
  }
  if (p[0] == 'o' && p[1] == 'f' && p[2] == 'f' && (p[3] == '\0' || p[3] == ' ' || p[3] == '-')) {
    *out = false;
    return true;
  }
  return false;
}

bool cliParseTx(const char* reply, uint8_t* out) {
  if (reply == nullptr || out == nullptr) return false;
  char buf[16];
  if (!cliStripPrefix(reply, buf, sizeof(buf))) return false;
  unsigned long w = 0, f = 0;
  int           fd = 0;
  if (!parseUnsigned(buf, &w, &f, &fd)) return false;
  if (fd != 0 || w > 255) return false;
  *out = (uint8_t)w;
  return true;
}

bool cliParseCoord(const char* reply, int32_t* e6) {
  if (reply == nullptr || e6 == nullptr) return false;
  char buf[24];
  if (!cliStripPrefix(reply, buf, sizeof(buf))) return false;
  bool        neg = false;
  const char* p = buf;
  if (*p == '-') {
    neg = true;
    p++;
  } else if (*p == '+') {
    p++;
  }
  unsigned long w = 0, f = 0;
  int           fd = 0;
  if (!parseUnsigned(p, &w, &f, &fd)) return false;
  if (w > 180) return false;
  // Scale to micro-degrees. StrHelper::ftoa prints up to 7 decimals with trailing zeros trimmed, so "46.25" and
  // "46.2500000" are both legal answers.
  int64_t v = (int64_t)w * 1000000;
  if (fd <= 6) {
    int64_t scale = 1;
    for (int i = fd; i < 6; i++) scale *= 10;
    v += (int64_t)f * scale;
  } else {
    int64_t div = 1;
    for (int i = 6; i < fd; i++) div *= 10;
    v += (int64_t)((f + (unsigned long)div / 2) / (unsigned long)div);   // round the 7th digit away
  }
  if (v > 180000000) return false;
  *e6 = (int32_t)(neg ? -v : v);
  return true;
}

bool cliParseTypedPosition(const char* typed, int32_t* latE6, int32_t* lonE6) {
  if (typed == nullptr || latE6 == nullptr || lonE6 == nullptr) return false;
  const char* comma = strchr(typed, ',');
  if (comma == nullptr) return false;
  char   lat[24], lon[24];
  const size_t la = (size_t)(comma - typed);
  if (la >= sizeof(lat)) return false;
  memcpy(lat, typed, la);
  lat[la] = '\0';
  const char* p = comma + 1;
  while (*p == ' ') p++;
  snprintf(lon, sizeof(lon), "%s", p);
  // Trim the spaces a finger or a paste can leave on either side.
  for (size_t i = strlen(lat); i > 0 && lat[i - 1] == ' '; i--) lat[i - 1] = '\0';
  for (size_t i = strlen(lon); i > 0 && lon[i - 1] == ' '; i--) lon[i - 1] = '\0';
  if (lat[0] == '\0' || lon[0] == '\0') return false;
  int32_t a = 0, b = 0;
  char    wrappedLat[32], wrappedLon[32];
  snprintf(wrappedLat, sizeof(wrappedLat), "> %s", lat);
  snprintf(wrappedLon, sizeof(wrappedLon), "> %s", lon);
  if (!cliParseCoord(wrappedLat, &a) || !cliParseCoord(wrappedLon, &b)) return false;
  if (a < -90000000 || a > 90000000) return false;
  if (b < -180000000 || b > 180000000) return false;
  *latE6 = a;
  *lonE6 = b;
  return true;
}

CliAnswer cliAnswerKind(const char* text) {
  if (text == nullptr) return CliAnswer::Unknown;
  if (text[0] == 'O' && text[1] == 'K') return CliAnswer::Ok;                    // "OK", "OK - ...", "OK, reboot..."
  if (text[0] == 'o' && text[1] == 'k' && (text[2] == '\0' || text[2] == ' ')) return CliAnswer::Ok;
  if (strncmp(text, "Error", 5) == 0) return CliAnswer::Error;                  // "Error, bad chars"
  if (strncmp(text, "ERR:", 4) == 0) return CliAnswer::Error;                   // "ERR: clock cannot go backwards"
  if (strncmp(text, "??:", 3) == 0) return CliAnswer::Unknown;                  // an unknown config
  return CliAnswer::Unknown;
}

// ---------------------------------------------------------------------------------- the confirm sheet's words
int confirmWriteText(char* out, size_t cap, const char* line, const char* current) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (line == nullptr || line[0] == '\0') return 0;
  // S516 (EST-473): "The repeater says nothing now" was wrong in BOTH cases it was produced. An ACTION row
  // (advert.zerohop / advert / clock sync) has no value to compare against at all, and a SETTING row that has not
  // been read yet has an UNKNOWN value — neither is "nothing", and the second one reads like an answer. The line
  // is the whole question then; a value clause is only added when there is a value.
  if (current == nullptr || current[0] == '\0') {
    const int n = snprintf(out, cap, "Send \"%s\"?", line);
    return n > 0 && (size_t)n < cap ? n : 0;
  }
  const int n = snprintf(out, cap, "Send \"%s\"? The repeater says %s now.", line, current);
  return n > 0 && (size_t)n < cap ? n : 0;
}

int confirmRebootText(char* out, size_t cap, const char* name) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (name == nullptr || name[0] == '\0') name = "the repeater";
  const int n = snprintf(out, cap, "Reboot %s? It is off the mesh for ~10 s.", name);
  return n > 0 && (size_t)n < cap ? n : 0;
}

// ------------------------------------------------------------------------ one line on the air at a time
void CliPending::begin(const char* l, uint8_t r, bool read, uint32_t nowMs, uint32_t deadline) {
  used = true;
  isRead = read;
  row = r;
  snprintf(line, sizeof(line), "%s", (l != nullptr) ? l : "");
  sentMs = nowMs;
  deadlineMs = nowMs + deadline;
}

}  // namespace rptr
}  // namespace om
