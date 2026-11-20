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

// ====================================================================================== part 2a: settings
// The SAFE settings EST-470 changes, and only those. Every LINE below is the one the repeater's own CLI parses
// (firmware/lib/meshcore/helpers/CommonCLI.cpp and CommonRadioPrefs.cpp, both read, never guessed) and every
// REPLY format is what that same handler answers. Radio, path hash, loop detect, advert timers, flood.max,
// regions, silent mode and a free-form line are 2b.
//
// Upstream:  line                      handler                        answer
//   set name <x>                       CommonCLI.cpp:531-537          "OK" / "Error, bad chars"
//   set owner.info <x>                 CommonCLI.cpp:585-594          "OK"  ('|' becomes a newline on the repeater)
//   set lat <x> / set lon <x>          CommonCLI.cpp:557-563          "OK"
//   set repeat on|off                  CommonCLI.cpp:538-542          "OK - repeat is now ON" / "... OFF"
//   powersaving on|off                 CommonCLI.cpp:439-457          "on - After 2 minutes" (ESP32) / "off"  [NO "set "]
//   set tx <n>                         CommonRadioPrefs.cpp:141-145   "OK"
//   advert.zerohop                     CommonCLI.cpp:203-206          "OK - zerohop advert sent"
//   advert                             CommonCLI.cpp:207-210          "OK - Advert sent"
//   clock sync                         CommonCLI.cpp:211-221          "OK - clock set: HH:MM - d/m/yyyy UTC"
//   reboot                             CommonCLI.cpp:199-200          NO answer (board reboot() never returns)
// Reads: `get <cfg>` answers "> <value>" (CommonCLI.cpp:701-...); `powersaving` answers a bare "on"/"off"
// (:457-462); `get tx` answers "> <dbm>" (CommonRadioPrefs.cpp:137-139).

constexpr int kOwnerMax = 119;    // NodePrefs::owner_info[120] (CommonCLI.h)
// The 23-byte name cap: an advert's app_data is MAX_ADVERT_DATA_SIZE = 32 bytes (MeshCore.h:12) and a position
// takes 1 flag byte + lat(4) + lon(4) = 9 of them (helpers/AdvertDataHelpers.cpp encodeTo), so a node that shares
// its position keeps 32 - 9 = 23 for the name. Without a shared position the stored name may use 31
// (NodePrefs::node_name[32], the L2's kNameMax).
constexpr int kNameSharedMax = 23;
constexpr uint8_t kTxMin = 1;     // the CLI's 1-22 dBm; upstream does NOT clamp `set tx` (CommonRadioPrefs.cpp
constexpr uint8_t kTxMax = 22;    // stores atoi() straight), so the range is ours to enforce.

// One row of the SETTINGS section, in the order the page draws them.
enum class SetRow : uint8_t {
  Name = 0,
  Owner,
  Position,
  Repeat,
  PowerSave,
  TxPower,
  AdvertLocal,
  AdvertFlood,
  ClockSync,
  Reboot,
  Count
};
constexpr int kSetRowCount = (int)SetRow::Count;

const char* setRowLabel(int row);       // "Name", "Owner info", ... ("" out of range)
bool        setRowIsText(int row);      // Name/Owner/Position: the shared text field
bool        setRowIsToggle(int row);    // Repeat/Power save: Enter flips it (behind the confirm)
bool        setRowIsStepper(int row);   // TX power: `+`/`-` move the draft, Enter opens the confirm
bool        setRowIsAction(int row);    // Advert local/flood, Clock sync, Reboot
const char* setRowReadCmd(int row);     // the first `get` line that fills this row, nullptr for an action
const char* setRowReadCmd2(int row);    // Position's second line ("get lon"), nullptr otherwise
const char* setRowActionCmd(int row);   // the line Enter sends for an action row, nullptr otherwise

// What the repeater has ANSWERED so far. `*Ok == false` means "not read yet" and the page draws "…", never a
// placeholder. reset() is what a fresh login (or a logout) starts from.
struct Settings {
  bool    nameOk = false;
  char    name[kNameMax + 1] = "";
  bool    ownerOk = false;
  char    owner[kOwnerMax + 1] = "";
  bool    latOk = false;
  int32_t latE6 = 0;
  bool    lonOk = false;
  int32_t lonE6 = 0;
  bool    repeatOk = false;
  bool    repeat = true;
  bool    psOk = false;
  bool    powerSave = false;
  bool    txOk = false;
  uint8_t tx = 0;
  void    reset() { *this = Settings(); }
  bool    positionOk() const { return latOk && lonOk; }
};

// The row's value as the glass shows it. False = nothing read yet (the caller draws "…"); `out` is left empty.
// Position is ONE row: "46.25000, 14.40000" only when BOTH halves have been answered.
bool settingsRowValue(const Settings& s, int row, char* out, size_t cap);

// The read-after-login script, in order: the seven `get` lines above that a repeater really answers. A row is
// read again after every change (and the whole script on `r`), so no row ever keeps a value the finger made up.
constexpr int kSetReadCount = 7;
const char* setReadCmd(int i);                 // i < 0 or >= kSetReadCount -> nullptr
int         setReadRow(const char* cmd);       // the SetRow a read fills, -1 when it is not one of ours

// Read one answer into its row, keyed by the LINE that asked (not by the row: lat and lon share the Position
// row, and only the line says which half this answer is). Returns false when the text is not that line's
// format (a stray answer, an error line), in which case the row keeps what it had.
bool settingsApplyRead(Settings* s, const char* read_cmd, const char* reply);

// ------------------------------------------------------------------------------------- the CLI line builders
// Each writes the exact line and returns its length, or 0 for a refused argument (out left empty).
int cliName(char* out, size_t cap, const char* name, bool positionShared);
int cliOwner(char* out, size_t cap, const char* owner);
int cliLat(char* out, size_t cap, int32_t latE6);                       // "set lat 46.25000"
int cliLon(char* out, size_t cap, int32_t lonE6);                       // "set lon 14.40000"
int cliPosition(char* out, size_t cap, int32_t latE6, int32_t lonE6);   // both, " · " between (as cliRadioApply)
int cliRepeat(char* out, size_t cap, bool on);
int cliPowerSaving(char* out, size_t cap, bool on);
int cliTx(char* out, size_t cap, unsigned dbm);                         // clamped to kTxMin..kTxMax
int degreesText(char* out, size_t cap, int32_t e6);                     // "46.25000" (five decimals)

constexpr const char* kCliAdvertLocal = "advert.zerohop";
constexpr const char* kCliAdvertFlood = "advert";
constexpr const char* kCliClockSync = "clock sync";
constexpr const char* kCliClockRead = "clock";
constexpr const char* kCliReboot = "reboot";
constexpr const char* kCliGetName = "get name";
constexpr const char* kCliGetOwner = "get owner.info";
constexpr const char* kCliGetLat = "get lat";
constexpr const char* kCliGetLon = "get lon";
constexpr const char* kCliGetRepeat = "get repeat";
constexpr const char* kCliPowerSavingRead = "powersaving";
constexpr const char* kCliGetTx = "get tx";
// 2025-01-01. Anything earlier is not a real clock: 0 is an unset RTC and 2024 is MeshCore's restart clock
// (the L2's kClockRealSince). The automatic clock sync after a login is skipped while ours is below this, because
// `clock sync` would only answer "ERR: clock cannot go backwards" (CommonCLI.cpp:211-221).
constexpr uint32_t kClockRealSince = 1735689600u;

// -------------------------------------------------------------------------------------- the answer parsers
// A `set` answers "OK...", "ok", an "Error..." or an "ERR:..." line, and `??: <cfg>` is an unknown config.
enum class CliAnswer : uint8_t { Ok = 0, Error, Unknown };
CliAnswer cliAnswerKind(const char* text);
bool      cliStripPrefix(const char* reply, char* out, size_t cap);   // "> value" -> "value"
bool      cliParseOnOff(const char* reply, bool* out);                // "> on" / "on" / "off"
bool      cliParseTx(const char* reply, uint8_t* out);                // "> 14"
bool      cliParseCoord(const char* reply, int32_t* e6);              // "> 46.25000"
// "lat, lon" as TYPED (the Position row's one field). Accepts an optional space after the comma and refuses an
// empty half or an out-of-range value, so a typo never becomes a command.
bool cliParseTypedPosition(const char* typed, int32_t* latE6, int32_t* lonE6);

// ---------------------------------------------------------------------------------- the confirm sheet's words
// Every change names the exact line that will go on the air and the value the row shows now (EST-470 item 3).
// S516 (EST-473): when there IS no value — an action row (advert, clock sync), or a setting not read yet — the
// question is the line alone; it never claims the repeater "says nothing", which is a different statement.
int confirmWriteText(char* out, size_t cap, const char* line, const char* current);
int confirmRebootText(char* out, size_t cap, const char* name);   // "Reboot X? It is off the mesh for ~10 s."

// What CliPending::tick() saw. Idle means nothing is in flight, so the next line may go now; Waiting means the
// line is still inside its own deadline or its late window (the next line is held); TimedOut means this call
// opened the late window; Closed means the late window ended with no answer and the slot is free again.
enum class CliEvent : uint8_t { Idle = 0, Waiting, TimedOut, Closed };

// ------------------------------------------------------------------------ one line on the air at a time
// A queue of ONE pending CLI line (EST-470 item 4). Nothing else is sent while `used`; a lost answer changes
// nothing on the repeater's side, so every row keeps the value it was READ as.
//
// S516 (EST-474): meeting the deadline does NOT release the next line. The line stays `used` for ONE MORE
// DEADLINE (the same estimate it was sent with, its own `deadlineMs - sentMs`) because the repeater's answer may
// still be on its way and must not land in the NEXT line's row. While that window is open every send is refused,
// which is exactly what holds the next line; a late answer still finds THIS line's `line`/`row`/`isRead`, so the
// shell routes it to the row that asked. When the window closes with nothing the line is cleared like a plain
// timeout ("no answer") and the held line goes out. One window per timed-out line, never stacked (the slot is
// released before the next line is sent); clear() — logout, leaving the repeater — drops the window too.
struct CliPending {
  bool     used = false;
  bool     isRead = false;   // a read (the answer becomes a value) or a change (the answer becomes a notice)
  uint8_t  row = 0xFF;       // the SetRow this answer belongs to
  char     line[64] = "";
  uint32_t sentMs = 0;
  uint32_t deadlineMs = 0;
  bool     late = false;     // S516: its deadline passed and the late-answer window is open

  void begin(const char* l, uint8_t r, bool read, uint32_t nowMs, uint32_t deadline);
  void clear() { *this = CliPending(); }
  // S516: the one place time moves this line. Idle (nothing in flight), Waiting (still in its own deadline or in
  // its late window), TimedOut (this call opened the late window — the caller drops a paired second half and
  // keeps every row as it was), Closed (the late window ended with no answer — the line is cleared, so the line
  // held behind it may go NOW). One call per pass; the window is never opened twice and never stacked.
  CliEvent tick(uint32_t nowMs);
  // The "one line at a time" rule: false while a line waits for its answer OR its late window is open.
  bool canSend() const { return !used; }
};

}  // namespace rptr
}  // namespace om
