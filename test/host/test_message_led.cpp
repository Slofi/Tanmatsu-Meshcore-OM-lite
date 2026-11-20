// SPDX-License-Identifier: MIT
// S516/EST-477 host tests: the Tanmatsu LEDs' rule (core/message_led_rule.h).
//
// Time in, LED frame out. The failures that would be silent on the glass: automatic mode while a switch is Off,
// manual mode while both are On, the wrong register bits for a colour, a colour B or fade-hold bit set, the wrong
// power colour, a pixel written between fade steps (the frame must say nothing CHANGED), a locked device that
// lights anything but LED 3, and an exit that leaves the message register breathing for the launcher.
#include <stdio.h>
#include <string.h>

#include "message_led_rule.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,       \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

om::ui::LedIn in(bool locked, bool power, bool radio, uint32_t unread,
                 om::ui::LedColour colour = om::ui::kMessageColourDefault,
                 om::ui::PowerState power_state = om::ui::PowerState::Battery,
                 bool radio_up = true, bool message_ok = true, uint32_t preview_until_ms = 0) {
  om::ui::LedIn i;
  i.locked              = locked;
  i.power_on            = power;
  i.radio_on            = radio;
  i.unread              = unread;
  i.colour              = colour;
  i.power               = power_state;
  i.radio_up            = radio_up;
  i.message_register_ok = message_ok;
  i.preview_until_ms    = preview_until_ms;
  return i;
}

bool pixel(const om::ui::LedFrame& f, int i, int r, int g, int b) {
  return f.rgb[i][0] == r && f.rgb[i][1] == g && f.rgb[i][2] == b;
}

// ---- 1. the Settings values and the colour table ---------------------------------------------------------
void testSettingValues() {
  CHECK(strcmp(om::ui::ledColourLabel(om::ui::LedColour::Red), "Red") == 0);
  CHECK(strcmp(om::ui::ledColourLabel(om::ui::LedColour::Green), "Green") == 0);
  CHECK(strcmp(om::ui::ledColourLabel(om::ui::LedColour::Blue), "Blue") == 0);
  CHECK(strcmp(om::ui::ledColourLabel(om::ui::LedColour::Yellow), "Yellow") == 0);
  CHECK(strcmp(om::ui::ledColourLabel(om::ui::LedColour::Cyan), "Cyan") == 0);
  CHECK(strcmp(om::ui::ledColourLabel(om::ui::LedColour::Magenta), "Magenta") == 0);
  CHECK(strcmp(om::ui::ledColourLabel(om::ui::LedColour::White), "White") == 0);

  // The default KEEPS v63..v71's soft cyan, not Blue.
  CHECK(om::ui::kMessageColourDefault == om::ui::LedColour::Cyan);
  CHECK_EQ(om::ui::kLedColourCount, 7);
  CHECK(om::ui::kPowerLedDefault);
  CHECK(om::ui::kRadioLedDefault);
  CHECK_EQ(om::ui::kLedCount, 6);
  CHECK_EQ(om::ui::kPowerLedIndex, 0);
  CHECK_EQ(om::ui::kRadioLedIndex, 1);
  CHECK_EQ(om::ui::kMessageLedIndex, 2);   // the system's LED 3

  // Enter on the colour row: Red -> Green -> Blue -> Yellow -> Cyan -> Magenta -> White -> Red.
  om::ui::LedColour c = om::ui::LedColour::Red;
  for (int i = 1; i < 7; i++) {
    c = om::ui::ledColourCycle(c);
    CHECK_EQ((int)c, i);
  }
  CHECK(om::ui::ledColourCycle(c) == om::ui::LedColour::Red);

  CHECK(om::ui::ledColourValid(0));
  CHECK(om::ui::ledColourValid(6));
  CHECK(!om::ui::ledColourValid(7));

  // The locked blink (S515) keeps its exact schedule; only the colour is now a choice.
  CHECK_EQ(om::ui::kMessageLedOnMs, 120);
  CHECK_EQ(om::ui::kMessageLedOffMs, 180);
  CHECK_EQ(om::ui::kMessageLedGroupMs, 20000);
  CHECK_EQ(om::ui::kMessageLedBurstMs, 900);
  CHECK_EQ(om::ui::kMessageLedFadeStepMs, 200);   // 5 fade frames/s at most
  CHECK_EQ(om::ui::kMessageLedFadeSteps, 8);

  // The emulated fade starts dark and steps every 200 ms.
  CHECK_EQ(om::ui::messageFadeLevel(0), 0);
  CHECK_EQ(om::ui::messageFadeLevel(199), 0);
  CHECK_EQ(om::ui::messageFadeLevel(200), 73);
  CHECK_EQ(om::ui::messageFadeLevel(800), 255);
  CHECK_EQ(om::ui::messageFadeLevel(1000), 219);
  CHECK_EQ(om::ui::messageFadeLevel(1600), 0);
}

// ---- 2. register 146 bits for EVERY colour, and no traffic while unchanged --------------------------------
void testMessageRegister() {
  struct Row {
    om::ui::LedColour colour;
    uint8_t           reg;
  };
  const Row rows[] = {
      {om::ui::LedColour::Red, 0x09},      // R + fade
      {om::ui::LedColour::Green, 0x0A},    // G + fade
      {om::ui::LedColour::Blue, 0x0C},     // B + fade
      {om::ui::LedColour::Yellow, 0x0B},   // R+G + fade
      {om::ui::LedColour::Cyan, 0x0E},     // G+B + fade
      {om::ui::LedColour::Magenta, 0x0D},  // R+B + fade
      {om::ui::LedColour::White, 0x0F},    // R+G+B + fade
  };
  for (const Row& row : rows) {
    om::ui::LedRule rule;
    const om::ui::LedFrame f = rule.step(1000, in(false, true, true, 1, row.colour));
    CHECK(f.automatic);
    CHECK_EQ(f.message, row.reg);
    CHECK(f.message_changed);
    CHECK(f.changed);
    CHECK_EQ(f.message & 0x08, 0x08);   // fade ON
    CHECK_EQ(f.message & 0xF0, 0x00);   // colour B OFF, fade hold OFF
    // Automatic mode never touches a pixel: the coprocessor owns indices 0-3.
    for (int i = 0; i < 6; i++) CHECK(pixel(f, i, 0, 0, 0));
    // The same state again: nothing to write.
    CHECK(!rule.step(1025, in(false, true, true, 1, row.colour)).changed);
  }

  // Unread 0 -> the whole register is false, and the drop is a change.
  om::ui::LedRule rule;
  CHECK(!rule.step(0, in(false, true, true, 0)).changed);   // boot state is already 0 and automatic
  const om::ui::LedFrame on = rule.step(25, in(false, true, true, 2));
  CHECK_EQ(on.message, 0x0E);
  CHECK(on.changed);
  const om::ui::LedFrame off = rule.step(50, in(false, true, true, 0));
  CHECK_EQ(off.message, 0);
  CHECK(off.message_changed);
  CHECK(off.changed);

  // An out-of-range colour byte is the default (cyan), never a crash.
  om::ui::LedRule bad;
  CHECK_EQ(bad.step(0, in(false, true, true, 1, (om::ui::LedColour)99)).message, 0x0E);
}

// ---- 3. the MODE for every combination: locked x power x radio x unread, plus the no-register fallback ----
void testModeMatrix() {
  for (int locked = 0; locked <= 1; locked++) {
    for (int power = 0; power <= 1; power++) {
      for (int radio = 0; radio <= 1; radio++) {
        for (int unread = 0; unread <= 1; unread++) {
          for (int ok = 0; ok <= 1; ok++) {
            om::ui::LedRule rule;
            const om::ui::LedFrame f =
                rule.step(0, in(locked != 0, power != 0, radio != 0, unread != 0 ? 3u : 0u,
                                om::ui::kMessageColourDefault, om::ui::PowerState::Battery, true, ok != 0));
            const bool want_auto = locked == 0 && power != 0 && radio != 0 && ok != 0;
            CHECK_EQ(f.automatic ? 1 : 0, want_auto ? 1 : 0);
          }
        }
      }
    }
  }

  // Automatic -> fallback manual when register 146 is refused: the message register is cleared, and the
  // power/radio LEDs are emulated from the same state.
  om::ui::LedRule rule;
  const om::ui::LedFrame a = rule.step(0, in(false, true, true, 1));
  CHECK(a.automatic);
  const om::ui::LedFrame m = rule.step(25, in(false, true, true, 1, om::ui::kMessageColourDefault,
                                              om::ui::PowerState::Battery, true, false));
  CHECK(!m.automatic);
  CHECK(m.message_changed);
  CHECK_EQ(m.message, 0);
  CHECK(pixel(m, 0, 0, 255, 0));
  CHECK(pixel(m, 1, 0, 255, 0));
}

// ---- 4. manual mode: the emulated power and radio colours ------------------------------------------------
void testManualEmulation() {
  // Power LED Off, radio LED On: the power pixel is dark, the radio is the C6's green.
  {
    om::ui::LedRule rule;
    const om::ui::LedFrame f = rule.step(0, in(false, false, true, 0, om::ui::kMessageColourDefault,
                                               om::ui::PowerState::Battery, true));
    CHECK(!f.automatic);
    CHECK(pixel(f, 0, 0, 0, 0));
    CHECK(pixel(f, 1, 0, 255, 0));
    CHECK(pixel(f, 2, 0, 0, 0));
    for (int i = 3; i < 6; i++) CHECK(pixel(f, i, 0, 0, 0));
  }
  // Radio LED Off, power LED On: only the power pixel is lit.
  {
    om::ui::LedRule rule;
    const om::ui::LedFrame f = rule.step(0, in(false, true, false, 0));
    CHECK(!f.automatic);
    CHECK(pixel(f, 0, 0, 255, 0));
    CHECK(pixel(f, 1, 0, 0, 0));
  }
  // Both Off: both dark (and still manual).
  {
    om::ui::LedRule rule;
    const om::ui::LedFrame f = rule.step(0, in(false, false, false, 0));
    CHECK(!f.automatic);
    CHECK(pixel(f, 0, 0, 0, 0));
    CHECK(pixel(f, 1, 0, 0, 0));
  }
  // Each power state, exactly main.c's colours (no battery is checked BEFORE charging).
  {
    struct Row {
      om::ui::PowerState state;
      int               r, g, b;
    };
    const Row rows[] = {
        {om::ui::PowerState::NoBattery, 255, 0, 255},   // magenta
        {om::ui::PowerState::Battery, 0, 255, 0},       // green
        {om::ui::PowerState::Charging, 255, 255, 0},    // yellow
    };
    for (const Row& row : rows) {
      om::ui::LedRule rule;
      const om::ui::LedFrame f =
          rule.step(0, in(false, true, false, 0, om::ui::kMessageColourDefault, row.state));
      CHECK(pixel(f, 0, row.r, row.g, row.b));
      CHECK(pixel(f, 1, 0, 0, 0));
    }
    CHECK(om::ui::powerState(false, false) == om::ui::PowerState::NoBattery);
    CHECK(om::ui::powerState(false, true) == om::ui::PowerState::NoBattery);
    CHECK(om::ui::powerState(true, true) == om::ui::PowerState::Charging);
    CHECK(om::ui::powerState(true, false) == om::ui::PowerState::Battery);
  }
  // The radio pixel follows OUR C6 link only.
  {
    om::ui::LedRule rule;
    const om::ui::LedFrame f = rule.step(0, in(false, false, true, 0, om::ui::kMessageColourDefault,
                                               om::ui::PowerState::Battery, false));
    CHECK(pixel(f, 1, 0, 0, 0));
  }
}

// ---- 5. the app-driven fade: 8 steps, 200 ms each, nothing between steps --------------------------------
void testManualFade() {
  const om::ui::LedIn i = in(false, false, true, 1, om::ui::LedColour::White);
  om::ui::LedRule rule;
  const om::ui::LedFrame first = rule.step(0, i);
  CHECK(!first.automatic);
  CHECK(first.changed);             // automatic -> manual
  CHECK(pixel(first, 2, 0, 0, 0));  // the fade starts dark

  // The expected 8-step ramp, sampled exactly on its 200 ms boundaries.
  CHECK(pixel(rule.step(200, i), 2, 73, 73, 73));
  CHECK(pixel(rule.step(400, i), 2, 146, 146, 146));
  CHECK(pixel(rule.step(600, i), 2, 219, 219, 219));
  CHECK(pixel(rule.step(800, i), 2, 255, 255, 255));
  CHECK(pixel(rule.step(1000, i), 2, 219, 219, 219));
  CHECK(pixel(rule.step(1200, i), 2, 146, 146, 146));
  CHECK(pixel(rule.step(1400, i), 2, 73, 73, 73));
  CHECK(pixel(rule.step(1600, i), 2, 0, 0, 0));

  // At the task's 25 ms poll: EXACTLY five changed frames per second while breathing.
  om::ui::LedRule polled;
  CHECK(polled.step(0, i).changed);   // the mode change itself
  int changes = 0;
  for (uint32_t t = 25; t <= 3200; t += 25) {
    if (polled.step(t, i).changed) ++changes;
  }
  CHECK_EQ(changes, 16);   // 3.2 s of ramp / 0.2 s per step

  // Light the ramp again, then Unread 0: dark at once, then silence.
  CHECK(pixel(polled.step(3400, i), 2, 73, 73, 73));
  const om::ui::LedFrame stop = polled.step(3425, in(false, false, true, 0));
  CHECK(pixel(stop, 2, 0, 0, 0));
  CHECK(stop.changed);
  CHECK(!polled.step(3450, in(false, false, true, 0)).changed);
}

// ---- 6. locked: ALL off except LED 3, three blinks every 20 s, in the chosen colour ---------------------
void testLockedBlink() {
  const om::ui::LedIn i = in(true, true, true, 1);   // both switches On: locked still means manual
  om::ui::LedRule rule;
  const om::ui::LedFrame f = rule.step(0, i);
  CHECK(!f.automatic);
  CHECK(pixel(f, 0, 0, 0, 0));
  CHECK(pixel(f, 1, 0, 0, 0));
  CHECK(pixel(f, 2, 0, 51, 51));   // cyan at the blink level
  for (int k = 3; k < 6; k++) CHECK(pixel(f, k, 0, 0, 0));

  // Blink 1: on [0,120), off [120,300). Blink 2: on [300,420). Blink 3: on [600,720).
  CHECK(pixel(rule.step(119, i), 2, 0, 51, 51));
  CHECK(pixel(rule.step(120, i), 2, 0, 0, 0));
  CHECK(pixel(rule.step(300, i), 2, 0, 51, 51));
  CHECK(pixel(rule.step(420, i), 2, 0, 0, 0));
  CHECK(pixel(rule.step(600, i), 2, 0, 51, 51));
  CHECK(pixel(rule.step(720, i), 2, 0, 0, 0));

  // The long dark gap: no change, so the shell writes nothing.
  const uint32_t gap[] = {900, 1000, 5000, 12345, 19999};
  for (uint32_t t : gap) CHECK(!rule.step(t, i).changed);

  // 20 s after the first burst started: the next burst, at once.
  CHECK(pixel(rule.step(20000, i), 2, 0, 51, 51));
  CHECK(rule.step(20000, i).changed == false);   // same second step must not write again
  CHECK(pixel(rule.step(20300, i), 2, 0, 51, 51));

  // The chosen colour reaches the blink.
  om::ui::LedRule red;
  CHECK(pixel(red.step(0, in(true, true, true, 1, om::ui::LedColour::Red)), 2, 51, 0, 0));
  om::ui::LedRule yellow;
  CHECK(pixel(yellow.step(0, in(true, false, false, 1, om::ui::LedColour::Yellow)), 2, 51, 51, 0));

  // Nothing unread: all dark, and the burst does not restart from an old clock.
  om::ui::LedRule idle;
  CHECK(idle.step(0, in(true, true, true, 0)).automatic == false);
  for (int k = 0; k < 6; k++) CHECK(pixel(idle.step(25, in(true, true, true, 0)), k, 0, 0, 0));
  CHECK(pixel(idle.step(20000, in(true, true, true, 0)), 2, 0, 0, 0));
}

// ---- 7. a 32-bit millisecond wrap must not stall the blink ------------------------------------------------
void testTimeWrap() {
  const uint32_t base = 0xFFFFFF00u;
  const om::ui::LedIn i = in(true, true, true, 1, om::ui::LedColour::Cyan);
  om::ui::LedRule rule;
  CHECK(pixel(rule.step(base, i), 2, 0, 51, 51));
  CHECK(pixel(rule.step(base + 120u, i), 2, 0, 0, 0));   // wraps past 2^32
  CHECK(pixel(rule.step(base + 300u, i), 2, 0, 51, 51));
}

// ---- 8. exit: automatic back and register 146 cleared -----------------------------------------------------
void testRestore() {
  om::ui::LedRule rule;
  CHECK(!rule.step(0, in(true, true, true, 4)).automatic);   // locked, blinking
  const om::ui::LedFrame f = rule.restore();
  CHECK(f.automatic);
  CHECK(f.changed);
  CHECK(f.message_changed);
  CHECK(f.pixels_changed);
  CHECK_EQ(f.message, 0);
  for (int k = 0; k < 6; k++) CHECK(pixel(f, k, 0, 0, 0));

  // A fresh rule (the shell's exit path may not have a live one) still forces the clear.
  om::ui::LedRule fresh;
  const om::ui::LedFrame g = fresh.restore();
  CHECK(g.automatic);
  CHECK(g.changed);
  CHECK(g.message_changed);
  CHECK_EQ(g.message, 0);
}

// ---- 9. poll independence: the 25 ms task sees the same 120 ms window ------------------------------------
void testPollIndependence() {
  om::ui::LedRule polled;
  int lit_start = -1, lit_stop = -1;
  for (uint32_t t = 0; t <= 30000; t += 25) {
    const om::ui::LedFrame f = polled.step(t, in(true, true, true, 2));
    const bool lit = f.rgb[om::ui::kMessageLedIndex][1] != 0;
    if (lit && lit_start < 0) lit_start = (int)t;
    if (!lit && lit_start >= 0 && lit_stop < 0) lit_stop = (int)t;
  }
  CHECK_EQ(lit_start, 0);
  CHECK_EQ(lit_stop, 125);   // the 120 ms on window ends at the first 25 ms sample past it
}

// ---- 10. the Settings colour preview: 3 s STEADY in the NEW colour, then the normal rule (S516/EST-479) -----
void testColourPreview() {
  // The deadline helper and the steady register byte: colour A only, NO fade bit.
  CHECK_EQ(om::ui::kMessageLedPreviewMs, 3000);
  CHECK(om::ui::ledPreviewActive(0, 1));
  CHECK(om::ui::ledPreviewActive(1000, 4000));
  CHECK(!om::ui::ledPreviewActive(3000, 3000));
  CHECK(!om::ui::ledPreviewActive(3001, 3000));
  CHECK(!om::ui::ledPreviewActive(0, 0));
  CHECK(om::ui::ledPreviewActive(0xFFFFFF00u, 0x00000100u));    // wraps 2^32
  CHECK(!om::ui::ledPreviewActive(0x00000100u, 0xFFFFFF00u));

  struct Steady {
    om::ui::LedColour colour;
    uint8_t           bits;
  };
  const Steady steady[] = {
      {om::ui::LedColour::Red, 0x01},   {om::ui::LedColour::Green, 0x02},
      {om::ui::LedColour::Blue, 0x04},  {om::ui::LedColour::Yellow, 0x03},
      {om::ui::LedColour::Cyan, 0x06},  {om::ui::LedColour::Magenta, 0x05},
      {om::ui::LedColour::White, 0x07},
  };
  for (const Steady& s : steady) {
    CHECK_EQ(om::ui::messageRegisterSteady(s.colour), s.bits);
    CHECK_EQ(om::ui::messageRegister(s.colour), s.bits | 0x08);   // the breathing one always fades
  }

  // AUTOMATIC + unread 0: the preview LIGHTS LED 3 (register 146, steady, NO fade) and returns to off at 3 s.
  {
    om::ui::LedRule rule;
    const om::ui::LedIn p =
        in(false, true, true, 0, om::ui::LedColour::Red, om::ui::PowerState::Battery, true, true, 3000);
    const om::ui::LedFrame f = rule.step(0, p);
    CHECK(f.automatic);
    CHECK_EQ(f.message, 0x01);
    CHECK(f.message_changed);
    CHECK(f.changed);
    CHECK_EQ(f.message & 0x08, 0);   // steady: the fade bit is OFF
    for (int i = 0; i < 6; i++) CHECK(pixel(f, i, 0, 0, 0));   // automatic never touches a pixel
    CHECK(!rule.step(2999, p).changed);   // steady for the whole 3 s: no I2C between
    const om::ui::LedFrame back = rule.step(3000, p);
    CHECK_EQ(back.message, 0);   // unread 0 -> the normal rule says off
    CHECK(back.message_changed);
    CHECK(!rule.step(3025, p).changed);
  }

  // AUTOMATIC + unread > 0: the preview REPLACES the breathing, then the normal fade comes back.
  {
    om::ui::LedRule rule;
    CHECK_EQ(rule.step(0, in(false, true, true, 2, om::ui::LedColour::Cyan)).message, 0x0E);   // breathing
    const om::ui::LedIn p =
        in(false, true, true, 2, om::ui::LedColour::Blue, om::ui::PowerState::Battery, true, true, 4000);
    const om::ui::LedFrame f = rule.step(1000, p);
    CHECK_EQ(f.message, 0x04);   // blue, steady
    CHECK_EQ(f.message & 0x08, 0);
    CHECK(f.message_changed);
    CHECK(!rule.step(3999, p).changed);
    const om::ui::LedFrame back = rule.step(4000, p);
    CHECK_EQ(back.message, 0x0C);   // blue + fade again
    CHECK(back.message_changed);
  }

  // A SECOND step RESTARTS the 3 s (and previews its own NEW colour).
  {
    om::ui::LedRule rule;
    CHECK_EQ(rule.step(0, in(false, true, true, 0, om::ui::LedColour::Red,
                             om::ui::PowerState::Battery, true, true, 3000))
                 .message,
             0x01);
    const om::ui::LedIn second = in(false, true, true, 0, om::ui::LedColour::Yellow,
                                    om::ui::PowerState::Battery, true, true, 5000);
    const om::ui::LedFrame s = rule.step(2000, second);
    CHECK_EQ(s.message, 0x03);
    CHECK(s.message_changed);
    CHECK(!rule.step(3000, second).changed);   // the FIRST window would have ended here
    CHECK(!rule.step(4999, second).changed);
    CHECK_EQ(rule.step(5000, second).message, 0);   // 3 s after the SECOND step
  }

  // MANUAL over the unread FADE: full brightness, steady; the fade restarts from dark afterwards.
  {
    om::ui::LedRule rule;
    CHECK(!rule.step(0, in(false, false, true, 1, om::ui::LedColour::Magenta)).automatic);   // power LED off
    CHECK(pixel(rule.step(0, in(false, false, true, 1, om::ui::LedColour::Magenta)), 2, 0, 0, 0));
    const om::ui::LedIn p =
        in(false, false, true, 1, om::ui::LedColour::White, om::ui::PowerState::Battery, true, true, 3000);
    const om::ui::LedFrame f = rule.step(400, p);
    CHECK(pixel(f, 2, 255, 255, 255));   // steady, full
    CHECK(f.pixels_changed);
    CHECK(!rule.step(2999, p).changed);
    const om::ui::LedFrame back = rule.step(3000, p);
    CHECK(pixel(back, 2, 0, 0, 0));   // a FRESH fade starts dark
    CHECK(back.changed);
    CHECK(pixel(rule.step(3200, p), 2, 73, 73, 73));   // and ramps as usual 200 ms later
  }

  // MANUAL over unread OFF: the preview lights LED 3, then it goes dark again — the other pixels untouched.
  {
    om::ui::LedRule rule;
    const om::ui::LedIn p =
        in(false, false, true, 0, om::ui::LedColour::Yellow, om::ui::PowerState::Battery, true, true, 3000);
    const om::ui::LedFrame f = rule.step(0, p);
    CHECK(!f.automatic);
    CHECK(pixel(f, 2, 255, 255, 0));   // yellow, full
    CHECK(pixel(f, 0, 0, 0, 0));       // Power LED Off stays off
    CHECK(pixel(f, 1, 0, 255, 0));     // Radio LED On stays the C6 green
    CHECK(!rule.step(2999, p).changed);
    CHECK(pixel(rule.step(3000, p), 2, 0, 0, 0));
  }

  // LOCKED: no Settings, so NO preview. The locked blink is the only light, in the chosen colour.
  {
    om::ui::LedRule rule;
    const om::ui::LedIn p =
        in(true, true, true, 1, om::ui::LedColour::Red, om::ui::PowerState::Battery, true, true, 3000);
    const om::ui::LedFrame f = rule.step(0, p);
    CHECK(!f.automatic);
    CHECK(pixel(f, 2, 51, 0, 0));   // the 20 % blink, NOT the full preview
    CHECK(pixel(f, 0, 0, 0, 0));
    CHECK(pixel(f, 1, 0, 0, 0));
    CHECK(!rule.step(100, p).changed);
    // Nothing unread and locked: the deadline must not light anything at all.
    om::ui::LedRule idle;
    const om::ui::LedIn q =
        in(true, true, true, 0, om::ui::LedColour::Red, om::ui::PowerState::Battery, true, true, 3000);
    for (int k = 0; k < 6; k++) CHECK(pixel(idle.step(0, q), k, 0, 0, 0));
  }

  // EXIT: restore() while the deadline is still ahead ends the preview with the usual dark/automatic frame.
  {
    om::ui::LedRule rule;
    const om::ui::LedIn p =
        in(false, true, true, 0, om::ui::LedColour::Red, om::ui::PowerState::Battery, true, true, 3000);
    CHECK_EQ(rule.step(0, p).message, 0x01);
    const om::ui::LedFrame r = rule.restore();
    CHECK(r.automatic);
    CHECK(r.changed);
    CHECK_EQ(r.message, 0);
    for (int k = 0; k < 6; k++) CHECK(pixel(r, k, 0, 0, 0));
  }
}

}  // namespace

// S516 (Claude, review of EST-479): "no preview" (0) stays no preview after 24.8 days of uptime.
static void testPreviewZeroAfterLongUptime() {
    CHECK(!om::ui::ledPreviewActive(0x80000010u, 0));
    CHECK(!om::ui::ledPreviewActive(0xF0000000u, 0));
    CHECK(om::ui::ledPreviewActive(0x80000010u, 0x80000010u + 3000u));
}

int main() {
    testPreviewZeroAfterLongUptime();
  testSettingValues();
  testMessageRegister();
  testModeMatrix();
  testManualEmulation();
  testManualFade();
  testLockedBlink();
  testTimeWrap();
  testRestore();
  testPollIndependence();
  testColourPreview();
  if (g_failures != 0) {
    printf("message-led: %d/%d checks FAILED\n", g_failures, g_checks);
    return 1;
  }
  printf("message-led: %d checks, 0 failures\n", g_checks);
  return 0;
}
