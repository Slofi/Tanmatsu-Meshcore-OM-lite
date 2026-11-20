// SPDX-License-Identifier: MIT
// S516/EST-477: the Tanmatsu's LEDs — the SYSTEM's meanings kept, two off switches and the message colour.
//
// Filip (2026-11-20): *"LED 1 & 2 should always be mapped to their default states, but with an option in our
// settings to turn them off"*, and earlier the same evening: *"LED 1 - ON/OFF, LED 2 - ON/OFF, LED 3 - new MSG,
// with options to select colours.. leave other LEDs as they are, no options for them"*. So:
//   * index 0 (LED 1) = the coprocessor's POWER indicator: PMIC fault red blinking; no battery magenta
//     (0xFF00FF); battery green (0x00FF00); charging yellow (0xFFFF00) — tanmatsu-coprocessor-firmware @ 7c2aad8
//     src/application/main.c:709-746, the facts Claude read.
//   * index 1 (LED 2) = the RADIO LED, the C6's own state (NOT Wi-Fi association): off when the C6 is off,
//     blue 0x0000FF in its bootloader, green 0x00FF00 when its application runs (same file, main.c:709-746).
//     OM Lite keeps the C6 running for LoRa, so this is green while our link is up.
//   * index 2 (LED 3) = the MESSAGE LED: register 146 at I2C_REG_MESSAGE, bits 0-2 colour A (R,G,B on/off),
//     bit 3 fade, bits 4-6 colour B, bit 7 fade hold (main.c:750-800; our BSP wrapper is
//     tanmatsu_coprocessor_set_message, tanmatsu_coprocessor.h:262-270).
//   * indices 3-5 keep the coprocessor's rest state (off); the user gets no rows for them.
//
// The rule (decided by Claude, S516):
//   1. Unlocked, Power LED On and Radio LED On: AUTOMATIC mode, always. Unread > 0 -> message colour A = the
//      chosen colour, colour B off, fade on, hold off (it breathes); unread == 0 -> the message register is all
//      false. No pixel is touched: the coprocessor still owns indices 0-3.
//   2. Unlocked and one or both off (or the coprocessor refused register 146): MANUAL mode while OM Lite runs.
//      The app writes all six: the Off ones dark, an On one EMULATES the coprocessor colour from the same state
//      (power: our battery/charging state -> green / yellow / magenta for no battery; radio: green while our C6
//      link is up, off otherwise), index 2 is the message LED, indices 3-5 off.
//   3. Locked (screen off): MANUAL, all off, index 2 blinks three times every 20 s in the chosen colour while
//      unread > 0 (Filip, S515).
//   4. Exit to the launcher: AUTOMATIC back and the message register cleared, so no stale message fade is left
//      for the launcher (restore()).
//   5. Settings › General › LED › Message LED colour (S516/EST-479): while the shell's preview deadline is ahead
//      AND the screen is unlocked, LED 3 is STEADY in the chosen (new) colour — register 146 with the colour and
//      NO fade bit in automatic mode, the full-brightness pixel in manual mode. 3 s, restarted by each further
//      step; afterwards rule 1/2 takes over again. Locked and any exit never preview.
//
// The app-driven fade is a plain 0..255 ramp in 8 steps of 200 ms, so at most FIVE I2C frames per second while
// the message LED is breathing and NONE at rest: this header says nothing CHANGED between steps.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace om {
namespace ui {

// The BSP's six pixels, in the coprocessor's order.
constexpr uint8_t kLedCount        = 6;
constexpr uint8_t kPowerLedIndex   = 0;
constexpr uint8_t kRadioLedIndex   = 1;
constexpr uint8_t kMessageLedIndex = 2;   // LED 3, the system's own message LED

// The two switches. Default On, as Filip asked: the system meanings are kept unless the user turns them off.
constexpr bool kPowerLedDefault = true;
constexpr bool kRadioLedDefault = true;

// ---- colours (Settings › General › LED › "Message LED colour") ---------------------------------------------
enum class LedColour : uint8_t {
    Red = 0,
    Green,
    Blue,
    Yellow,
    Cyan,
    Magenta,
    White,
    Count,
};

constexpr uint8_t kLedColourCount = (uint8_t)LedColour::Count;
// Today's behaviour (v63..v71) was a soft cyan: a dimmed full (0, 255, 255). The default therefore KEEPS
// that colour; there is no "Blue if none" case to fall back to.
constexpr LedColour kMessageColourDefault = LedColour::Cyan;

inline const char* ledColourLabel(LedColour c) {
    switch (c) {
        case LedColour::Red: return "Red";
        case LedColour::Green: return "Green";
        case LedColour::Blue: return "Blue";
        case LedColour::Yellow: return "Yellow";
        case LedColour::Cyan: return "Cyan";
        case LedColour::Magenta: return "Magenta";
        case LedColour::White: return "White";
        default: return "Cyan";
    }
}

inline bool ledColourValid(uint8_t v) { return v < kLedColourCount; }

// Enter on the row: Red -> Green -> Blue -> Yellow -> Cyan -> Magenta -> White -> Red.
inline LedColour ledColourCycle(LedColour c) {
    const uint8_t next = (uint8_t)((uint8_t)c + 1);
    return next >= kLedColourCount ? LedColour::Red : (LedColour)next;
}

// The colour's ON channels. The coprocessor wants booleans (register 146), the manual mode wants pixels.
inline void ledColourRgb(LedColour c, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t rr = 0, gg = 0, bb = 0;
    switch (c) {
        case LedColour::Red: rr = 255; break;
        case LedColour::Green: gg = 255; break;
        case LedColour::Blue: bb = 255; break;
        case LedColour::Yellow: rr = 255; gg = 255; break;
        case LedColour::Cyan: gg = 255; bb = 255; break;
        case LedColour::Magenta: rr = 255; bb = 255; break;
        case LedColour::White: rr = 255; gg = 255; bb = 255; break;
        default: gg = 255; bb = 255; break;   // an invalid byte reads as the default (cyan)
    }
    if (r != nullptr) *r = rr;
    if (g != nullptr) *g = gg;
    if (b != nullptr) *b = bb;
}

// Register 146: bits 0-2 colour A, bit 3 fade, bits 4-6 colour B, bit 7 fade hold. The breathing message is
// colour A = the chosen colour, B = off, fade = on, hold = off.
inline uint8_t messageColourBits(LedColour c) {
    uint8_t r = 0, g = 0, b = 0;
    ledColourRgb(c, &r, &g, &b);
    return (uint8_t)((r != 0 ? 1u : 0u) | (g != 0 ? 2u : 0u) | (b != 0 ? 4u : 0u));
}

inline uint8_t messageRegister(LedColour c) {
    return (uint8_t)(messageColourBits(c) | (1u << 3));   // fade on, colour B off, hold off
}

// S516/EST-479: the Settings preview is STEADY: colour A only, no fade, no colour B, no hold, so the colour is
// easy to judge for the 3 s.
inline uint8_t messageRegisterSteady(LedColour c) {
    return messageColourBits(c);
}

// ---- the power state we can SEE (main.c:715-732) -----------------------------------------------------------
// The coprocessor ALSO has an uninitialised (white) and a PMIC-fault (red blinking) branch; the BSP gives the app
// only the battery/charging facts, and the ticket's emulation names those three, so those are the three we say.
enum class PowerState : uint8_t { NoBattery = 0, Battery, Charging };

inline PowerState powerState(bool battery_available, bool charging) {
    if (!battery_available) return PowerState::NoBattery;   // main.c checks "no battery" BEFORE charging
    return charging ? PowerState::Charging : PowerState::Battery;
}

inline void powerLedRgb(PowerState s, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t rr = 0, gg = 0, bb = 0;
    switch (s) {
        case PowerState::NoBattery: rr = 255; bb = 255; break;   // magenta 0xFF00FF
        case PowerState::Charging: rr = 255; gg = 255; break;    // yellow  0xFFFF00
        case PowerState::Battery:
        default: gg = 255; break;                                // green   0x00FF00
    }
    if (r != nullptr) *r = rr;
    if (g != nullptr) *g = gg;
    if (b != nullptr) *b = bb;
}

// ---- the locked blink (unchanged from S515/EST-468) --------------------------------------------------------
constexpr uint8_t  kMessageLedBlinks  = 3;
constexpr uint32_t kMessageLedOnMs    = 120;
constexpr uint32_t kMessageLedOffMs   = 180;
constexpr uint32_t kMessageLedCycleMs = kMessageLedOnMs + kMessageLedOffMs;          // one blink
constexpr uint32_t kMessageLedBurstMs = kMessageLedBlinks * kMessageLedCycleMs;      // three blinks
constexpr uint32_t kMessageLedGroupMs = 20000;                                       // then every 20 s
// The locked blink keeps S515's gentle look: 20 % of the chosen colour (v71 used a 15 % global brightness over
// full cyan; this is the same "soft, not a torch at night" intent, now for every colour).
constexpr uint8_t kMessageLedBlinkLevel = 51;

// ---- the app-driven fade in manual mode --------------------------------------------------------------------
// 8 steps of 200 ms: 0, 73, 146, 219, 255, 219, 146, 73 -> a 1.6 s breath. At most 5 I2C frames/s, and the
// frame says "unchanged" between steps, so there is no traffic at rest.
constexpr uint32_t kMessageLedFadeStepMs = 200;
constexpr uint8_t  kMessageLedFadeSteps  = 8;

inline uint8_t messageFadeLevel(uint32_t elapsed_ms) {
    static constexpr uint8_t kLevels[kMessageLedFadeSteps] = {0, 73, 146, 219, 255, 219, 146, 73};
    return kLevels[(elapsed_ms / kMessageLedFadeStepMs) % kMessageLedFadeSteps];
}

// ---- the Settings colour preview (S516/EST-479) -------------------------------------------------------------
// Filip: *"when changing the new MSG LED colour, it should 'demo' the colour for a few seconds on
// change/'scroll'"*. Each step of the row's cycle holds LED 3 STEADY in the NEW colour for 3 s, then the normal
// rule takes over again; a further step restarts the 3 s. The shell owns ONE deadline (now + 3 s on each
// change); the rule only reads whether it is still ahead. Steady = no fade bit in register 146 and full
// brightness in the app's pixel, so the colour is easy to judge. Locked has no Settings: the rule ignores the
// deadline while locked, so the locked blink is unchanged.
constexpr uint32_t kMessageLedPreviewMs = 3000;

// Wrap-safe: a deadline in the future is a positive int32 difference; 0 (or past) means no preview.
inline bool ledPreviewActive(uint32_t now_ms, uint32_t preview_until_ms) {
    // S516 (Claude, review): 0 is "no preview" EXPLICITLY. The wrap-safe compare alone reads 0 as a deadline
    // ahead once the ms clock passes 2^31 (24.8 days up), which would light LED 3 steady for no reason.
    return preview_until_ms != 0 && (int32_t)(preview_until_ms - now_ms) > 0;
}

// What the shell publishes to the rule each tick.
struct LedIn {
    bool       locked              = false;   // key-lock / screen off (core/screen_power.h)
    uint32_t   unread              = 0;       // the hint bar's unreadTotals() sum
    bool       power_on            = kPowerLedDefault;
    bool       radio_on            = kRadioLedDefault;
    LedColour  colour              = kMessageColourDefault;
    PowerState power               = PowerState::Battery;
    bool       radio_up            = false;   // our C6 link is up (the C6 application is running)
    bool       message_register_ok = true;    // false = older coprocessor firmware, no register 146
    uint32_t   preview_until_ms    = 0;       // Settings colour preview deadline; 0 / past = none
};

// One step's answer. `changed` is the only gate the shell needs: apply ONLY when true, so there is no I2C
// traffic between blinks and between fade steps.
struct LedFrame {
    bool    automatic       = true;   // true = hand indices 0-3 back to the coprocessor
    bool    changed         = false;  // mode, message register or pixels differ from the previous step
    bool    message_changed = false;  // write register 146 with `message`
    bool    pixels_changed  = false;  // write the six pixels
    uint8_t message         = 0;      // register 146's byte
    uint8_t rgb[kLedCount][3] = {};   // the manual pixels (all zero in automatic mode)
};

class LedRule {
  public:
    // One step. `now_ms` is monotonic (esp_timer / 1000); only differences are used, so a wrap is safe.
    LedFrame step(uint32_t now_ms, const LedIn& in) {
        const LedColour colour = ledColourValid((uint8_t)in.colour) ? in.colour : kMessageColourDefault;
        const bool    automatic = !in.locked && in.power_on && in.radio_on && in.message_register_ok;
        // Rule 5: the Settings preview, only while unlocked. It overrides unread-off and unread-fade.
        const bool    preview   = !in.locked && ledPreviewActive(now_ms, in.preview_until_ms);
        // S516 (Claude; Filip: "LED3 flashes constantly on new MSG, not just 3-times than nothing for 20s"):
        // the unread pattern is the 3-blink burst every 20 s in EVERY mode (it was a continuous fade while
        // unlocked, which was Claude's spec, not his). One timer for all modes; the preview pauses it.
        bool burst_on = false;
        if (in.unread > 0 && !preview) {
            if (!blink_active_) {
                blink_active_ = true;
                blink_start_  = now_ms;
            }
            burst_on = burstLit((uint32_t)(now_ms - blink_start_) % kMessageLedGroupMs);
        } else {
            blink_active_ = false;
        }
        const uint8_t message   = automatic ? ((preview || burst_on) ? messageRegisterSteady(colour) : 0) : 0;

        LedFrame f;
        f.automatic = automatic;
        f.message   = message;
        memset(f.rgb, 0, sizeof(f.rgb));

        if (!automatic && !in.locked) {
            // Rule 2: manual, the On indicators emulate the coprocessor from the same state.
            if (in.power_on) {
                powerLedRgb(in.power, &f.rgb[kPowerLedIndex][0], &f.rgb[kPowerLedIndex][1],
                            &f.rgb[kPowerLedIndex][2]);
            }
            if (in.radio_on && in.radio_up) {
                f.rgb[kRadioLedIndex][1] = 255;   // green, the C6 application's own colour
            }
            if (preview) {
                // Rule 5: full brightness, steady, whatever unread says. The breath restarts after the preview.
                setScaled(&f.rgb[kMessageLedIndex][0], colour, 255);
                breath_active_ = false;
            } else if (burst_on) {
                setScaled(&f.rgb[kMessageLedIndex][0], colour, kMessageLedBlinkLevel);
            }
            breath_active_ = false;
        } else {
            breath_active_ = false;
        }

        if (!automatic && in.locked) {
            // Rule 3: all off; index 2 blinks three times every 20 s in the chosen colour.
            if (burst_on) setScaled(&f.rgb[kMessageLedIndex][0], colour, kMessageLedBlinkLevel);
        }

        f.message_changed = (automatic != prev_automatic_) || (message != prev_message_);
        f.pixels_changed  = (automatic != prev_automatic_) || !samePixels(f.rgb);
        f.changed         = f.message_changed || f.pixels_changed;

        prev_automatic_ = automatic;
        prev_message_   = message;
        memcpy(prev_rgb_, f.rgb, sizeof(prev_rgb_));
        return f;
    }

    // Rule 4: exit to the launcher. Always CHANGED, so the shell writes the dark pixels, clears register 146
    // (all false) and only then hands the LEDs back to the coprocessor. A fresh rule can return it.
    LedFrame restore() {
        blink_active_   = false;
        breath_active_  = false;
        prev_automatic_ = true;
        prev_message_   = 0;
        memset(prev_rgb_, 0, sizeof(prev_rgb_));

        LedFrame f;
        f.automatic       = true;
        f.changed         = true;
        f.message_changed = true;
        f.pixels_changed  = true;
        f.message         = 0;
        return f;
    }

  private:
    static void setScaled(uint8_t* rgb, LedColour c, uint8_t level) {
        uint8_t r = 0, g = 0, b = 0;
        ledColourRgb(c, &r, &g, &b);
        rgb[0] = (uint8_t)((uint16_t)r * level / 255u);
        rgb[1] = (uint8_t)((uint16_t)g * level / 255u);
        rgb[2] = (uint8_t)((uint16_t)b * level / 255u);
    }

    static bool burstLit(uint32_t t) {
        return t < kMessageLedBurstMs && (t % kMessageLedCycleMs) < kMessageLedOnMs;
    }

    bool samePixels(const uint8_t rgb[kLedCount][3]) const {
        return memcmp(rgb, prev_rgb_, sizeof(prev_rgb_)) == 0;
    }

    bool     blink_active_  = false;
    uint32_t blink_start_   = 0;
    bool     breath_active_ = false;
    uint32_t breath_start_  = 0;

    bool    prev_automatic_ = true;
    uint8_t prev_message_   = 0;
    uint8_t prev_rgb_[kLedCount][3] = {};
};

}  // namespace ui
}  // namespace om
