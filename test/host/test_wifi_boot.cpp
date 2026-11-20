// SPDX-License-Identifier: MIT
// S516/EST-480: the "Wi-Fi at boot" rule (core/wifi_boot.h).
//
// What is pinned, exactly as the ticket states it:
//   1. DEFAULT Off, and only a stored 1 is On (absent/corrupt -> Off);
//   2. Off at OM Lite's start: present 1 -> write 0; present 0 -> NO write; absent -> NO write
//      (the launcher already reads a missing key as off);
//   3. On at OM Lite's start: NO write, whatever the key says;
//   4. switching On writes 1 ONCE; switching Off writes 0 ONCE;
//   5. the two NVS names ("mcset"/"wifiboot" for us, "system"/"ntp" for the launcher) are pinned here, so a
//      typo in main.cpp cannot silently point the rule at another key.
//
// Build/run: sh test/host/run.sh (g++ -std=c++17 -Wall -Wextra -Werror).
#include <stdio.h>
#include <string.h>

#include "wifi_boot.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                           \
  do {                                                        \
    ++g_checks;                                               \
    if (!(cond)) {                                            \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
      ++g_failures;                                           \
      return;                                                 \
    }                                                         \
  } while (0)

#define CHECK_EQ(got, want)                                                          \
  do {                                                                               \
    ++g_checks;                                                                      \
    const long g_ = static_cast<long>(got);                                          \
    const long w_ = static_cast<long>(want);                                         \
    if (g_ != w_) {                                                                  \
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,       \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

#define CHECK_STR(got, want)                                                        \
  do {                                                                              \
    ++g_checks;                                                                     \
    if (strcmp((got), (want)) != 0) {                                               \
      printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (got), (want));  \
      ++g_failures;                                                                 \
      return;                                                                       \
    }                                                                               \
  } while (0)

// ------------------------------------------------------------------------ 1. default + stored mapping
void test_default_and_mapping() {
  printf("test  default is Off; only a stored 1 is On; absent/corrupt reads as Off\n");
  CHECK_EQ((int)tm::wifiboot::kDefaultMode, (int)tm::wifiboot::Mode::Off);
  CHECK_EQ((int)tm::wifiboot::modeFromStored(0), (int)tm::wifiboot::Mode::Off);
  CHECK_EQ((int)tm::wifiboot::modeFromStored(1), (int)tm::wifiboot::Mode::On);
  CHECK_EQ((int)tm::wifiboot::modeFromStored(2), (int)tm::wifiboot::Mode::Off);    // corrupt -> default
  CHECK_EQ((int)tm::wifiboot::modeFromStored(0xFF), (int)tm::wifiboot::Mode::Off); // corrupt -> default
  CHECK_EQ(tm::wifiboot::storedFromMode(tm::wifiboot::Mode::Off), 0);
  CHECK_EQ(tm::wifiboot::storedFromMode(tm::wifiboot::Mode::On), 1);
  CHECK_STR(tm::wifiboot::modeLabel(tm::wifiboot::Mode::Off), "Off");
  CHECK_STR(tm::wifiboot::modeLabel(tm::wifiboot::Mode::On), "On");
  CHECK_EQ((int)tm::wifiboot::modeToggle(tm::wifiboot::Mode::Off), (int)tm::wifiboot::Mode::On);
  CHECK_EQ((int)tm::wifiboot::modeToggle(tm::wifiboot::Mode::On), (int)tm::wifiboot::Mode::Off);
  CHECK_EQ(tm::wifiboot::modeToggle(tm::wifiboot::modeToggle(tm::wifiboot::Mode::Off)),
           (int)tm::wifiboot::Mode::Off);
}

// ---------------------------------------------------------------- 2. the every-start rule (Off) and no-op (On)
void test_start_rule() {
  printf("test  Off at start: 1 -> write 0, 0 -> no write, absent -> no write; On -> no write at start\n");
  using tm::wifiboot::Mode;
  using tm::wifiboot::NtpWrite;

  // Off: the one case the ticket names first.
  CHECK_EQ((int)tm::wifiboot::startAction(Mode::Off, true, 1), (int)NtpWrite::Zero);
  // Off: already 0 -> no write at all.
  CHECK_EQ((int)tm::wifiboot::startAction(Mode::Off, true, 0), (int)NtpWrite::Nothing);
  // Off: any non-zero value is not "already 0" and is forced to 0 (the launcher tests == 1, but the key is
  // ours to hand back clean; 2 is corrupt and must not stay).
  CHECK_EQ((int)tm::wifiboot::startAction(Mode::Off, true, 2), (int)NtpWrite::Zero);
  CHECK_EQ((int)tm::wifiboot::startAction(Mode::Off, true, 0xFF), (int)NtpWrite::Zero);
  // Off: a missing key already reads as off in the launcher; leave it absent rather than create it.
  CHECK_EQ((int)tm::wifiboot::startAction(Mode::Off, false, 0), (int)NtpWrite::Nothing);

  // On: we never touch the launcher's key at start, whatever it says.
  CHECK_EQ((int)tm::wifiboot::startAction(Mode::On, true, 0), (int)NtpWrite::Nothing);
  CHECK_EQ((int)tm::wifiboot::startAction(Mode::On, true, 1), (int)NtpWrite::Nothing);
  CHECK_EQ((int)tm::wifiboot::startAction(Mode::On, false, 0), (int)NtpWrite::Nothing);

  // The value a write must carry: Zero -> 0, One -> 1 (and only a write carries a value at all).
  CHECK_EQ(tm::wifiboot::ntpValue(NtpWrite::Zero), 0);
  CHECK_EQ(tm::wifiboot::ntpValue(NtpWrite::One), 1);
}

// ------------------------------------------------------------------------------------ 3. the switch itself
void test_switch_rule() {
  printf("test  switching On writes 1 once; switching Off writes 0 once\n");
  using tm::wifiboot::Mode;
  using tm::wifiboot::NtpWrite;
  CHECK_EQ((int)tm::wifiboot::switchAction(Mode::On), (int)NtpWrite::One);
  CHECK_EQ((int)tm::wifiboot::switchAction(Mode::Off), (int)NtpWrite::Zero);
  CHECK_EQ(tm::wifiboot::ntpValue(tm::wifiboot::switchAction(Mode::On)), 1);
  CHECK_EQ(tm::wifiboot::ntpValue(tm::wifiboot::switchAction(Mode::Off)), 0);
}

// --------------------------------------------------------------------------- 4. the NVS contract, pinned
void test_nvs_names() {
  printf("test  our key is mcset/wifiboot; the launcher's is system/ntp; nothing else is named\n");
  CHECK_STR(tm::wifiboot::kNamespace, "mcset");
  CHECK_STR(tm::wifiboot::kKey, "wifiboot");
  CHECK_STR(tm::wifiboot::kLauncherNamespace, "system");
  CHECK_STR(tm::wifiboot::kLauncherKey, "ntp");
}

}  // namespace

int main() {
  test_default_and_mapping();
  test_start_rule();
  test_switch_rule();
  test_nvs_names();
  if (g_failures == 0) {
    printf("wifi-boot: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  printf("wifi-boot: %d/%d checks FAILED\n", g_failures, g_checks);
  return 1;
}
