# Tanmatsu — install and recovery (write this before the first install)

> **Written by DeepSeek (S499), 2026-11-13**, per `BRIEF.md` § 8 ("before the first install: write down the
> recovery path"). Method and citations from the WadaMesh reference (`~/dev/wadamesh` @ `53d17d0`,
> `TANMATSU_SIDELOAD.md` + `tanmatsu/tools/`), the launcher source v0.3.1 (`Nicolai-Electronics/tanmatsu-launcher`),
> and the device facts in `DEVICE.md`. Everything marked *(checked)* was read in source or run on this unit.

## 0. The one-line rule

**Our program is an AppFS app under the stock launcher, never firmware.** We write **one app image + one
metadata sector** inside the `appfs` partition and nothing else. **Never `idf.py flash`** — that writes the
bootloader, the partition table and the OTA slots, i.e. the launcher OS itself.

What we never touch: `bootloader` (0x2000), `partition-table` (0x8000), `otadata` (0x1D000), `ota_0`/`ota_1`
(the launcher, 0x20000 / 0x220000), `nvs` (0x19000), `locfd` (0xC20000).

## 1. What "installed" means

- AppFS lives at **0x420000, 8 MB** (`16M.csv`; re-checked against the release partition table in `DEVICE.md` § 2 *(checked)*).
- The launcher indexes apps by **name + version** and lists them under **Apps**; the AppFS metadata sector (the
  first 64 KB of the partition) is what makes an app visible.
- **The version must increase on every deploy**, or the launcher keeps the old binary silently *(checked:
  `tan_deploy.py:51-52`, `TANMATSU_SIDELOAD.md`)*.
- Size ceiling: the launcher maps at most **50 × 64 KB = 3200 KB** of an app *(checked: PLAN § 6.3)*.

## 2. Before installing (once per install round)

1. **Put the P4 in Debug mode**: launcher home → **F6** ("USB mode" in the footer bar). BadgeLink mode gives no
   serial port at all (`DEVICE.md` § 1).
2. **Take a fresh AppFS dump** — this is the diff baseline, so it must describe *this* device *now*:

       PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:E0:F2:91-if00
       esptool -p $PORT read-flash 0x420000 0x800000 appfs-pristine.bin

   Store it **outside** the synced tree (8 MB; e.g. `~/backups/tanmatsu-<date>/`). ⚠️ A stale or foreign dump
   makes the deploy overwrite apps it does not know about — always re-dump.
3. `fetch-appfs.sh` (once per machine) to get badge.team's `appfs.py`. It is not vendored (no licence header on
   the circulating copy).
4. C6 heads-up: **resetting the P4 desynchronises the C6**, and the C6 has no reset line — the launcher can then
   hang on "Initializing radio" and only a **full power cycle** (off, then on) clears it *(checked:
   `TANMATSU_SIDELOAD.md` § Verify)*.

## 3. Install

    cd tanmatsu && ./build.sh build            # real exit code; output build/tanmatsu/application.bin
    ESPTOOL=esptool OUT=/tmp ./tools/tan_flash.sh $PORT

What that does, in this order *(checked: `tan_flash.sh`, `tan_deploy.py`, `TANMATSU_SIDELOAD.md`)*:

1. Builds the AppFS image = pristine dump + our `application.bin`, then keeps only the changed 64 KB sectors:
   `appfs_app.bin` (app data) and `appfs_meta.bin` (metadata sector).
2. Writes the **app data first** (`--after no_reset`).
3. Writes the **metadata sector last**, then resets — that write is what commits the install.
4. Reads the metadata back and `cmp`s it. Expected output: **`metadata verified`**. Anything else:
   re-run; nothing is broken, the device still has the previous app.

Then: **Launcher → Apps → our app** and launch it. An empty serial log right after the flash is normal.

## 4. Removing our app (three ways, least invasive first)

1. **From the launcher (the normal way, needs nothing):** **Apps → our app → F5 Remove**, then confirm the
   "Delete App — Do you really want to delete the app?" dialog. It calls `app_mgmt_uninstall()` for internal and
   SD locations *(checked: launcher `main/menu/apps.c:586-598`)*. The footer also shows `AppFS: N%` free.
2. **Restore the metadata sector from the pre-install dump** (device in Debug mode, Filip present):

       dd if=appfs-pristine.bin of=meta.pristine.bin bs=65536 count=1
       esptool -p $PORT write-flash 0x420000 meta.pristine.bin

   The launcher then no longer sees our app; its data sectors stay in AppFS unreferenced until the allocator
   reuses them (harmless — that is exactly the "interrupted install" state the tooling is designed around).
3. **Write the whole pristine AppFS back** (only if something in the partition is really wrong):

       esptool -p $PORT write-flash 0x420000 appfs-pristine.bin

## 5. When the device looks stuck

| symptom | what it is | what to do |
|---|---|---|
| no serial ports at all | the C6 has wedged | **full power cycle** (off, not just reset), then retry |
| "No ESP32-P4 found" in the tool, but ports exist | something else holds the port (a leftover monitor) | close it, retry |
| the launcher hangs on "Initializing radio" | C6 desynced by a P4 reset | full power cycle |
| our app crashes / hangs | the launcher is still the OS | **hold the power button / power cycle**; the launcher boots again from `ota_0`, which we never write |
| **app panics, then the NEXT boot hangs ~1.6 s in with the old screen still showing** | a panic skips our exit path, and OUR EXIT PATH is what power-cycles the C6 — so the next boot blocks in radio init (`bsp_tanmatsu_coprocessor_get_handle` / `hostedConnectC6`). Measured twice on 2026-11-13 (S501) | **full power cycle** (switch off, on). A P4 reset is NOT enough: the C6 keeps its state. Then the app boots normally — in S501 the identity and channels were intact |
| "metadata DIFFERS" | the commit write did not land | re-run the install; the device is untouched |
| a crash to decode | — | `riscv32-esp-elf-addr2line -e build/tanmatsu/application.elf -fpC <MEPC> <RA> <stack RAs>` |

**The launcher itself** needs no recovery path of ours: it lives in `ota_0`, we never write it, and S499 proved
the write-back works (`~/backups/tanmatsu-2026-11-13-S499/slotA-launcher-v0.2.8.bin`, plus the v0.3.1 image
already flashed). If it ever *did* get damaged: `DEVICE.md` § 4 has the exact command.

## 6. If something is unexpected

Stop and report — do not try a fix on a guess (BRIEF § 8). Report the **screen text verbatim**, the last serial
log lines, and which of the steps above was the last one that printed its expected line.
