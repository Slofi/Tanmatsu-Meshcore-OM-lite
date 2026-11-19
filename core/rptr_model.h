// SPDX-License-Identifier: MIT
// RPTR part 1 (S514/EST-453): the parts of repeater discovery, login and status that are plain arithmetic or
// plain text, ported from the L2 (`firmware/src/rptr_model.*`). No Arduino, no IDF, no drawing: the host test
// proves the list, the login state machine with its deadline, the status decode and the units.
//
// The shell (main/rptr_link.cpp) owns the radio and NVS; main/rptr_page.cpp owns the glass. This file owns only
// what can be checked on a laptop.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace rptr {

// --------------------------------------------------------------------------------------------- limits
constexpr int kMaxFound = 16;        // discovery answers kept per scan
constexpr int kPasswordMax = 15;     // MeshCore's sendLogin() truncates at 15 characters (BaseChatMesh.cpp:612)
constexpr int kNameMax = 31;         // NodePrefs::node_name[32]
constexpr int kPubKeyBytes = 32;     // MeshCore's full public key

// Discovery keeps listening this long after the request goes out. The request is zero-hop, so every
// repeater in range answers once with a random delay (rptr_model.h of the L2, EST-358).
constexpr uint32_t kScanWindowMs = 20000;
// The login deadline is upstream's own estimate plus this margin (the L2's kLoginExtraMs).
constexpr uint32_t kLoginExtraMs = 2500;
// A status request's deadline never drops under this floor (the L2's EST-364 rule).
constexpr uint32_t kStatusMinMs = 8000;

constexpr uint32_t requestDeadlineMs(uint32_t estMs, uint32_t extraMs, uint32_t floorMs) {
  const uint32_t wanted = estMs + extraMs;
  return wanted > floorMs ? wanted : floorMs;
}

// ------------------------------------------------------------------------------------------ password
// The admin password is taken literally: printable ASCII only. The page's field uses this filter, so no key can
// put a byte on the air that the repeater would never match.
bool passwordChar(char c);
int passwordFilter(const char* src, char* out, size_t cap);

// ---------------------------------------------------------------------------------------------- found
// One discovery answer. MeshCore's control-data DISCOVER_RESP carries the tag, the node type, the SNR the
// repeater heard us at (x4) and its public key (6 bytes or 32). The SNR we heard it at comes from the radio.
struct Found {
  uint8_t prefix[6] = {};            // what the row is keyed on
  uint8_t key[kPubKeyBytes] = {};    // the full key when the answer carried it
  uint8_t keyLen = 0;                // 32 for a full-key answer, 6/8 for a prefix-only one
  char name[32] = "";                // the contact name, or "" when this repeater is not in the table
  bool inContacts = false;           // already in our contact table
  bool hasSnrIn = false;             // the repeater told us how it heard us
  int8_t snrIn4 = 0;                 // x4, MeshCore's unit
  bool hasSnrOut = false;            // we measured how we heard it
  int8_t snrOut4 = 0;
  uint8_t hops = 0;                  // path length of the answer (0: it came straight to us)
  uint32_t heardMs = 0;              // millis() when it arrived
};

struct FoundList {
  Found rows[kMaxFound];
  int count = 0;
  bool listening = false;
  uint32_t startedMs = 0;
  uint32_t tag = 0;          // the random tag the request carried; answers must echo it
  uint32_t serial = 0;       // bumps on every change, so the shell knows to redraw

  void begin(uint32_t tagSeed, uint32_t nowMs);
  // One answer. Returns the row index (an existing prefix updates in place), or -1 when the list is full.
  int addAnswer(const uint8_t* key, int keyLen, const char* name, bool inContacts, bool hasIn, int8_t in4,
                bool hasOut, int8_t out4, uint8_t hops, uint32_t nowMs);
  // The scan window closes: listening becomes false and one serial bump follows.
  void tick(uint32_t nowMs);
  int indexOf(const uint8_t* prefix6) const;
  // "N found" for the header ("listening, 2 found" while the window is open).
  void stateText(char* out, size_t cap) const;
};

// A discovery answer as the wire parser hands it over (see rptr_wire.h).
struct ScanAnswer {
  uint32_t tag = 0;
  uint8_t nodeType = 0;
  int8_t snrIn4 = 0;
  const uint8_t* key = nullptr;
  int keyLen = 0;
};

// ---------------------------------------------------------------------------------------------- login
// MeshCore's login answer, decoded. The repeater's reply is
//   [0]=RESP_SERVER_LOGIN_OK(0), [1]=keep-alive/16 (0 since v7), [2]=is_admin, [3]=permissions,
//   [4..7]=random, [8]=FIRMWARE_VER_LEVEL   (simple_repeater/MyMesh.cpp handleLoginReq)
// and the 4-byte timestamp before it has already been stripped by the caller (rptr_wire.h).
// A legacy repeater answers the two bytes "OK" instead. A WRONG password gets NO answer at all.
struct LoginAnswer {
  bool ok = false;
  bool legacy = false;
  bool admin = false;
  uint8_t permissions = 0;
  uint8_t fwLevel = 0;
  uint32_t clock = 0;        // the repeater's own clock, from the answer's first four bytes
  bool clockValid = false;
};
bool loginParse(const uint8_t* buf, int len, LoginAnswer* out);

enum class LoginState : uint8_t { Idle = 0, Sending, Admin, Guest, Refused, NoAnswer };
const char* loginStateText(LoginState s);

struct LoginMachine {
  LoginState state = LoginState::Idle;
  uint32_t sentMs = 0;
  uint32_t deadlineMs = 0;
  uint32_t rttMs = 0;
  bool haveRtt = false;
  LoginAnswer answer;

  void begin(uint32_t nowMs, uint32_t estTimeoutMs);
  void succeed(const LoginAnswer& a, uint32_t nowMs);
  void refuse();
  // True when the state changed (a timeout), so the caller redraws once.
  bool tick(uint32_t nowMs);
  bool busy() const { return state == LoginState::Sending; }
  bool loggedIn() const { return state == LoginState::Admin || state == LoginState::Guest; }
  void reset();
};

// ---------------------------------------------------------------------------------------------- status
// RepeaterStats, as simple_repeater sends it (MyMesh.h of that example, 56 bytes, naturally packed):
//   uint16 batt_mv, uint16 tx_queue, int16 noise_floor, int16 last_rssi,
//   uint32 recv, sent, air_time, up_time, sent_flood, sent_direct, recv_flood, recv_direct,
//   uint16 err_events, int16 last_snr (x4), uint16 direct_dups, uint16 flood_dups,
//   uint32 rx_air_time, uint32 recv_errors.
// The status reply is [timestamp 4][stats 56]; the caller strips the timestamp (rptr_wire.h).
struct Status {
  bool valid = false;
  uint32_t atMs = 0;
  uint16_t battMv = 0;
  uint16_t txQueue = 0;
  int16_t noiseFloor = 0;
  int16_t lastRssi = 0;
  uint32_t rxPackets = 0;
  uint32_t txPackets = 0;
  uint32_t airtimeSec = 0;
  uint32_t uptimeSec = 0;
  uint32_t sentFlood = 0;
  uint32_t sentDirect = 0;
  uint32_t recvFlood = 0;
  uint32_t recvDirect = 0;
  uint16_t errEvents = 0;
  int16_t lastSnr4 = 0;
  uint16_t directDups = 0;
  uint16_t floodDups = 0;
  uint32_t rxAirtimeSec = 0;
  uint32_t rxCrcErrors = 0;
  uint8_t fwLevel = 0;       // from the login answer, not the status reply
  uint32_t clock = 0;        // the repeater's clock from the login answer
  bool clockValid = false;
  uint32_t rttMs = 0;
  bool haveRtt = false;
};
constexpr int kRepeaterStatsBytes = 56;
bool statusParse(const uint8_t* buf, int len, Status* out);

// ------------------------------------------------------------------------------------------ formatting
// The L2's units on the glass, one home each. Every string is NUL-terminated; the return value is the length.
int uptimeText(char* out, size_t cap, uint32_t seconds);           // "3d 04:12", "4h 02m", "12m 30s"
int batteryVoltsText(char* out, size_t cap, uint16_t milliVolts);  // "4.11 V" or "n/a"
int batteryPercent(uint16_t milliVolts);                           // -1 when implausible
int snrText(char* out, size_t cap, int8_t snr4);                   // "8.25 dB"
constexpr uint16_t kBatteryMinMv = 2500;
constexpr uint16_t kBatteryMaxMv = 4500;
bool batteryPlausible(uint16_t milliVolts);

}  // namespace rptr
}  // namespace om
