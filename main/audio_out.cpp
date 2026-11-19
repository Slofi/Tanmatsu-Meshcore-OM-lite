// SPDX-License-Identifier: MIT
// S514/EST-450: the one owner of the Tanmatsu I2S channel. See audio_out.h for the split.
//
// WHY 16 kHz: sound_synth.h fixes kRate = 16000 and its low-pass / alias guard are written for an
// 8 kHz Nyquist. The BSP's own default is 44.1 kHz (targets/tanmatsu/badge_bsp_audio.c,
// initialize_i2s(44100)); playing the same samples at that clock would make every phrase ~2.76x too
// fast and too high. bsp_audio_set_rate() reconfigures only the I2S clock (badge_bsp_audio.c:64-67),
// which is exactly the one thing we need, so begin() asks for 16000 Hz.
//
// THE BSP ALREADY OWNS THE I2C CODEC: bsp_device_initialize() calls bsp_audio_initialize()
// (common/badge_bsp_device.c:84), which takes the primary I2C bus handle (badge_bsp_audio.c:70) and
// configures the ES8156 at 0x08. Calling it again would create a second I2S channel and run the codec
// bring-up twice, so this file takes the published handle and only falls back to the initializer if
// the BSP's attempt failed. The amplifier is a CH32 coprocessor command (badge_bsp_audio.c:103-107),
// the same coprocessor this app power-cycles for the C6, so it is only touched after boot is past.
#include "audio_out.h"

#include <string.h>

#include "bsp/audio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sound_store.h"

// bsp/audio.h does not declare the initializer (the BSP's own device init declares it in its .c);
// forward-declaring it is what lets us use it as the one fallback when the BSP's handle is missing.
extern "C" esp_err_t bsp_audio_initialize(void);

namespace om {
namespace audioout {
namespace {

const char* TAG = "tm/aud";

constexpr int kQueueLen = 3;   // one playing + a couple waiting; a full queue drops the newest
constexpr int kStageFrames = 256;
// The L2 waits 250 ms after enabling its ES8311 amplifier. This is a different amp behind the CH32;
// 30 ms is the first value to verify on the glass, and the synth's own 2..14 ms attack is the rest of
// the anti-click story. No sound ever starts or ends on a non-zero sample.
constexpr int kAmpSettleMs = 30;
constexpr int kTailSilenceMs = 30;

struct PlayRequest {
  uint8_t sound;
  uint8_t event;
};

i2s_chan_handle_t s_i2s = nullptr;
QueueHandle_t     s_queue = nullptr;
int16_t*          s_buf = nullptr;
int               s_bufSamples = 0;
bool              s_bufPsram = false;
bool              s_ready = false;
uint32_t          s_lastPlayMs = 0;
uint32_t          s_played = 0;
uint32_t          s_dropped = 0;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

bool enqueue(uint8_t sound, uint8_t event) {
  if (s_queue == nullptr) return false;
  const PlayRequest req{sound, event};
  if (xQueueSend(s_queue, &req, 0) != pdTRUE) {
    ++s_dropped;
    ESP_LOGW(TAG, "queue full, dropped %s sound %u (%lu dropped)",
             tm::soundcfg::eventKey((tm::soundcfg::Event)event), (unsigned)sound,
             (unsigned long)s_dropped);
    return false;
  }
  return true;
}

// The BSP configures the I2S slot as 16-bit STEREO (badge_bsp_audio.c:32-34), so one mono synth
// sample becomes the same value in the left and right slot.
bool writeMono(const int16_t* mono, int n) {
  static int16_t stage[kStageFrames * 2];
  for (int off = 0; off < n; off += kStageFrames) {
    int frames = n - off;
    if (frames > kStageFrames) frames = kStageFrames;
    for (int i = 0; i < frames; ++i) {
      stage[2 * i]     = mono[off + i];
      stage[2 * i + 1] = mono[off + i];
    }
    size_t    written = 0;
    esp_err_t err = i2s_channel_write(s_i2s, stage, (size_t)frames * 2 * sizeof(int16_t), &written,
                                      pdMS_TO_TICKS(1000));
    if (err != ESP_OK || written == 0) {
      ESP_LOGW(TAG, "i2s write stopped at %d/%d frames (%s)", off + frames, n, esp_err_to_name(err));
      return false;
    }
  }
  return true;
}

// Keep the amplifier on until the DMA has actually played the last sample, then give the hardware a
// moment of quiet before the amp goes off (no click at the end).
void writeTailSilence() {
  static int16_t zeros[kStageFrames * 2];
  int frames = (int)((om::sound::kRate * (uint32_t)kTailSilenceMs) / 1000u);
  while (frames > 0) {
    const int chunk = frames > kStageFrames ? kStageFrames : frames;
    size_t    written = 0;
    const esp_err_t err = i2s_channel_write(s_i2s, zeros, (size_t)chunk * 2 * sizeof(int16_t),
                                            &written, pdMS_TO_TICKS(1000));
    if (err != ESP_OK || written == 0) break;
    frames -= chunk;
  }
}

void audioTask(void*) {
  PlayRequest req{};
  for (;;) {
    if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;
    const uint8_t soundId = req.sound;
    if (!om::sound::validSound(soundId)) continue;

    om::sound::RenderInfo info{};
    const int64_t renderStart = esp_timer_get_time();
    const int n = om::sound::renderSound(soundId, s_buf, s_bufSamples, &info);
    const int64_t renderUs = esp_timer_get_time() - renderStart;
    if (n <= 0) {
      ESP_LOGW(TAG, "render failed for sound %u", (unsigned)soundId);
      continue;
    }

    bsp_audio_set_volume(om::soundstore::settings().volume);
    bsp_audio_set_amplifier(true);
    vTaskDelay(pdMS_TO_TICKS(kAmpSettleMs));
    const bool wrote = writeMono(s_buf, n);
    if (wrote) writeTailSilence();
    bsp_audio_set_amplifier(false);

    ++s_played;
    ESP_LOGI(TAG, "play %s sound=%u \"%s\" samples=%d peak=%d render_us=%lld stack=%u",
             tm::soundcfg::eventKey((tm::soundcfg::Event)req.event), (unsigned)soundId,
             om::sound::soundName(soundId) != nullptr ? om::sound::soundName(soundId) : "?",
             info.samples, info.peak, (long long)renderUs,
             (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
  }
}

}  // namespace

bool begin() {
  if (s_ready) return true;
  om::soundstore::begin();   // even if the audio path fails, the settings are readable

  i2s_chan_handle_t handle = nullptr;
  esp_err_t         err    = bsp_audio_get_i2s_handle(&handle);
  if (err != ESP_OK || handle == nullptr) {
    // The BSP's device init already tried (common/badge_bsp_device.c:84) and failed. Retry ONCE here,
    // now that the C6 link and the card are up; if this fails too, the app runs silent.
    ESP_LOGW(TAG, "BSP published no I2S handle (%s); trying bsp_audio_initialize once",
             esp_err_to_name(err));
    err = bsp_audio_initialize();
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "audio unavailable, running silent (%s)", esp_err_to_name(err));
      return false;
    }
    err = bsp_audio_get_i2s_handle(&handle);
  }
  if (err != ESP_OK || handle == nullptr) {
    ESP_LOGW(TAG, "audio unavailable, running silent (no I2S handle)");
    return false;
  }
  s_i2s = handle;

  // 16 kHz is the synth's own rate (sound_synth.h: kRate); see the header comment for why 44.1 kHz is
  // wrong here. If the clock cannot be changed, silence is safer than a chipmunk chime.
  // S514 (Claude, review): the BSP has already ENABLED the channel (badge_bsp_audio.c:56), and ESP-IDF only
  // changes the clock while the channel is stopped (i2s_std.h:321-330, "i2s_channel_disable should be called
  // before calling this function if I2S has started"); without this the call returns ESP_ERR_INVALID_STATE and
  // the app runs silent for ever. Stop, change, start again.
  (void)i2s_channel_disable(handle);   // ESP_ERR_INVALID_STATE here just means it was not running
  err = bsp_audio_set_rate(om::sound::kRate);
  const esp_err_t en = i2s_channel_enable(handle);
  if (err == ESP_OK && en != ESP_OK) err = en;
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "audio unavailable: cannot set %u Hz (%s)", (unsigned)om::sound::kRate,
             esp_err_to_name(err));
    s_i2s = nullptr;
    return false;
  }
  bsp_audio_set_volume(om::soundstore::settings().volume);
  bsp_audio_set_amplifier(false);   // no hiss while nothing is playing

  s_buf = (int16_t*)heap_caps_malloc((size_t)om::sound::kMaxSamples * sizeof(int16_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_bufSamples = om::sound::kMaxSamples;
  s_bufPsram   = s_buf != nullptr;
  if (s_buf == nullptr) {
    // Same fallback as the L2 (EST-397): a quarter of the phrase in internal RAM, cut by the synth's
    // normal tail fade, rather than 48 KB of internal heap.
    s_bufSamples = om::sound::kMaxSamples / 4;
    s_buf = (int16_t*)heap_caps_malloc((size_t)s_bufSamples * sizeof(int16_t),
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  s_queue = xQueueCreate(kQueueLen, sizeof(PlayRequest));
  if (s_buf == nullptr || s_queue == nullptr) {
    ESP_LOGW(TAG, "audio unavailable, running silent (buffer=%s queue=%s)",
             s_buf != nullptr ? "ok" : "NULL", s_queue != nullptr ? "ok" : "NULL");
    if (s_buf != nullptr) heap_caps_free(s_buf);
    if (s_queue != nullptr) vQueueDelete(s_queue);
    s_buf = nullptr;
    s_queue = nullptr;
    s_i2s   = nullptr;
    return false;
  }
  if (xTaskCreate(audioTask, "om-audio", 4096, nullptr, 2, nullptr) != pdPASS) {
    ESP_LOGW(TAG, "audio unavailable, running silent (task)");
    heap_caps_free(s_buf);
    vQueueDelete(s_queue);
    s_buf = nullptr;
    s_queue = nullptr;
    s_i2s   = nullptr;
    return false;
  }

  s_lastPlayMs = nowMs() - tm::soundcfg::kSoundMinGapMs;
  s_ready      = true;
  ESP_LOGI(TAG, "audio up: %u Hz, %d samples (%u B) in %s, queue %d, volume %u", (unsigned)om::sound::kRate,
           s_bufSamples, (unsigned)((size_t)s_bufSamples * sizeof(int16_t)),
           s_bufPsram ? "PSRAM" : "INTERNAL (PSRAM refused)", kQueueLen,
           (unsigned)om::soundstore::settings().volume);
  return true;
}

bool ready() { return s_ready; }

bool playEvent(tm::soundcfg::Event ev, bool newnode_notice_enabled) {
  if (!s_ready) return false;
  const tm::soundcfg::SoundSettings& st = om::soundstore::settings();
  if (!tm::soundcfg::shouldPlay(st, ev, false, false, false, newnode_notice_enabled)) return false;
  const uint32_t now = nowMs();
  if (!tm::soundcfg::rateDue(s_lastPlayMs, now)) {
    ESP_LOGD(TAG, "rate limit: %lu ms since the last sound", (unsigned long)(now - s_lastPlayMs));
    return false;
  }
  const uint8_t id = st.ids[(int)ev];
  if (!enqueue(id, (uint8_t)ev)) return false;
  s_lastPlayMs = now;
  return true;
}

bool preview(uint8_t soundId) {
  if (!s_ready) return false;
  if (!om::soundstore::settings().master) return false;
  if (soundId == om::sound::kSoundOff || !om::sound::validSound(soundId)) return false;
  return enqueue(soundId, (uint8_t)tm::soundcfg::Event::Message);
}

void setVolume(uint8_t percent) {
  if (!s_ready) return;
  bsp_audio_set_volume(percent > 100 ? 100 : percent);
}

uint32_t playedCount() { return s_played; }
uint32_t droppedCount() { return s_dropped; }

}  // namespace audioout
}  // namespace om
