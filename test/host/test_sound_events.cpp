// SPDX-License-Identifier: MIT
// Host tests for S514/EST-450: the event -> L2 default mapping, Boot Off on this device, the 400 ms
// rate gate (time passed in, 32-bit wrap pinned), the echo/duplicate/restored refusal, the settings
// byte round trip and the L2's kRedirect rule for a retired/unknown id.
//
// The synth itself is NOT retested here: the L2's `firmware/test/host/test_est369.cpp` already pins
// every waveform, edge at zero and headroom, and it compiles the same sound_synth.cpp this suite links.
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "sound_events.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
      ++g_failures;                                                       \
      return;                                                             \
    }                                                                     \
  } while (0)

#define CHECK_EQ(got, want)                                                          \
  do {                                                                               \
    ++g_checks;                                                                      \
    const long g_ = static_cast<long>(got);                                          \
    const long w_ = static_cast<long>(want);                                         \
    if (g_ != w_) {                                                                  \
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,      \
             #got, #want, g_, w_);                                                    \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

#define CHECK_STR(got, want)                                                         \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (strcmp((got), (want)) != 0) {                                                \
      printf("FAIL %s:%d  \"%s\" == \"%s\"\n", __FILE__, __LINE__, (got), (want));  \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

using tm::soundcfg::Event;
using tm::soundcfg::SoundSettings;

void test_event_defaults() {
  printf("test  event -> the L2's default sound; Boot Off here, the L2's Boot is Bell TK-TK\n");
  const SoundSettings s = tm::soundcfg::defaultSettings();
  CHECK(s.master);                                       // Sound On, the ticket's default
  CHECK_EQ(s.volume, 60);                                // the L2's own default (sound_events.cpp)
  CHECK_EQ(s.volume, tm::soundcfg::kDefaultVolume);

  CHECK_EQ(s.ids[(int)Event::Message], om::sound::defaultSound(om::sound::Event::Message));
  CHECK_EQ(s.ids[(int)Event::Dm], om::sound::defaultSound(om::sound::Event::Dm));
  CHECK_EQ(s.ids[(int)Event::Sent], om::sound::defaultSound(om::sound::Event::Sent));
  CHECK_EQ(s.ids[(int)Event::Error], om::sound::defaultSound(om::sound::Event::Error));
  CHECK_EQ(s.ids[(int)Event::NewNode], om::sound::defaultSound(om::sound::Event::NewNode));

  // Names, so a silent renumbering in the shared library is a red test rather than a wrong chime.
  CHECK_STR(tm::soundcfg::choiceName(s.ids[(int)Event::Message]), "Soft Hi");
  CHECK_STR(tm::soundcfg::choiceName(s.ids[(int)Event::Dm]), "Wood Warm");
  CHECK_STR(tm::soundcfg::choiceName(s.ids[(int)Event::Sent]), "Soft Tick");
  CHECK_STR(tm::soundcfg::choiceName(s.ids[(int)Event::NewNode]), "Soft Lo");
  CHECK_STR(tm::soundcfg::choiceName(s.ids[(int)Event::Error]), "Bell Fall");

  // The L2's own Boot default is the TK-TK phrase; this device's default is deliberately Off (Filip
  // removed the boot advert, so a boot chime would announce nothing).
  CHECK_STR(tm::soundcfg::choiceName(om::sound::defaultSound(om::sound::Event::Boot)), "Bell TK-TK");
  CHECK_EQ(s.ids[(int)Event::Boot], om::sound::kSoundOff);
  CHECK_STR(tm::soundcfg::choiceName(s.ids[(int)Event::Boot]), "Off");

  CHECK_STR(tm::soundcfg::eventKey(Event::Message), "message");
  CHECK_STR(tm::soundcfg::eventKey(Event::Dm), "dm");
  CHECK_STR(tm::soundcfg::eventKey(Event::NewNode), "newnode");
  CHECK_STR(tm::soundcfg::eventLabel(Event::Dm), "Direct message");
}

void test_rate_gate() {
  printf("test  400 ms rate gate, exact edges and the 32-bit wrap\n");
  CHECK_EQ(tm::soundcfg::kSoundMinGapMs, 400);
  CHECK(!tm::soundcfg::rateDue(1000, 1399));
  CHECK(tm::soundcfg::rateDue(1000, 1400));
  CHECK(tm::soundcfg::rateDue(1000, 1401));
  CHECK(!tm::soundcfg::rateDue(1000, 1000));

  // 0xFFFFFF00 + 400 wraps to 0x00000090; 399 wraps to 0x0000008F.
  CHECK(!tm::soundcfg::rateDue(0xFFFFFF00u, 0x0000008Fu));
  CHECK(tm::soundcfg::rateDue(0xFFFFFF00u, 0x00000090u));
}

void test_never_for_echo_duplicate_restored() {
  printf("test  no sound for our echo, a duplicate, a restored line; New node follows the notice\n");
  SoundSettings s = tm::soundcfg::defaultSettings();
  CHECK(tm::soundcfg::shouldPlay(s, Event::Message, false, false, false, true));
  CHECK(!tm::soundcfg::shouldPlay(s, Event::Message, true, false, false, true));
  CHECK(!tm::soundcfg::shouldPlay(s, Event::Message, false, true, false, true));
  CHECK(!tm::soundcfg::shouldPlay(s, Event::Message, false, false, true, true));
  CHECK(!tm::soundcfg::shouldPlay(s, Event::Message, true, true, true, true));

  // The ticket: the new-node sound uses the SAME condition as the notice.
  CHECK(tm::soundcfg::shouldPlay(s, Event::NewNode, false, false, false, true));
  CHECK(!tm::soundcfg::shouldPlay(s, Event::NewNode, false, false, false, false));

  // Master Off silences everything; Off silences one event; Boot is Off by default.
  s.master = false;
  CHECK(!tm::soundcfg::shouldPlay(s, Event::Error, false, false, false, true));
  s.master = true;
  s.ids[(int)Event::Error] = om::sound::kSoundOff;
  CHECK(!tm::soundcfg::shouldPlay(s, Event::Error, false, false, false, true));
  CHECK(!tm::soundcfg::shouldPlay(tm::soundcfg::defaultSettings(), Event::Boot, false, false, false,
                                  true));
}

void test_settings_round_trip() {
  printf("test  settings byte round trip, short/long/wrong-version refusal, volume clamp\n");
  SoundSettings a = tm::soundcfg::defaultSettings();
  a.master        = false;
  a.volume        = 30;
  tm::soundcfg::setChoice(a, Event::Message, om::sound::libraryId(3));   // a current id
  tm::soundcfg::setChoice(a, Event::NewNode, om::sound::kSoundOff);

  uint8_t buf[32];
  CHECK_EQ(tm::soundcfg::encodeSettings(a, buf, sizeof(buf)), tm::soundcfg::kSettingsBytes);
  CHECK_EQ(tm::soundcfg::kSettingsBytes, 9);   // version + master + volume + six event ids

  SoundSettings b;
  CHECK(tm::soundcfg::decodeSettings(buf, tm::soundcfg::kSettingsBytes, &b));
  CHECK_EQ(b.master, a.master);
  CHECK_EQ(b.volume, 30);
  for (int i = 0; i < tm::soundcfg::kEventCount; ++i) CHECK_EQ(b.ids[i], a.ids[i]);

  // A buffer one byte short is not written at all.
  uint8_t tiny[8];
  memset(tiny, 0xA5, sizeof(tiny));
  CHECK_EQ(tm::soundcfg::encodeSettings(a, tiny, sizeof(tiny)), 0);
  CHECK_EQ(tiny[0], 0xA5);

  SoundSettings untouched = tm::soundcfg::defaultSettings();
  SoundSettings before    = untouched;
  CHECK(!tm::soundcfg::decodeSettings(buf, tm::soundcfg::kSettingsBytes - 1, &untouched));
  CHECK_EQ(untouched.volume, before.volume);
  CHECK(!tm::soundcfg::decodeSettings(buf, tm::soundcfg::kSettingsBytes + 1, &untouched));

  uint8_t bad[16];
  memcpy(bad, buf, tm::soundcfg::kSettingsBytes);
  bad[0] = (uint8_t)(tm::soundcfg::kSettingsVersion + 1);
  CHECK(!tm::soundcfg::decodeSettings(bad, tm::soundcfg::kSettingsBytes, &untouched));

  memcpy(bad, buf, tm::soundcfg::kSettingsBytes);
  bad[2] = 200;   // a corrupt volume: clamped, never 200
  CHECK(tm::soundcfg::decodeSettings(bad, tm::soundcfg::kSettingsBytes, &b));
  CHECK_EQ(b.volume, 100);
}

void test_unknown_id_redirect() {
  printf("test  a retired id resolves through the L2's kRedirect, an unknown one falls back\n");
  SoundSettings s = tm::soundcfg::defaultSettings();
  tm::soundcfg::setChoice(s, Event::Message, 0);   // a retired EST-361 id
  CHECK_EQ(s.ids[(int)Event::Message], om::sound::resolveSound(0));
  CHECK_STR(tm::soundcfg::choiceName(s.ids[(int)Event::Message]), "Bell TK-TK");

  tm::soundcfg::setChoice(s, Event::Message, 3);   // another retired id
  CHECK_EQ(s.ids[(int)Event::Message], om::sound::resolveSound(3));

  tm::soundcfg::setChoice(s, Event::Message, 200);   // not an id at all
  CHECK_EQ(s.ids[(int)Event::Message], tm::soundcfg::defaultSoundFor(Event::Message));

  tm::soundcfg::setChoice(s, Event::Message, om::sound::kSoundOff);
  CHECK_EQ(s.ids[(int)Event::Message], om::sound::kSoundOff);

  // The same rule must run when a blob comes back: a stored 200 must not reach the synth.
  SoundSettings enc = tm::soundcfg::defaultSettings();
  uint8_t buf[tm::soundcfg::kSettingsBytes];
  tm::soundcfg::encodeSettings(enc, buf, sizeof(buf));
  buf[3 + (int)Event::Message] = 200;
  SoundSettings loaded;
  CHECK(tm::soundcfg::decodeSettings(buf, sizeof(buf), &loaded));
  CHECK_EQ(loaded.ids[(int)Event::Message], tm::soundcfg::defaultSoundFor(Event::Message));
}

void test_volume_steps() {
  printf("test  volume walks 0..100 in tens and wraps\n");
  CHECK_EQ(tm::soundcfg::nextVolume(0), 10);
  CHECK_EQ(tm::soundcfg::nextVolume(50), 60);
  CHECK_EQ(tm::soundcfg::nextVolume(90), 100);
  CHECK_EQ(tm::soundcfg::nextVolume(100), 0);
}

}  // namespace


// S514 (Claude): the L2's volume curve, through the SAME arithmetic the device runs: the BSP's x0.70
// (badge_bsp_audio.c:100) and es8156_set_volume_percentage's clamp + lroundf (es8156.c:800-815), reproduced here.
static int codecRegisterFor(float bsp_pct) {
  float v = bsp_pct * 0.70f;
  if (v < 0.0f) v = 0.0f;
  if (v > 100.0f) v = 100.0f;
  const float db  = -95.5f + (v / 100.0f) * (32.0f - (-95.5f));
  long        reg = lroundf((db - (-95.5f)) / 0.5f);
  if (reg < 0) reg = 0;
  if (reg > 255) reg = 255;
  return (int)reg;
}

void test_volume_curve() {
  printf("test  volume: the L2 curve (60 %% = 0 dB) reaches the codec exactly\n");
  CHECK_EQ((int)tm::soundcfg::volumeRegister(0), 0x00);     // mute
  CHECK_EQ((int)tm::soundcfg::volumeRegister(1), 0x61);
  CHECK_EQ((int)tm::soundcfg::volumeRegister(60), 0xBF);    // 0 dB, as on the L2
  CHECK_EQ((int)tm::soundcfg::volumeRegister(100), 0xFF);   // +32 dB, the codec's top
  CHECK_EQ((int)tm::soundcfg::volumeRegister(250), 0xFF);   // clamped
  for (int p = 0; p <= 100; p++) {
    const int want = tm::soundcfg::volumeRegister((uint8_t)p);
    CHECK_EQ(codecRegisterFor(tm::soundcfg::bspVolumeForRegister((uint8_t)want)), want);
  }
  // The old path handed the percentage straight to the BSP: 100 % landed 38 dB below the L2's 100 %.
  CHECK(codecRegisterFor(100.0f) < tm::soundcfg::volumeRegister(100));
}

int main() {
  test_event_defaults();
  test_volume_curve();
  test_rate_gate();
  test_never_for_echo_duplicate_restored();
  test_settings_round_trip();
  test_unknown_id_redirect();
  test_volume_steps();
  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
