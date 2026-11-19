# Tanmatsu — device facts, update path and recovery

> **Written by DeepSeek (S499), 2026-11-13**, on the unit itself over USB, everything below **verified by a command
> run now** unless it says "not verified". Read this before flashing, updating or filing a device bug.
> The brief (`BRIEF.md`) stays the *what we build*; this file is the *what the hardware is and how to write to it*.

## 1. The two chips behind the internal USB hub

| | ESP32-P4 (launcher + apps) | ESP32-C6 (Wi-Fi/BLE/802.15.4 radio) |
|---|---|---|
| USB serial (MAC) | **`30:ED:A0:E0:F2:91`** | **`98:A3:16:A6:9D:40`** |
| Chip (esptool) | ESP32-P4 **rev v1.0**, dual core + LP core, 400 MHz | ESP32-C6 (QFN40) **rev v0.2**, single core + LP, 160 MHz |
| Flash detected | **16 MB** | **8 MB** |
| Debug-mode USB ID | `303a:1001` "USB JTAG/serial debug unit" → `/dev/ttyACM` | `303a:1001` → `/dev/ttyACM` |
| App-mode USB ID | `16d0:0f9a` **"Tanmatsu"** (Nicolai Electronics, BadgeLink, 1 vendor interface, **no tty**) | — |

Both sit behind the internal **WCH/QinHeng hub `1a86:8091`** (`lsusb -t`: ports 2-2.3 = P4, 2-2.4 = C6).

⚠️ **The tty name is NOT stable** — it depends on enumeration order and swapped between sessions
(S498 saw `30:ED…`→ttyACM0, S499 saw `30:ED…`→ttyACM1). **Always address the port through
`/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<SERIAL>-if00`**, never by ttyACM number.
⚠️ Reset the C6 and the hub re-enumerates **both** devices; the P4 can come back in BadgeLink mode (no tty) —
that is not a fault, see § 4.

⚠️ **Real incident, 2026-11-13 ~13:2x (Claude, recorded by DeepSeek):** the L2 tool (`cmd.py`, meant for the L2
Pro on `ttyACM2`) ignored the port it was given, opened **its default port — one of the Tanmatsu's two** — set
**DTR** and wrote the text `/dev/ttyACM2 status`. The P4 **reset** (its console then showed a full boot
sequence) and it re-enumerated cleanly: C6 `H_SDIO_DRV: Received INIT event`, coprocessor booted, radio
configured, Wi-Fi back at 192.168.64.116 — **no power cycle needed**. Nothing was lost: no install/flash/AppFS
write was in progress, and the launcher ignores lines that are not its own two tokens. Two lessons: **a DTR
pulse on a Tanmatsu port reboots a chip**, and a device tool must select by **serial** (`/dev/serial/by-id/…`),
never by tty number or a built-in default — the L2 is now picked by its serial (`84C7BB7…`).


## 2. Flash layout (from the release partition table, `tanmatsu.zip` → `partition-table.bin`)

    coredump   0x009000     64 KB
    nvs        0x019000     16 KB     ← Wi-Fi credentials + settings
    otadata    0x01d000      8 KB     ← which app slot boots
    phy_init   0x01f000      4 KB
    ota_0      0x020000   2.00 MiB    ← launcher slot A
    ota_1      0x220000   2.00 MiB    ← launcher slot B (blank on this unit)
    appfs      0x420000   8.00 MiB    ← installed apps (NOT touched by the launcher update)
    locfd      0xC20000   3.88 MiB    ← factory/config partition (NOT touched)
    (flash = 16 MB)

`merged-binary.bin` in the release is the **whole 16 MB** image — flashing it at 0x0 wipes appfs, NVS and
`locfd`. **Never use it for an update.**

## 3. State as shipped, and what S499 changed

- Shipped: **launcher v0.2.8** in `ota_0`, `ota_1` blank, otadata `seq=1 state=2 (VALID)` → boots `ota_0`.
  (Reading the otadata CRC only over `ota_seq` matched the stored value; the `esp_ota_select_entry_t` CRC over
  28 bytes did **not** match — the active slot is decided by `seq`, so nothing depends on this.)
- **2026-11-13 (S499, DeepSeek): updated to v0.3.1** by writing the release `launcher.bin` **in place into the
  running slot** — `esptool write-flash 0x20000` (this is the repo's own `make quickflash`). otadata, appfs,
  NVS and `locfd` untouched; otadata still `seq=1`, so the boot path did not change.
  - image: `v0.3.1`, project `launcher`, built `Nov 1 2026 20:29:27`, sha256
    `f0227419d67e457ab6d89b4b0ee915cadeb624fc68d0fa07074b9927edb75b29`
  - byte-identical to what the OTA server serves over IPv6 (§ 5) — so it is exactly what OTA would have installed
  - verified: esptool "Hash of data verified", read-back of the app descriptor says `v0.3.1`, and the boot log
    says `app_init: App version: v0.3.1` + `Bootloader is up-to-date` (the image's embedded bootloader already
    matches, nothing else was written).

**Backups from the shipped state** (for a rollback): `~/backups/tanmatsu-2026-11-13-S499/`
`slotA-launcher-v0.2.8.bin` (2 MiB), `otadata.bin`, `nvs.bin`, `slotA-head.bin`.

## 4. Writing to the device (the only sane method)

1. **Put the P4 in debug mode**: on the launcher's home screen press **F6** ("USB mode" in the footer bar).
   Toggle: Debug ↔ BadgeLink. esptool only works in **Debug** mode (`303a:1001`); in BadgeLink (`16d0:0f9a`)
   there is no serial port at all.
2. Flash the **running** slot (check `otadata` first, or simply `esptool flash-id` + read the descriptor):

       PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E0:F2:91-if00
       esptool -p $PORT read-flash 0x1d000 0x2000 otadata.bin          # confirm the active slot
       esptool -p $PORT write-flash 0x20000 tanmatsu/launcher.bin      # ota_0  (0x220000 = ota_1)
       esptool -p $PORT read-flash 0x20000 0x200 head.bin              # descriptor must say the new version

3. Do **not** touch `appfs`, `locfd`, `nvs`, the bootloader or the C6 unless that is the actual task.
   Secure boot / flash encryption: **no `CONFIG_SECURE_*` or encryption symbol** in either
   `sdkconfigs/general` or `sdkconfigs/tanmatsu` (checked), and the plaintext image written in S499 booted —
   which rules out flash encryption in practice.

**Rollback to the shipped v0.2.8**: `esptool -p $PORT write-flash 0x20000 ~/backups/tanmatsu-2026-11-13-S499/slotA-launcher-v0.2.8.bin`.
otadata was verified **byte-identical** before and after the S499 flash, so a rollback needs no otadata change.
If a write is ever interrupted: **not tested here**, but the ROM loader is reachable over USB-serial/JTAG with
BOOT + RESET even when no app boots (that is the documented ESP32 path, not something we exercised).

## 5. Why the on-device update fails: the OTA server's IPv4 side is broken (2026-11-13)

Device log of a real attempt (S499, captured over USB):

    I (4222) Custom certificates: Adding Tanmatsu OTA certificate to CA store
    I (16152) OTA update: Attempting to download update from https://selfsigned.ota.tanmatsu.cloud/tanmatsu-stable.bin
    E (16682) esp_https_ota: Server error (500)
    E (16682) esp_https_ota: Failed to establish HTTP connection
    E (16682) OTA update: ESP HTTPS OTA Begin failed
    OTA status changed: Failed to start download (0%)

What was measured, from two vantage points (both hosts share the same A/AAAA pair):

| | IPv4 `178.251.25.72` | IPv6 `2a00:1938:1:1::a053` |
|---|---|---|
| `selfsigned.ota.tanmatsu.cloud/` (root) | **500** | **200** |
| `…/tanmatsu-stable.bin` | **500** | **200**, 1,931,296 B, `Last-Modified 2026-11-01 20:38` |
| `ota.tanmatsu.cloud/radio-v3.4.0/instructions.json` | **500** | **200** |
| a **nonexistent** path on the OTA hosts | 500 | **500** (this vhost answers 500, not 404, for missing files) |
| `apps.tanmatsu.cloud/` and a nonexistent path there | 200 / **404** | 200 / 404 |
| TestBox (SI, LAN) | 500 | the LAN has no IPv6 at all |
| VPS-1 (DE, netcup) | 500 | 200 |

- **Both OTA vhosts are broken on IPv4 only**: every request over IPv4 returns 500, including `/`, from **two
  different countries/networks**. The same hosts over IPv6 serve the real files. `apps.tanmatsu.cloud` behaves
  correctly on both (proper 404 for a missing path) — so the server is up; it is the OTA vhosts' IPv4 path.
- The file over IPv6 is **byte-identical to the v0.3.1 `launcher.bin`** (sha256 `f0227419…b75b29`) → v0.3.1
  **is** published. Not our device, not our network, not the image.
- **TLS is fine**: the certificate the server presents equals the one baked into the firmware
  (`badgeteam/custom-certificates`, `tanmatsu_ota.pem`, sha256 `5B:21:3E:…:97:48`, valid to 2125). The device
  even logs "Adding Tanmatsu OTA certificate to CA store" before the request.
- The device's own Wi-Fi link is **IPv4-only** (`192.168.64.116/24`, no IPv6) → it can only ever take the broken
  path. The launcher has **no** alternative URL and **no** SD-card update path (checked in the menu source).
- `wifi_ota.c` / `firmware_update.c` are **identical in v0.2.8 and v0.3.1** → updating the launcher could never
  have fixed this.

**Fix belongs upstream**: report to Nicolai (Discord is fastest; the launcher repo is
`github.com/Nicolai-Electronics/tanmatsu-launcher`, open issue #3 "Add Github workflow for pushing releases to
OTA server" is related). Until then: update over USB (§ 4) or from a network that hands out IPv6.

### Draft report — not sent yet (DeepSeek, S499)

> Hi Nicolai, a Tanmatsu (launcher v0.2.8, everything else stock) cannot update itself:
> Settings → Firmware update → stable ends in "Failed to start download" within half a second.
> The device log says `esp_https_ota: Server error (500)` → `Failed to establish HTTP connection`.
> It is not the device: `https://selfsigned.ota.tanmatsu.cloud/tanmatsu-stable.bin` returns
> **HTTP 500 from nginx over IPv4** (178.251.25.72) for *every* path including `/`, from two different
> countries (Slovenia via a normal ISP, Germany via a netcup VPS). Over **IPv6** the same host returns
> **200** and the file is byte-identical to the `launcher.bin` in the v0.3.1 release (sha256 f0227419…),
> so the release did reach the OTA server. `ota.tanmatsu.cloud` (the radio/icons host) shows the same
> pattern: 500 on IPv4 for every path, 200 on IPv6. `apps.tanmatsu.cloud` answers correctly on both
> families (proper 404 for a missing path), so the server itself is fine — it looks like the IPv4
> listeners of the two OTA vhosts are misconfigured. Many home networks and the Tanmatsu's own Wi-Fi
> here are IPv4-only, so users cannot update at all. I installed v0.3.1 by writing `launcher.bin` to the
> running OTA slot over USB (`make quickflash`), which worked fine. Cheers!

## 5b. RSSI and SNR from the C6 radio are unusable (2026-11-18, S513; written by Claude) — report to Nicolai

**What we see** (OM Lite, `tanmatsu-lora` 0.6.0 over the C6, `Projects/pocket-mesh/measurements/tanmatsu-v40-first-dm-2026-11-18-S513.log`):
every received packet's stats arrive as **raw RSSI 0** and **raw signal-RSSI 0**; the SNR byte is non-zero but cannot
be decoded consistently (raw -54 and -105 read as -13 / -26 dB by the component's own header, and -26 dB is below the
floor SF8 can decode). Examples: `rx 114 B raw rssi 0 snr -54 signal 0`, `rx 120 B raw rssi 0 snr -105 signal 0`.

**What the component source shows** (`managed_components/nicolaielectronics__tanmatsu-lora/lora.c`): the header
documents `rssi_dbm = -rssi_pkt_raw/2`, `snr_db = snr_pkt_raw/4`, `signal_rssi_dbm = -signal_rssi_pkt_raw/2`, but the
radio-side path (`lora_radio_read_data`, ~line 442) writes `rssi_pkt_raw = rssi_pkt_dbm * 4.0f` (a NEGATIVE float into
a `uint8_t`, which comes out 0 on RISC-V), `snr_pkt_raw = -snr_pkt_db * 2.0f` and `signal_rssi_pkt_raw =
-signal_rssi_pkt_db * 2.0f`. The RSSI line alone explains the 0; the signal field is also 0 and the SNR scale does not
match either reading, so **we do not know which code the C6 firmware actually runs** — hence a report, not a patch.

**Ask for Nicolai:** which source the shipped C6 radio firmware is built from, and whether the three conversions match
the header. Until then OM Lite shows no RSSI and treats the SNR as unknown. *(Not sent yet; Filip sends it.)*

## 6. Other facts read off this unit

- LoRa: the radio reports **`SX1261 V2D 2D02`** (from the C6's lora coprocessor). ⚠️ **That string is the generic
  SX126x family identification — it does NOT prove SX1261 vs SX1262.** Our notes assume SX1262; treat the exact
  part as unverified until the module/silicon is looked at. 868 MHz module, SMA (Filip).
  **Added by DeepSeek (S499, same day):** the launcher's own boot log also prints **`W (6442) main: SX1262 LoRa
  radio detected`** beside that string (launcher v0.3.1 `main/main.c`, decided from the version string). Two
  launcher-side statements, still not a datasheet — the part stays marked unverified, but the balance now leans
  SX1262, agreeing with our existing notes.
- Display driver `st7701` v2.0.2, audio codec ES8156 present, no add-on board detected, HID keyboard host waits
  for a device (`hid_kbd: Waiting for HID Device to be connected`).
- Wi-Fi from the P4 goes through the C6 (`esp_hosted`/SDIO: `H_SDIO_DRV: Card init success`).
- Boot log noise that is normal: `E (6451) main: Failed to set LoRa mode: ESP_ERR_WIFI_NOT_STARTED` (boot order;
  the radio is set to RX a moment later), `W (4542) WiFi remote: Starting ESP hosted...`.
