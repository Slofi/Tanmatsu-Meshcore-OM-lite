// SPDX-License-Identifier: MIT
// S515/EST-468: the MESSAGE LED's rule, pure and host-tested (time in, LED frame out).
//
// Filip (2026-11-19): *"when the Tanmatsu is 'locked', screen off, all LEDs would be off, but the new MSG one
// would blink, like 3-continuing blinks, every 20 secs"*. The BSP has 6 LEDs behind the coprocessor; which
// index is the "message" one is NOT documented, so the user picks one in Settings › General › LED. This file
// owns only the DECISION: the shell's task applies it through bsp_led_*.
//
// The frame is one step's answer:
//   * `manual`   the app must own the LEDs (bsp_led_set_mode(false)); false means "hand them back"
//                (all pixels dark, then bsp_led_set_mode(true) so the system indicators return);
//   * `lit`      the chosen LED is ON in this slice;
//   * `led`      1..6 while `manual`, 0 otherwise (index - 1 into the BSP's list);
//   * `changed`  `manual`, `lit` or `led` differs from the previous step, so the shell only writes on a
//                change and there is NO I2C traffic between blinks.
//
// The rule, exactly:
//   active = screen OFF (core/screen_power.h) AND unread total > 0 AND a LED is chosen (1..6).
//   While active: manual mode, all 6 LEDs dark, the chosen one gives THREE short blinks (120 ms on /
//   180 ms off) at once, then the same burst every 20 s, dim (15 %) soft cyan.
//   It stops the moment the screen goes on, the unread drops to 0, or the LED choice goes back to Off.
//
// A PREVIEW (`preview(now, led)`) runs the same three blinks ONCE regardless of the rules, so Settings can
// show which LED a choice means. When it ends, the rule takes over (and starts its own burst at once).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace ui {

constexpr uint8_t kMessageLedMax      = 6;
constexpr uint8_t kMessageLedOff      = 0;   // the Settings value "Off"
constexpr uint8_t kMessageLedDefault  = kMessageLedOff;
constexpr uint8_t kMessageLedBlinks   = 3;
constexpr uint32_t kMessageLedOnMs    = 120;
constexpr uint32_t kMessageLedOffMs   = 180;
constexpr uint32_t kMessageLedCycleMs = kMessageLedOnMs + kMessageLedOffMs;          // one blink
constexpr uint32_t kMessageLedBurstMs = kMessageLedBlinks * kMessageLedCycleMs;      // three blinks
constexpr uint32_t kMessageLedGroupMs = 20000;                                       // then every 20 s
// Chosen by the implementer, as the ticket allows: 15 % brightness, soft cyan (full cyan through the
// coprocessor's 15 % limit reads as a soft cyan, not a bright white).
constexpr uint8_t kMessageLedBrightness = 15;
constexpr uint8_t kMessageLedR          = 0;
constexpr uint8_t kMessageLedG          = 255;
constexpr uint8_t kMessageLedB          = 255;

// Settings › General › LED: "Off", "1" .. "6". Any value outside 0..6 reads as Off.
inline const char* messageLedLabel(uint8_t led) {
    switch (led) {
        case 1: return "1";
        case 2: return "2";
        case 3: return "3";
        case 4: return "4";
        case 5: return "5";
        case 6: return "6";
        case kMessageLedOff:
        default: return "Off";
    }
}

// Enter on the row: Off -> 1 -> 2 -> … -> 6 -> Off.
inline uint8_t messageLedCycle(uint8_t led) {
    if (led >= kMessageLedMax) return kMessageLedOff;
    return (uint8_t)(led + 1);
}

inline bool messageLedValid(uint8_t led) { return led <= kMessageLedMax; }

// What the shell publishes to the rule each tick. `unread` is the hint bar's unreadTotals() sum.
struct MessageLedIn {
    bool     screen_off = false;
    uint32_t unread     = 0;
    uint8_t  led        = kMessageLedOff;
};

struct MessageLedFrame {
    bool     manual  = false;
    bool     lit     = false;
    uint8_t  led     = kMessageLedOff;
    bool     changed = false;
};

class MessageLed {
  public:
    // One step. `now_ms` is monotonic (esp_timer / 1000); only differences are used, so a wrap is safe.
    MessageLedFrame step(uint32_t now_ms, const MessageLedIn& in) {
        bool     manual = false;
        bool     lit    = false;
        uint8_t  out    = kMessageLedOff;

        if (preview_) {
            const uint32_t age = now_ms - preview_start_;
            if (age < kMessageLedBurstMs) {
                manual = true;
                lit    = burstLit(age);
                out    = preview_led_;
            } else {
                // The preview is over: hand the LEDs back, and let the rule start its own burst at once.
                preview_ = false;
                active_  = false;
            }
        }

        if (!manual) {
            const bool want = in.screen_off && in.unread > 0 && in.led >= 1 && in.led <= kMessageLedMax;
            if (want) {
                if (!active_ || led_ != in.led) {
                    active_ = true;
                    start_  = now_ms;   // "at once", and again whenever the choice changes
                    led_    = in.led;
                }
                manual = true;
                lit    = burstLit((uint32_t)(now_ms - start_) % kMessageLedGroupMs);
                out    = in.led;
            } else {
                active_ = false;
            }
        }

        MessageLedFrame f;
        f.manual  = manual;
        f.lit     = lit;
        f.led     = out;
        f.changed = (manual != out_manual_) || (lit != out_lit_) || (manual && out != out_led_);
        out_manual_ = manual;
        out_lit_    = lit;
        out_led_    = out;
        return f;
    }

    // Settings preview: three blinks of `led` (1..6) starting now, then the rule resumes. A request while a
    // preview is running restarts it on the new LED.
    void preview(uint32_t now_ms, uint8_t led) {
        if (led < 1 || led > kMessageLedMax) return;
        preview_       = true;
        preview_start_ = now_ms;
        preview_led_   = led;
        active_        = false;   // after the preview the rule must not think its 20 s clock is still running
    }

    bool previewing() const { return preview_; }

  private:
    static bool burstLit(uint32_t t) {
        return t < kMessageLedBurstMs && (t % kMessageLedCycleMs) < kMessageLedOnMs;
    }

    bool     preview_       = false;
    uint32_t preview_start_ = 0;
    uint8_t  preview_led_   = kMessageLedOff;
    bool     active_        = false;
    uint32_t start_         = 0;
    uint8_t  led_           = kMessageLedOff;

    bool    out_manual_ = false;
    bool    out_lit_    = false;
    uint8_t out_led_    = kMessageLedOff;
};

}  // namespace ui
}  // namespace om
