// SPDX-License-Identifier: MIT
// The only place in this app that touches the LoRa driver (PLAN §2 rule 1).
//
// On the Tanmatsu the SX1262 does NOT hang off the P4's SPI: it sits on the ESP32-C6 radio coprocessor and
// is reached "remote" over the esp-hosted link, which is what `nicolaielectronics/tanmatsu-lora` wraps.
// Consequences that shape this file:
//   * `lora_init_remote()` is a SINGLETON in the component (a static `remote_handle`; a second call returns
//     ESP_FAIL), so it is done once per boot, not once per module start.
//   * `lora_send_packet()` blocks until the C6 ACKs the transaction and leaves the radio out of RX, so we
//     put it back into RX ourselves.
//   * TX mode cannot be selected directly ("host must send data to transmit", `lora.c` local branch), which
//     is why there is no tx-mode call here at all.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace om {
namespace radio {

// Bring the link up with `tm::kSloPlan`, push the config and listen. Safe to call again after end().
bool begin();
void end();
bool started();

// "SX1262" / "SX1268" as the C6 reported it, or "?" if it never said.
const char* chipName();

// Bytes queued to the C6, or -1. Puts the radio back into RX afterwards.
int send(const uint8_t* data, size_t len);

// One packet from the RX queue: length, or -1 if nothing arrived within wait_ms. rssi/snr may be null.
int poll(uint8_t* buf, size_t cap, int* rssi_dbm, int* snr_db, uint32_t wait_ms);

// Counters and the last packet's numbers, for the status line.
uint32_t txCount();
uint32_t rxCount();
int lastRssiDbm();
int lastSnrDb();

// The status bar's short radio text, e.g. "radio 869.618 · RX" or "radio FAIL".
const char* statusText();

}  // namespace radio
}  // namespace om
