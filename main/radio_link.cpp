// SPDX-License-Identifier: MIT
// Device backend for om::radio — see radio_link.h for why it looks like this.
#include "radio_link.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "radio_plan.h"   // core/: the numbers live there, not here

extern "C" {
#include "lora.h"
}

namespace om {
namespace radio {
namespace {

const char* TAG = "tm/radio";

lora_handle_t s_handle = {};
bool          s_inited = false;   // lora_init_remote() is a singleton in the component
bool          s_started = false;

char          s_state[24] = "off";   // "RX" / "off" / "FAIL (...)" — the WHY, not the where
uint32_t      s_tx = 0;
uint32_t      s_rx = 0;
int           s_last_rssi = 0;
int           s_last_snr = 0;
lora_protocol_chip_t s_chip = LORA_PROTOCOL_CHIP_SX1262;
bool          s_chip_known = false;

// One packet buffer for both directions: 5 bytes of stats + 256 of payload.
lora_protocol_lora_packet_t s_pkt = {};

void setState(const char* s) { snprintf(s_state, sizeof(s_state), "%s", s); }

bool pushConfig(const tm::RadioPlan& p) {
    lora_protocol_config_params_t cfg = {};
    cfg.frequency                  = p.freq_hz;
    cfg.spreading_factor           = p.sf;
    cfg.bandwidth                  = p.bw_khz;      // integer kHz: 62 = the 62.5 kHz step
    cfg.coding_rate                = p.cr;           // 8 = 4/8
    cfg.sync_word                  = p.sync_word;    // 0x12, MeshCore
    cfg.preamble_length            = p.preamble;     // 32 for SF<=8, MeshCore's own rule
    cfg.power                      = p.power_dbm;
    cfg.ramp_time                  = p.ramp_us;
    cfg.crc_enabled                = p.crc;
    cfg.invert_iq                  = false;
    cfg.low_data_rate_optimization = p.ldro;
    cfg.rx_boost                   = p.rx_boost;
    cfg.use_dcdc                   = true;
    cfg.use_automatic_correction   = true;
    esp_err_t err                  = lora_set_config(&s_handle, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

}  // namespace

bool begin() {
    if (!s_started) {
        if (!tm::radio_plan_valid(tm::kSloPlan)) {
            ESP_LOGE(TAG, "the plan is outside the chip's limits: refusing to transmit");
            setState("FAIL (plan)");
            return false;
        }
        if (!s_inited) {
            esp_err_t err = lora_init_remote(&s_handle, 16);   // 16 RX slots
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "lora_init_remote failed: %s (is the C6 up?)", esp_err_to_name(err));
                setState("FAIL (C6)");
                return false;
            }
            s_inited = true;
        }
        // The chip identifies itself once, at bring-up, before we start polling RX.
        lora_protocol_status_params_t st = {};
        if (lora_get_status(&s_handle, &st) == ESP_OK) {
            s_chip       = st.chip_type;
            s_chip_known = true;
            ESP_LOGI(TAG, "chip %s, version %s, errors %u", chipName(), st.version_string, (unsigned)st.errors);
        }
        if (!pushConfig(tm::kSloPlan)) {
            setState("FAIL (config)");
            return false;
        }
        ESP_LOGI(TAG, "configured %.3f MHz bw %u kHz sf %u cr 4/%u sync 0x%02X preamble %u pwr %u dBm",
                 (double)tm::kSloPlan.freq_hz / 1000000.0, (unsigned)tm::kSloPlan.bw_khz, (unsigned)tm::kSloPlan.sf,
                 (unsigned)tm::kSloPlan.cr, (unsigned)tm::kSloPlan.sync_word,
                 (unsigned)tm::kSloPlan.preamble, (unsigned)tm::kSloPlan.power_dbm);
        esp_err_t err = lora_set_mode(&s_handle, LORA_PROTOCOL_MODE_RX);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "set RX failed: %s", esp_err_to_name(err));
            setState("FAIL (rx)");
            return false;
        }
        s_started = true;
        setState("RX");
        ESP_LOGI(TAG, "listening");
    }
    return s_started;
}

void end() {
    if (!s_started) return;
    // Standby, not deinit: the component's remote handle cannot be created twice, so the link stays up
    // and the next begin() only has to push the config again.
    lora_set_mode(&s_handle, LORA_PROTOCOL_MODE_STANDBY_RC);
    s_started = false;
    setState("off");
    ESP_LOGI(TAG, "stopped (tx %u rx %u)", (unsigned)s_tx, (unsigned)s_rx);
}

bool started() { return s_started; }

const char* chipName() {
    if (!s_chip_known) return "?";
    switch (s_chip) {
        case LORA_PROTOCOL_CHIP_SX1262: return "SX1262";
        case LORA_PROTOCOL_CHIP_SX1268: return "SX1268";
        default:                        return "?";
    }
}

int send(const uint8_t* data, size_t len) {
    if (!s_started || data == nullptr || len == 0 || len > sizeof(s_pkt.data)) return -1;
    s_pkt.length = (uint8_t)len;
    memcpy(s_pkt.data, data, len);
    s_pkt.stats = {};
    esp_err_t err = lora_send_packet(&s_handle, &s_pkt);
    // The C6 leaves TX on its own; ask for RX again so we do not sit deaf.
    lora_set_mode(&s_handle, LORA_PROTOCOL_MODE_RX);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(err));
        return -1;
    }
    s_tx++;
    return (int)len;
}

int poll(uint8_t* buf, size_t cap, int* rssi_dbm, int* snr_db, uint32_t wait_ms) {
    if (!s_started || buf == nullptr) return -1;
    s_pkt = {};
    if (lora_receive_packet(&s_handle, &s_pkt, pdMS_TO_TICKS(wait_ms)) != ESP_OK) return -1;
    size_t n = s_pkt.length < cap ? s_pkt.length : cap;
    memcpy(buf, s_pkt.data, n);
    // Per the component's header: rssi_dbm = -raw/2, snr_db = raw/4 (signed, 1/4 dB).
    s_last_rssi = -(int)s_pkt.stats.rssi_pkt_raw / 2;
    s_last_snr  = (int)s_pkt.stats.snr_pkt_raw / 4;
    // S513/EST-434: print the THREE raw bytes next to the converted values. The vendor's C6-side lora.c:442
    // writes rssi as rssi_dbm*4 into a uint8 (a negative float into an unsigned byte is 0 on RISC-V) and snr as
    // -snr*2, which contradicts its own header; these raw bytes are what decides which side is right. The
    // conversions above are deliberately unchanged until that is settled.
    ESP_LOGI(TAG, "rx %u B raw rssi %u snr %d signal %u -> rssi %d dBm, snr %d dB, signal %d dBm",
             (unsigned)n, (unsigned)s_pkt.stats.rssi_pkt_raw, (int)s_pkt.stats.snr_pkt_raw,
             (unsigned)s_pkt.stats.signal_rssi_pkt_raw, s_last_rssi, s_last_snr,
             -(int)s_pkt.stats.signal_rssi_pkt_raw / 2);
    if (rssi_dbm != nullptr) *rssi_dbm = s_last_rssi;
    if (snr_db != nullptr) *snr_db = s_last_snr;
    s_rx++;
    return (int)n;
}

uint32_t txCount() { return s_tx; }
uint32_t rxCount() { return s_rx; }
int lastRssiDbm() { return s_last_rssi; }
int lastSnrDb() { return s_last_snr; }

const char* statusText() {
    // Called once per UI refresh (2 s): the numbers stay live without the radio task having to
    // touch the screen.
    // 96, not 48: -Werror=format-truncation is right — the state can be 23 chars and two counters
    // can be 10 digits each.
    static char out[96];
    if (s_rx > 0) {
        snprintf(out, sizeof(out), "radio 869.618 · %s · %u/%u · %d dBm", s_state, (unsigned)s_tx,
                 (unsigned)s_rx, s_last_rssi);
    } else {
        snprintf(out, sizeof(out), "radio 869.618 · %s · %u/%u", s_state, (unsigned)s_tx, (unsigned)s_rx);
    }
    return out;
}

}  // namespace radio
}  // namespace om
