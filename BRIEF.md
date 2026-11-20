# Tanmatsu: Launcher + OM Lite — brief for DeepSeek

> **Location:** `Projects/pocket-mesh/tanmatsu/BRIEF.md` (the Tanmatsu folder; the old path `pocket-mesh/tanmatsu-brief-ds.md` is a pointer here). Code and `docs/` for the Tanmatsu app go in this same folder.

> Written by Claude, S498 (2026-11-13), at Filip's request, before Claude's weekly usage runs out (~3 days).
> **Read this whole file before writing any code.** Filip will relay between you and him directly; Claude refines the work when the usage resets. Main project note: `pocket-mesh.md` (§ REBUILD, the L2 history); card: `pocket-mesh-overview.md`.

## 1. What Filip wants (his words, 2026-11-13)

*"Look over the OMlite and the CD's Launcher. … CD style launcher adjusted for Tanmatsu. For now, we will focus on the launcher and OMlite version for Tanmatsu. … I don't want a repeat of that 'retro look' he did on first try on L2. So the style stays the same as on OMlite on L2!! So a bit 'expanded' version of OMlite, adjusted to non-touch screen, but taking advantage of Tanmatsu 'extra' buttons (the first row of buttons - triangle, square, circle, 'cloud', diamond), those can be used to switch pages!"*

**Filip's order and notes (2026-11-13, added by Claude):**
- **The Launcher FIRST**, installed for now as an ordinary app in the stock launcher's **"Apps"** list ("for now ;)"). **Then OM Lite.**
- The mockup `file:///home/slofi/Projects/pocket-mesh/ui-mock/index.html` is the visual guide, **"but it may be changed a bit"**, and **the "icon" buttons there are wrong** (do not copy the mock's icon buttons; the five physical symbol keys are the page switch, see § 5).
- While Filip is away, work without the device: plan, previews, code and host tests; **no install** until he is there.

**Build now:** (1) the **Launcher** (CD style, inside our own Tanmatsu app) and (2) **OM Lite for Tanmatsu**.
**Not now:** Mesh-Torry, OPS-TOC Lite (later modules of the same launcher; design for them, do not build them).

## 2. The device (what we know, verified unless marked)

- **Tanmatsu** (Nicolai Electronics / badge.team, open hardware CERN-OHL-P, launcher MIT). Filip's unit arrived 2026-11-13.
- **ESP32-P4** main CPU (2× RISC-V 400 MHz, 32 MB PSRAM, 16 MB flash) runs the launcher and the apps.
- **ESP32-C6** network coprocessor (Wi-Fi 6, BLE, 802.15.4), "Radio v3.4.0" on Filip's unit.
- **CH32V203** coprocessor for the keyboard matrix and power ("Coprocessor r9").
- **Display 3.97" 800×480 MIPI-DSI, NO touch.** **69-key QWERTY**, including a **top row of five symbol keys: triangle, square, circle, "cloud" (tri-circle), diamond** (Filip's names).
- **LoRa module 868 MHz** with SMA (Filip confirmed 868). How the P4 reaches the LoRa radio: see WadaMesh `variants/tanmatsu` and the "tanmatsu-lora" bridge in `TANMATSU_PORT.md` (read it; do not assume SPI from the P4).
- **No GPS** (an external module later: Qwiic / PMOD / SAO / rear socket; design the position source as optional).
- **microSD**: Filip is buying a **32 GB** card (FAT32; 64 GB cards are exFAT). The L2's map pack (`tiles.pack` + `tiles.idx`, SLO, 945 MB) can be copied onto it.
- **Software:** the stock **badge.team launcher v0.2.8** (Filip's unit; v0.3.1 exists, the on-device update fails with "Failed to start download", not our problem now). **It is NOT multitasking**: it starts one app at a time from **AppFS**.
- **USB:** a WCH hub `1a86:8091` with **two** Espressif USB-JTAG units behind it, serials `30:ED:A0:E0:F2:91` and `98:A3:16:A6:9D:40` (most likely P4 and C6; which is which is not established). **Opening a serial port can reset an ESP32: never open these ports while Filip is updating or using the device.**
  **made by DeepSeek — start** (S499, 2026-11-13): both are now **established** — `30:ED…` = **ESP32-P4 rev v1.0, 16 MB**; `98:A3…` = **ESP32-C6 rev v0.2, 8 MB**. The tty number is **not stable** (it swapped between sessions) → address ports by `/dev/serial/by-id/…`. The P4's USB has **two modes**, toggled with **F6** on the home screen: **Debug** (`303a:1001`, a tty, what esptool needs) and **BadgeLink** (`16d0:0f9a "Tanmatsu"`, no tty). **made by DeepSeek — end**

## 3. The architecture (decided with Filip, S497; read `research-2026-11-11.md` § Tanmatsu shape)

**ONE Tanmatsu app of ours, started from the stock launcher, with its OWN launcher inside.** Do NOT replace the badge.team launcher (keeps the app store, other apps, and recovery). Inside our app:

- **The inner Launcher (CD style)**: a home screen listing the modules (OM Lite now; Mesh-Torry and OPS-TOC Lite as greyed "later" tiles, or not shown until they exist), each with a clear **running / stopped** state, and **Stop** that frees the module's memory **without a restart** (Filip asked for exactly that). Look at the CD launcher: `/home/slofi/Projects/cyberdeck/launcher/` (`app.py`, `templates/`, `static/`): how it lists apps, shows what runs, starts/stops them. Copy the *behaviour and clarity*, not its web technology.
- **Modules on a shared core:** radio + MeshCore, settings/NVS, SD + map tiles, clock, (later) GPS. A module allocates on start and returns EVERYTHING on stop (tasks ended, buffers freed, all big buffers in PSRAM). Add a start/stop host test per module (start → stop → start leaves the same free heap, within a few hundred bytes).
- **Honest limit to keep in the design:** no process isolation on an MCU; one module crashing restarts the whole app; a leaking module starves the others. So: one owner per resource (the core owns the radio), modules only talk to the core.

## 4. The LOOK — the most important section (Filip's veto)

**It must look like OM Lite on the L2, expanded to 800×480. NOT retro, NOT terminal-green, NOT pixel fonts, NOT a "badge demo" look.** On the L2's first try DeepSeek produced a "retro look" and it was thrown away; do not repeat it.

Take the style from what exists, in this order of authority:
1. **The L2 firmware as it runs today** (`/home/slofi/Projects/pocket-mesh/firmware/src/ui/theme.h`, `ui/layout.h`, the screen files) and its real screenshots in `firmware/docs/shots/` (2026-11-10 … 2026-11-12, e.g. `2026-11-11-nodes.png`, `-selector.png`, `-map.png`, `-settings.png`, `-rptr.png`, and the est414 scroll-indicator preview).
2. **The approved mockup** `/home/slofi/Projects/pocket-mesh/ui-mock/index.html`: it already has a **Tanmatsu form factor** (S459/S461: left sidebar, list + detail two-pane, focus ring, key-hint bar, typing straight into Chat). `preview.html` is the frozen approved copy.
3. Palette (Filip-approved 2026-11-10): accent **OM gold `#e8b04f`**, client nodes blue `#4aa3ff`, backgrounds neutral grey with a slight warm lean (bg `#131210`, panel `#1c1b19`, line `#333230`); the same status-bar elements (clock, GPS, battery, gear), the same row style, the same unread dot/badge rules, the same gold thumb scroll indicator and end bounce, the same "pressed" look.
4. Fonts: the L2 uses generated anti-aliased DejaVu Sans (`tools/mkfont.py`, `tools/mkvlw.py`); at 800×480 use larger sizes of the SAME family, never a pixel/bitmap font.

**"Expanded" means:** use the width for two panes (list left, detail right), show more per row (e.g. hops + SNR + last heard on the Nodes list, the full message on Chat), a wider map with a side panel; keep the spacing and proportions of the L2, not a denser "dashboard".

**Before ANY firmware UI code: produce host-rendered preview PNGs at 800×480** (extend `firmware/tools/preview.py` or make the Tanmatsu equivalent in the same shared-tool spirit) of: the Launcher home, OM Lite Nodes, Chat (list + open chat), Map, RPTR, Settings. Put them in `pocket-mesh/tanmatsu-shots/`. **Filip approves the look on these PNGs before the screens are built.** If he says "retro" or "not like the L2", stop and fix the look first.

## 5. Input: no touch, keys only

- **Focus-based navigation:** a visible **focus ring** (gold) on the selected element; arrow keys move focus; **Enter** = select/open/confirm; **Esc** (or Backspace when no text field) = back; typing goes straight into a focused text field (chat composer, editors).
- **The five symbol keys switch pages** (Filip's idea). Proposed mapping, **Filip may change it** (make it one table in one header):

  | Key | Page |
  |---|---|
  | triangle | Launcher home |
  | square | Nodes |
  | circle | Chat |
  | cloud | Map |
  | diamond | RPTR (Settings via a key hint / gear) |

  **made by DeepSeek — start (S516/EST-478, 2026-11-20, Filip's decision):** the map moved one up. **X (F1) =
  Launcher, triangle = Nodes, square = Chat, circle = Map, cloud = RPTR, diamond = Settings.** `,` is no longer
  a Settings key (outside a text field it does nothing, inside one it types a comma). The live map and codes
  are `tanmatsu/core/keys.h`; `docs/PLAN.md` § 5 carries the current table. **made by DeepSeek — end**

  Find the real key codes for these five keys from the Tanmatsu BSP / WadaMesh `variants/tanmatsu` (do not guess codes; log them once like we did for the CardKB).
- A **key-hint bar** at the bottom showing what the keys do on the current page (as in the mockup), same style as the L2 tab bar.
- Everything reachable without touch; no hidden "long press" UX that only works on a touch screen.
- The L2 rules carry over: a confirmation sheet for the settings that matter (EST-422 list), no change on a mistaken key press.

## 6. OM Lite for Tanmatsu: scope

The same four areas as on the L2, same behaviour, keyboard instead of touch:
- **Nodes:** list (favourites on top, hops, SNR, last heard), detail pane (Info), actions (DM, trace, favourite) from the keyboard.
- **Chat:** channels + DMs, unread dots/counts, message history on SD (the L2's history store design), reply/info, emoji optional, Slovenian š č ž; the composer uses the physical keyboard.
- **Map:** offline tiles from the SD pack (same pack format as the L2: `tiles.pack` + `tiles.idx`), markers, traces, pan with arrows, zoom with keys; no GPS needed (position optional).
- **RPTR:** scan (manual only), login (remembered passwords), pages, console.
- **Settings:** the L2's tabs (General, Nodes, Chat, Map, RPTR), radio region/preset (SLO 869.618 / BW 62.5 / SF 8), path hash, node name, Wi-Fi/BLE (default off), time sources.
**Reuse, do not rewrite:** the L2 code's pure logic files (`*_logic.h`, `ui/*_layout.h`, the history store, the timekeeper, channels, the tap/confirm rules) wherever they are hardware-free. MeshCore stays the upgradable library (`firmware/lib/meshcore`, `tools/update_meshcore.py`).

## 7. Where the code goes, and what must not break

- **Read first:** `/home/slofi/dev/wadamesh/TANMATSU_PORT.md`, `TANMATSU_SIDELOAD.md`, `variants/tanmatsu/` (how an Arduino/ESP-IDF app is built for the Tanmatsu, how it is installed into AppFS, the BSP, the LoRa bridge, the display driver). WadaMesh is a REFERENCE: take facts and the build/sideload method, write our own code (licence boundary, as on the L2).
- Put the Tanmatsu app in **`/home/slofi/Projects/pocket-mesh/tanmatsu/`** (its own build). Shared pure-logic code: include it from `firmware/src/` rather than copying, ONLY if that keeps the L2 build untouched; otherwise copy with a note.
- **The L2 must keep building and its test suite must stay green** (`cd firmware && sh test/host/run.sh`, the REBUILD.md build command). Run both after every change that touches shared files.
- The rules in `firmware/docs/REBUILD.md` § "How to work" apply here too (real exit codes, host tests armed against the old code, **no clutter**: one shared preview tool, no per-ticket scripts, PROGRESS entry written early). Keep a `tanmatsu/docs/PROGRESS.md` with the same discipline.
- **Rule 9 (public repo hygiene):** no coordinates, LAN addresses, SSIDs, passwords, keys, PINs or other people's messages in logs, docs or screenshots.

## 8. Installing on the device — SAFETY

- **Never** overwrite the badge.team launcher, never erase the whole flash, never flash the C6 radio firmware or the CH32 coprocessor. Our code goes in as an **AppFS app** by the documented sideload method (`TANMATSU_SIDELOAD.md`).
- **Before the first install:** write down the recovery path (how to remove our app, how to get back to the stock launcher) in `tanmatsu/docs/`.
- **Install only with Filip present and with his OK**, like every flash on the L2.
- The device is brand new: if anything unexpected happens (boot loop, launcher missing), stop and report; do not try fixes on a guess.

## 9. Work order (one step at a time, Filip approves each)

0. **Read + plan (no code):** this brief, the L2 code layout, the CD launcher, WadaMesh's Tanmatsu port. Write `tanmatsu/docs/PLAN.md`: the module/core structure, which L2 files are reused, the build + sideload method, the key-code plan. Filip reads it.
1. **Look first:** the 800×480 preview PNGs (§ 4). Filip approves the look.
2. **Bring-up "hello":** our app starts from the stock launcher, draws the approved status bar + launcher home at 800×480, reads the keyboard (log every key once, including the five symbol keys), reads the SD card, shows battery. Installed with Filip present.
3. **Inner launcher:** module registry, running/stopped, Stop frees memory (with the start/stop memory test), the symbol-key page switching.
4. **OM Lite core:** LoRa via the Tanmatsu's real path, MeshCore up with the L2's identity rules and SLO radio settings, channels, send/receive.
5. **Nodes + Chat** two-pane, with the SD history. 6. **Map** from the SD pack. 7. **RPTR.** 8. **Settings.**
After each step: build (real exit code), host tests, a preview or a real screenshot, a PROGRESS entry, and Filip's look before the next step.

## 10. Open / Filip decides

- The symbol-key mapping (proposal in § 5).
- Whether Mesh-Torry and OPS-TOC show as "later" tiles on the launcher home now.
- GPS module choice (later).
- The launcher update to v0.3.1 (separate; ask Nicolai on Discord if it keeps failing).
  **made by DeepSeek — start** (S499, 2026-11-13): **DONE — the unit runs launcher v0.3.1**, installed over USB by writing the release `launcher.bin` into the running slot (no data touched). The on-device route **cannot** work yet: `selfsigned.ota.tanmatsu.cloud` returns **HTTP 500 on IPv4** for every path while IPv6 serves the byte-identical file, and the device is IPv4-only — proof and the flashing recipe in `tanmatsu/docs/DEVICE.md`. Remaining: **send the report to Nicolai**. **made by DeepSeek — end**
