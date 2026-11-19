// SPDX-License-Identifier: MIT
// S515/EST-468: the message LED's device glue. See main/message_led.h for the split.
#include "message_led.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "bsp/led.h"
#include "message_led_rule.h"

namespace om {
namespace led {
namespace {

const char* TAG         = "om-led";
const char* kNamespace  = "omled";
const char* kKey        = "msgled";
constexpr uint32_t kTaskPeriodMs = 25;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

uint8_t  s_choice      = ui::kMessageLedDefault;
bool     s_screen_off  = false;
uint32_t s_unread      = 0;
uint8_t  s_preview_req = 0;   // 0 = none, 1..6 = the LED to preview
bool     s_stop        = false;
bool     s_started     = false;

SemaphoreHandle_t s_out_mutex = nullptr;   // one owner of the bsp_led_* calls at a time
bool              s_manual    = false;
uint8_t           s_prev_brightness = 100;

void writeAllDark() {
    for (uint32_t i = 0; i < ui::kMessageLedMax; ++i) bsp_led_set_pixel_rgb(i, 0, 0, 0);
    bsp_led_send();
}

// Apply one frame. Called ONLY by the task (and once by restoreAutomatic(), under the same mutex).
void applyFrame(const ui::MessageLedFrame& f) {
    if (s_out_mutex != nullptr) xSemaphoreTake(s_out_mutex, portMAX_DELAY);
    // S515/EST-468: leaveToLauncher() may have stopped the task between the copy and this call. Re-check under
    // the output mutex so the final state is always "all dark, automatic", never one last manual frame.
    bool stopped;
    portENTER_CRITICAL(&s_mux);
    stopped = s_stop;
    portEXIT_CRITICAL(&s_mux);
    if (stopped) {
        if (s_out_mutex != nullptr) xSemaphoreGive(s_out_mutex);
        return;
    }
    if (!f.manual) {
        if (s_manual) {
            writeAllDark();
            bsp_led_set_brightness(s_prev_brightness);   // the system indicators keep their old level
            bsp_led_set_mode(true);
            s_manual = false;
            ESP_LOGI(TAG, "[led] off");
        }
    } else {
        if (!s_manual) {
            // Enter manual mode at the chosen low brightness; remember what automatic mode used.
            uint8_t b = 100;
            if (bsp_led_get_brightness(&b) != ESP_OK || b == 0) b = 100;
            s_prev_brightness = b;
            bsp_led_set_mode(false);
            bsp_led_set_brightness(ui::kMessageLedBrightness);
            s_manual = true;
            ESP_LOGI(TAG, "[led] blink on (LED %u)", (unsigned)f.led);
        }
        // Writes only when the frame CHANGED (the task asks before calling), so there is no I2C traffic in
        // the dark gap between the 20 s bursts.
        for (uint32_t i = 0; i < ui::kMessageLedMax; ++i) bsp_led_set_pixel_rgb(i, 0, 0, 0);
        if (f.lit && f.led >= 1 && f.led <= ui::kMessageLedMax) {
            bsp_led_set_pixel_rgb(f.led - 1, ui::kMessageLedR, ui::kMessageLedG, ui::kMessageLedB);
        }
        bsp_led_send();
    }
    if (s_out_mutex != nullptr) xSemaphoreGive(s_out_mutex);
}

void ledTask(void*) {
    ui::MessageLed rule;
    for (;;) {
        bool     off;
        uint32_t unread;
        uint8_t  chosen;
        uint8_t  preview;
        bool     stop;
        portENTER_CRITICAL(&s_mux);
        off     = s_screen_off;
        unread  = s_unread;
        chosen  = s_choice;
        preview = s_preview_req;
        s_preview_req = 0;
        stop    = s_stop;
        portEXIT_CRITICAL(&s_mux);
        if (stop) {
            vTaskDelete(nullptr);
        }

        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (preview != 0) rule.preview(now, preview);
        ui::MessageLedIn in;
        in.screen_off = off;
        in.unread     = unread;
        in.led        = chosen;
        const ui::MessageLedFrame f = rule.step(now, in);
        if (f.changed) applyFrame(f);

        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

}  // namespace

void begin() {
    s_choice = ui::kMessageLedDefault;
    nvs_handle_t h = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, kKey, &v) == ESP_OK && ui::messageLedValid(v)) s_choice = v;
        nvs_close(h);
    }
    ESP_LOGI(TAG, "message LED: %s", ui::messageLedLabel(s_choice));

    // A previous run must never leave the system's Wi-Fi/power indicators dark.
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

uint8_t choice() {
    portENTER_CRITICAL(&s_mux);
    const uint8_t v = s_choice;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

bool setChoice(uint8_t led) {
    if (!ui::messageLedValid(led)) return false;
    nvs_handle_t h = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t r = nvs_set_u8(h, kKey, led);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "choice not saved (%s)", esp_err_to_name(r));
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    s_choice = led;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void requestPreview(uint8_t led) {
    if (led < 1 || led > ui::kMessageLedMax) return;
    portENTER_CRITICAL(&s_mux);
    s_preview_req = led;
    portEXIT_CRITICAL(&s_mux);
}

void publish(bool screen_off, uint32_t unread) {
    portENTER_CRITICAL(&s_mux);
    s_screen_off = screen_off;
    s_unread     = unread;
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
    // All dark, then automatic mode, BEFORE the handover (the task stops on its next tick; this call is
    // made on the main task and waits for any in-flight write).
    if (s_out_mutex != nullptr) xSemaphoreTake(s_out_mutex, portMAX_DELAY);
    writeAllDark();
    bsp_led_set_brightness(s_prev_brightness);
    bsp_led_set_mode(true);
    const bool was_manual = s_manual;
    s_manual = false;
    if (s_out_mutex != nullptr) xSemaphoreGive(s_out_mutex);
    if (was_manual) ESP_LOGI(TAG, "[led] off");
}

}  // namespace led
}  // namespace om
