// SPDX-License-Identifier: MIT
// S516/EST-477: the Tanmatsu's LEDs: device glue. See main/message_led.h for the split.
#include "message_led.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "tanmatsu_coprocessor.h"

#include "bsp/led.h"
#include "bsp/tanmatsu.h"
#include "message_led_rule.h"

namespace om {
namespace led {
namespace {

const char* TAG        = "om-led";
const char* kNamespace = "omled";
// NVS keys beside the old one. "msgled" (Off / 1..6) is the v63..v71 LED picker; it is migrated away in
// begin() because the message LED is now always the system's LED 3 (index 2).
const char* kKeyPower  = "pwled";
const char* kKeyRadio  = "rdled";
const char* kKeyColour = "msgcol";
const char* kKeyOld    = "msgled";
constexpr uint32_t kTaskPeriodMs = 25;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

bool         s_power_on = ui::kPowerLedDefault;
bool         s_radio_on = ui::kRadioLedDefault;
ui::LedColour s_colour  = ui::kMessageColourDefault;

bool     s_screen_off        = false;
uint32_t s_unread            = 0;
bool     s_battery_available = true;
bool     s_charging          = false;
bool     s_radio_up          = false;
bool     s_message_ok        = true;
bool     s_stop              = false;
bool     s_started           = false;

SemaphoreHandle_t s_out_mutex = nullptr;   // one owner of the bsp_led_* calls at a time
bool              s_manual    = false;

bool storeU8(const char* key, uint8_t value) {
    nvs_handle_t h = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t r = nvs_set_u8(h, key, value);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "%s not saved (%s)", key, esp_err_to_name(r));
        return false;
    }
    return true;
}

void writePixels(const uint8_t rgb[ui::kLedCount][3]) {
    for (uint32_t i = 0; i < ui::kLedCount; ++i) {
        bsp_led_set_pixel_rgb(i, rgb[i][0], rgb[i][1], rgb[i][2]);
    }
    bsp_led_send();
}

void writeAllDark() {
    uint8_t dark[ui::kLedCount][3] = {};
    writePixels(dark);
}

// Register 146: bits 0-2 colour A, bit 3 fade, bits 4-6 colour B, bit 7 fade hold (main.c:750-800).
bool setMessageBits(uint8_t bits) {
    tanmatsu_coprocessor_handle_t h = nullptr;
    if (bsp_tanmatsu_coprocessor_get_handle(&h) != ESP_OK || h == nullptr) return false;
    return tanmatsu_coprocessor_set_message(h, (bits & 0x01) != 0, (bits & 0x02) != 0, (bits & 0x04) != 0,
                                            (bits & 0x10) != 0, (bits & 0x20) != 0, (bits & 0x40) != 0,
                                            (bits & 0x08) != 0, (bits & 0x80) != 0) == ESP_OK;
}

void writeMessage(uint8_t bits) {
    bool ok;
    portENTER_CRITICAL(&s_mux);
    ok = s_message_ok;
    portEXIT_CRITICAL(&s_mux);
    if (!ok) return;
    if (!setMessageBits(bits)) {
        portENTER_CRITICAL(&s_mux);
        s_message_ok = false;
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGW(TAG, "[led] register 146 refused at run time: the app drives the message LED from now on");
    }
}

// Apply one frame. The caller holds s_out_mutex.
void applyFrameLocked(const ui::LedFrame& f) {
    if (f.automatic) {
        if (s_manual) {
            // Leaving manual mode: dark the app-owned pixels first, clear the message, then hand indices 0-3
            // back so the system indicators appear without one stale manual frame.
            writeAllDark();
            if (f.message_changed) writeMessage(f.message);
            bsp_led_set_mode(true);
            s_manual = false;
            ESP_LOGI(TAG, "[led] automatic");
        } else if (f.message_changed) {
            // Rule 1: the coprocessor owns the pixels; only register 146 changes.
            writeMessage(f.message);
        }
    } else {
        if (!s_manual) {
            bsp_led_set_mode(false);
            s_manual = true;
            ESP_LOGI(TAG, "[led] manual (power %s, radio %s)", f.rgb[ui::kPowerLedIndex][0] == 0 &&
                                                                          f.rgb[ui::kPowerLedIndex][1] == 0 &&
                                                                          f.rgb[ui::kPowerLedIndex][2] == 0
                                                                      ? "off"
                                                                      : "on",
                     f.rgb[ui::kRadioLedIndex][1] == 0 ? "off" : "on");
        }
        if (f.message_changed) writeMessage(f.message);   // 0 when entering manual: no stale fade
        if (f.pixels_changed) writePixels(f.rgb);
    }
}

void applyFrame(const ui::LedFrame& f) {
    if (s_out_mutex != nullptr) xSemaphoreTake(s_out_mutex, portMAX_DELAY);
    // leaveToLauncher() may have stopped the task between the copy and this call. Re-check under the output
    // mutex so the final state is always "automatic", never one last manual frame.
    bool stopped;
    portENTER_CRITICAL(&s_mux);
    stopped = s_stop;
    portEXIT_CRITICAL(&s_mux);
    if (!stopped) applyFrameLocked(f);
    if (s_out_mutex != nullptr) xSemaphoreGive(s_out_mutex);
}

void ledTask(void*) {
    ui::LedRule rule;
    for (;;) {
        bool          off;
        uint32_t      unread;
        bool          power_on, radio_on, battery, charging, radio_up, message_ok, stop;
        ui::LedColour chosen;
        portENTER_CRITICAL(&s_mux);
        off        = s_screen_off;
        unread     = s_unread;
        power_on   = s_power_on;
        radio_on   = s_radio_on;
        chosen     = s_colour;
        battery    = s_battery_available;
        charging   = s_charging;
        radio_up   = s_radio_up;
        message_ok = s_message_ok;
        stop       = s_stop;
        portEXIT_CRITICAL(&s_mux);
        if (stop) {
            vTaskDelete(nullptr);
        }

        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        ui::LedIn in;
        in.locked              = off;
        in.unread              = unread;
        in.power_on            = power_on;
        in.radio_on            = radio_on;
        in.colour              = chosen;
        in.power               = ui::powerState(battery, charging);
        in.radio_up            = radio_up;
        in.message_register_ok = message_ok;
        const ui::LedFrame f = rule.step(now, in);
        if (f.changed) applyFrame(f);

        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

}  // namespace

void begin() {
    s_power_on = ui::kPowerLedDefault;
    s_radio_on = ui::kRadioLedDefault;
    s_colour   = ui::kMessageColourDefault;

    nvs_handle_t h = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, kKeyPower, &v) == ESP_OK) s_power_on = v != 0;
        if (nvs_get_u8(h, kKeyRadio, &v) == ESP_OK) s_radio_on = v != 0;
        if (nvs_get_u8(h, kKeyColour, &v) == ESP_OK && ui::ledColourValid(v)) s_colour = (ui::LedColour)v;

        // Migrate/ignore the old LED picker cleanly: read it, say what happened, erase it, commit. Its value
        // cannot be carried over — the message LED is no longer a choice.
        if (nvs_get_u8(h, kKeyOld, &v) == ESP_OK) {
            if (nvs_erase_key(h, kKeyOld) == ESP_OK && nvs_commit(h) == ESP_OK) {
                ESP_LOGI(TAG, "old NVS %s=%u ignored (the message LED is LED 3 now) and erased", kKeyOld,
                         (unsigned)v);
            } else {
                ESP_LOGW(TAG, "old NVS %s=%u ignored, but could not be erased", kKeyOld, (unsigned)v);
            }
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "power LED %s, radio LED %s, message colour %s", s_power_on ? "On" : "Off",
             s_radio_on ? "On" : "Off", ui::ledColourLabel(s_colour));

    // Probe register 146 once at boot (and leave it cleared). An older coprocessor firmware has no register
    // 146; then the app drives the message LED and says so, with the firmware version it found.
    s_message_ok = setMessageBits(0);
    if (!s_message_ok) {
        tanmatsu_coprocessor_handle_t cph = nullptr;
        uint16_t                      fw  = 0;
        if (bsp_tanmatsu_coprocessor_get_handle(&cph) == ESP_OK && cph != nullptr &&
            tanmatsu_coprocessor_get_firmware_version(cph, &fw) == ESP_OK) {
            ESP_LOGW(TAG,
                     "coprocessor firmware v%u has no usable message register 146: the app drives the message "
                     "LED",
                     (unsigned)fw);
        } else {
            ESP_LOGW(TAG,
                     "message register 146 is unavailable and the coprocessor firmware version is not readable: "
                     "the app drives the message LED");
        }
    }

    // A previous run must never leave the system's power/radio indicators dark.
    if (bsp_led_set_mode(true) == ESP_OK) {
        ESP_LOGI(TAG, "automatic mode on");
    } else {
        ESP_LOGW(TAG, "automatic mode refused by the BSP");
    }

    s_out_mutex = xSemaphoreCreateMutex();
}

void startTask() {
    if (s_started) return;
    if (xTaskCreate(ledTask, "om-led", 3072, nullptr, 1, nullptr) != pdPASS) {
        ESP_LOGW(TAG, "task not started; the LEDs stay in automatic mode");
        return;
    }
    s_started = true;
}

bool powerOn() {
    portENTER_CRITICAL(&s_mux);
    const bool v = s_power_on;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

bool setPowerOn(bool on) {
    if (!storeU8(kKeyPower, on ? 1 : 0)) return false;
    portENTER_CRITICAL(&s_mux);
    s_power_on = on;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool radioOn() {
    portENTER_CRITICAL(&s_mux);
    const bool v = s_radio_on;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

bool setRadioOn(bool on) {
    if (!storeU8(kKeyRadio, on ? 1 : 0)) return false;
    portENTER_CRITICAL(&s_mux);
    s_radio_on = on;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

ui::LedColour colour() {
    portENTER_CRITICAL(&s_mux);
    const ui::LedColour v = s_colour;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

bool setColour(ui::LedColour colour) {
    if (!ui::ledColourValid((uint8_t)colour)) return false;
    if (!storeU8(kKeyColour, (uint8_t)colour)) return false;
    portENTER_CRITICAL(&s_mux);
    s_colour = colour;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void publish(bool screen_off, uint32_t unread, bool battery_available, bool charging, bool radio_up) {
    portENTER_CRITICAL(&s_mux);
    s_screen_off        = screen_off;
    s_unread            = unread;
    s_battery_available = battery_available;
    s_charging          = charging;
    s_radio_up          = radio_up;
    portEXIT_CRITICAL(&s_mux);
}

void setScreenOff(bool off) {
    portENTER_CRITICAL(&s_mux);
    s_screen_off = off;
    portEXIT_CRITICAL(&s_mux);
}

void restoreAutomatic() {
    portENTER_CRITICAL(&s_mux);
    s_stop = true;
    portEXIT_CRITICAL(&s_mux);
    // All dark, register 146 cleared, then automatic mode, BEFORE the handover (the task stops on its next
    // tick; this call is made on the main task and waits for any in-flight write).
    if (s_out_mutex != nullptr) xSemaphoreTake(s_out_mutex, portMAX_DELAY);
    writeAllDark();
    writeMessage(0);
    bsp_led_set_mode(true);
    s_manual = false;
    if (s_out_mutex != nullptr) xSemaphoreGive(s_out_mutex);
    ESP_LOGI(TAG, "[led] off (automatic, message cleared)");
}

}  // namespace led
}  // namespace om
