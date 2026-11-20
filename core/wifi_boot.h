// SPDX-License-Identifier: MIT
// "Wi-Fi at boot" (S516/EST-480): the pure rule behind General's one WI-FI row.
//
// The stock launcher connects Wi-Fi at boot ONLY when NVS "system"/"ntp" (u8) is 1; a missing key reads as
// off, so every other connect is a user action (app store, OTA, the Wi-Fi menu, icons, the signature test).
// Source (read-only, the ticket's citation): tanmatsu-launcher @ 3ceac92, main/main.c:187 and main/ntp.c:22-49.
//
// That key is the LAUNCHER'S, not ours. Our choice lives in OUR namespace, next to the other General settings
// ("mcset"/"wifiboot", u8, 0 = Off / 1 = On), and is never derived from system/ntp: the two can disagree
// between our start and our next write, and the UI must show OUR stored choice, not the launcher's key.
//
// The rules, exactly as the ticket states them:
//   * DEFAULT Off.
//   * Off -> at EVERY start of OM Lite write 0, but ONLY when the key is present and not already 0 (read
//     first; a missing key already reads as off, so it is left absent). One log line either way is the shell's.
//   * On -> write 1 ONCE when switched; at start the key is left exactly as it is.
//   * Switching Off writes 0 once as well (the user asked for it), and the every-start rule then keeps it.
//   * No other "system" key is ever opened, read or written.
//
// Everything here is plain C++17: no IDF, no NVS. The host test drives these same functions.
#pragma once

#include <stdint.h>

namespace tm {
namespace wifiboot {

// The launcher's own switch (the one key we are allowed to touch, and only to make it say 0 or 1).
constexpr const char* kLauncherNamespace = "system";
constexpr const char* kLauncherKey       = "ntp";

// OUR setting, in the General settings' namespace (mcset holds heardto/saveheard/newnode/mapstart today).
constexpr const char* kNamespace = "mcset";
constexpr const char* kKey       = "wifiboot";

enum class Mode : uint8_t { Off = 0, On };

// Without a stored choice there is no boot Wi-Fi: Off (Filip, EST-480).
constexpr Mode kDefaultMode = Mode::Off;

// Only the stored 1 is On. An absent/corrupt value reads as the default (Off), the same shape as
// newnode/saveheard in mc_link.
constexpr Mode modeFromStored(uint8_t v) { return v == 1 ? Mode::On : Mode::Off; }
constexpr uint8_t storedFromMode(Mode m) { return m == Mode::On ? 1u : 0u; }

constexpr const char* modeLabel(Mode m) { return m == Mode::On ? "On" : "Off"; }
constexpr Mode modeToggle(Mode m) { return m == Mode::On ? Mode::Off : Mode::On; }

// What the shell must write to the LAUNCHER's key.
//   Nothing = do not open a write handle at all (the ticket's "write only on a difference").
enum class NtpWrite : uint8_t { Nothing = 0, Zero, One };

// At OM Lite's start. Off enforces 0 but only on a real difference; On never touches the key.
// `present == false` means nvs_get_u8 returned NOT_FOUND: the launcher already reads that as off, so a
// write would change nothing and is not made.
constexpr NtpWrite startAction(Mode stored, bool present, uint8_t ntp) {
  if (stored == Mode::On) return NtpWrite::Nothing;
  if (!present) return NtpWrite::Nothing;
  return ntp == 0 ? NtpWrite::Nothing : NtpWrite::Zero;
}

// The switch itself: On writes 1 once; Off writes 0 once (the every-start rule above keeps it at 0 after).
constexpr NtpWrite switchAction(Mode next) { return next == Mode::On ? NtpWrite::One : NtpWrite::Zero; }

// The value the launcher key must hold for a write action (valid only when the action is not Nothing).
constexpr uint8_t ntpValue(NtpWrite w) { return w == NtpWrite::One ? 1u : 0u; }

}  // namespace wifiboot
}  // namespace tm
