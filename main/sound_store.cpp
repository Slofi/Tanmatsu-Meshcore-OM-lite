// SPDX-License-Identifier: MIT
// S514/EST-450: NVS glue for the sound settings. The rules and the byte format live in
// core/sound_events.h; nothing here decides what a sound means.
#include "sound_store.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

#include "volume_popup.h"

namespace om {
namespace soundstore {
namespace {

const char* TAG = "tm/snd";
const char* kNamespace = "mcsnd";
const char* kKey = "cfg";

nvs_handle_t                         s_nvs = 0;
bool                                 s_opened = false;
tm::soundcfg::SoundSettings          s_settings;
tm::volume::VolumeSave               s_volume_save;

bool writeAll(const tm::soundcfg::SoundSettings& s) {
    if (!s_opened) return false;
    uint8_t buf[tm::soundcfg::kSettingsBytes];
    const size_t n = tm::soundcfg::encodeSettings(s, buf, sizeof(buf));
    if (n == 0) return false;
    esp_err_t r = nvs_set_blob(s_nvs, kKey, buf, n);
    if (r == ESP_OK) r = nvs_commit(s_nvs);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "save failed (%s); the old stored value stays", esp_err_to_name(r));
        return false;
    }
    return true;
}

}  // namespace

bool begin() {
    // S514 (Claude): the "already open" check comes FIRST. begin() is called twice (main.cpp and
    // audio_out.cpp:153), and resetting to the defaults before this check threw the loaded choices away on
    // every boot (log: "settings loaded: ... volume=100" then "audio up: ... volume 60"); the next change
    // would then have stored defaults + that one change over the user's choices.
    if (s_opened) return true;
    s_settings = tm::soundcfg::defaultSettings();

    esp_err_t r = nvs_open(kNamespace, NVS_READWRITE, &s_nvs);
    if (r != ESP_OK) {
        // The app still runs and sounds with the defaults; only persistence is lost.
        ESP_LOGW(TAG, "NVS %s unavailable (%s): defaults, choices will not persist", kNamespace,
                 esp_err_to_name(r));
        return false;
    }
    s_opened = true;

    uint8_t buf[tm::soundcfg::kSettingsBytes];
    size_t  len = sizeof(buf);
    r = nvs_get_blob(s_nvs, kKey, buf, &len);
    if (r == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no stored settings: Sound On, volume %u, Boot Off", (unsigned)s_settings.volume);
        return true;
    }
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "stored settings unreadable (%s): defaults", esp_err_to_name(r));
        return true;
    }
    tm::soundcfg::SoundSettings loaded;
    if (!tm::soundcfg::decodeSettings(buf, len, &loaded)) {
        ESP_LOGW(TAG, "stored settings invalid (%u B): defaults", (unsigned)len);
        return true;
    }
    s_settings = loaded;
    ESP_LOGI(TAG, "settings loaded: master=%s volume=%u msg=%u dm=%u sent=%u err=%u newn=%u boot=%u",
             s_settings.master ? "on" : "off", (unsigned)s_settings.volume,
             (unsigned)s_settings.ids[(int)tm::soundcfg::Event::Message],
             (unsigned)s_settings.ids[(int)tm::soundcfg::Event::Dm],
             (unsigned)s_settings.ids[(int)tm::soundcfg::Event::Sent],
             (unsigned)s_settings.ids[(int)tm::soundcfg::Event::Error],
             (unsigned)s_settings.ids[(int)tm::soundcfg::Event::NewNode],
             (unsigned)s_settings.ids[(int)tm::soundcfg::Event::Boot]);
    return true;
}

const tm::soundcfg::SoundSettings& settings() { return s_settings; }

bool available() { return s_opened; }

bool setMaster(bool on) {
    tm::soundcfg::SoundSettings next = s_settings;
    next.master                      = on;
    if (!writeAll(next)) return false;
    s_settings = next;
    s_volume_save.clear();   // the blob just written holds any live volume change too
    return true;
}

bool setVolume(uint8_t volume) {
    if (volume > 100) return false;
    tm::soundcfg::SoundSettings next = s_settings;
    next.volume                      = volume;
    if (!writeAll(next)) return false;
    s_settings = next;
    s_volume_save.clear();   // an immediate write already carries the value
    return true;
}

// S516/EST-478: a volume KEY press. The live value moves at once (the Settings row and the tick read it),
// but NVS is only written by volumeTick() after ~2 s with no presses. That is why this is not
// setVolume(): the row's own +/- and Enter keep writing immediately, as before.
bool setVolumeLive(uint8_t volume, uint32_t now_ms) {
    if (volume > 100) return false;
    s_settings.volume = volume;
    s_volume_save.changed(volume, now_ms);
    return true;
}

// Any volume-key press (a clamp included) pushes the coalesced write later.
void volumeTouch(uint32_t now_ms) { s_volume_save.touch(now_ms); }

void volumeTick(uint32_t now_ms) {
    uint8_t pending = 0;
    if (!s_volume_save.take(now_ms, &pending)) return;
    if (!s_opened) return;
    if (!writeAll(s_settings)) {
        // Keep trying, but never in a tight loop: another full window.
        s_volume_save.changed(pending, now_ms);
        return;
    }
    ESP_LOGI(TAG, "volume %u saved (coalesced after the key burst)", (unsigned)pending);
}

bool setChoice(tm::soundcfg::Event ev, uint8_t id) {
    tm::soundcfg::SoundSettings next = s_settings;
    tm::soundcfg::setChoice(next, ev, id);
    if (!writeAll(next)) return false;
    s_settings = next;
    s_volume_save.clear();   // the blob just written holds any live volume change too
    return true;
}

}  // namespace soundstore
}  // namespace om
