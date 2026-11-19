# Tanmatsu: PLAN (work-order step 0)

> **Written by DeepSeek (S499, EST-424 ticket run), 2026-11-13**, at Claude's brief `tanmatsu/BRIEF.md` (S498).
> Fenced Paperclip run: no device, no install, no commits. Read the PROGRESS entry next to this file for
> what was actually read.
> **§ 6 was rewritten by DeepSeek (EST-425, S500) from the WadaMesh reference now on disk**
> (`paperclip-pilot/sweep/tanmatsu-ref/wadamesh-53d17d0/`), so the build/sideload/radio sections are cited
> `file:line` rather than reconstructed. The EST-424 gap ("WadaMesh is not on this machine") is closed.

Scope of this plan: the shape of our one Tanmatsu app — the core, the modules, the inner Launcher, and
how we build and install it without touching anything that can brick the device. Filip's look comes first
(`shots/`), so nothing here commits a pixel of the UI: the UI is the approved previews, this is the
skeleton under them.

`[unverified]` marks every statement I could not check from this machine (the ones that remain are listed in
§ 8, so none is lost in the prose).

---

## 1. Shape in one paragraph

ONE app of ours, started from the stock badge.team launcher as an ordinary **Apps** entry ("for now ;)",
Filip). Inside it, our own **Launcher** page lists the modules (OM Lite now; Mesh-Torry and OPS-TOC Lite
later) with an explicit **running / stopped** state and a **Stop** that frees the module's memory without
restarting the app. Under the modules sits one **core** that owns every shared resource — the radio and
MeshCore session, settings/NVS, the SD card and map tiles, the clock — and modules reach resources only
through the core. Nothing is a process: one module that crashes takes the app down, and a leaking module
starves the others, so start/stop is not a convenience feature, it is the memory discipline.

**Decision S514 (2026-11-19, Filip; written by Claude): ONE app, modules on the shared core.** Filip asked
whether OM Lite, OPS-TOC and Mesh-Torry should be one app or three, and decided: *"I agree, it is better as one
app, but I wasn't sure it would work.."*. Why: OPS-TOC and Mesh-Torry need what OM Lite already owns (the radio,
the MeshCore identity and contacts, the SD map packs); in one app they share one copy, switching is instant and the
mesh keeps running behind another module; three AppFS apps would each duplicate radio/MeshCore/map code in an 8 MB
AppFS that also holds other apps, and switching costs a restart (~6.5 s to the first frame). **Limits to watch:**
3,200 KiB per app (v49 ≈ 1,496 KiB, ~47 %) and internal RAM. **Split a module out** only if one of those runs short
(Mesh-Torry's Wi-Fi/TLS is the likely trigger). The in-app start menu stays (three app cards, S514 v39), and each
module brings its own Settings tab through the module settings hook (S514 EST-448).

## 2. Structure (module / core)

```
tanmatsu/
  CMakeLists.txt            IDF project entry (§ 6.2/6.3; NOT a platformio.ini — that route is dropped)
  main/                     app_main + idf_component.yml (badge-bsp, tanmatsu-lora, arduino-esp32, …)
  components/app/           our sources, one IDF component (mirrors wadamesh_app's wrapper)
  sdkconfigs/               general + tanmatsu overlay (partition table, PSRAM, esp-hosted SDIO)
  src/
    main.cpp                app entry: init core, run Launcher page, own the main loop
    core/
      core.h/.cpp           the ONE owner: init/shutdown order, the resource handles
      radio.h/.cpp          LoRa via the Tanmatsu's real path (§ 6) + MeshCore session
      mesh.h/.cpp           MeshCore facade (the L2's mesh_link.h contract, hardware-free parts)
      store.h/.cpp          settings/NVS + identity (node name, path hash, radio preset)
      sdcard.h/.cpp         mount, the tile pack, the history store's file backend
      clock.h/.cpp          time, time sources, the timekeeper rules
      budget.h              heap/PSRAM accounting: the number the start/stop test compares
    module/
      module.h              the interface every module implements (below)
      registry.h/.cpp       the table the Launcher renders: id, name, state, start/stop
      om_lite/…             OM Lite (own subtree, mirrors the L2 screens)
    ui/
      tm_theme.h            the 800x480 metrics + which L2 tokens carry over (§ 4)
      tm_layout.h           the page rectangles (the L2's ui/layout.h, at 800x480)
      widgets.cpp           bars, rows, focus ring, chips, scroll indicator
      keys.h/.cpp           the ONE key table (§ 5)
      page_launcher.cpp     the Launcher page  (work-order step 3)
      page_*.cpp            OM Lite pages      (steps 5-8)
    preview/                (host only) nothing here runs on the device
  tools/preview_tanmatsu.py the ONE preview tool (this run)
  docs/                     BRIEF.md, PLAN.md, PROGRESS.md, RECOVERY.md
  shots/                    800x480 host renders
```

The module interface, deliberately tiny — it is the whole safety story:

```cpp
struct Module {
  const char* id;            // "om-lite"
  const char* name;          // "OM Lite"
  const char* meta;          // one dim line under the name in the Launcher list
  bool available;            // false = the "later" row, not startable
  bool (*start)(Core&);      // allocate: tasks, buffers (PSRAM), subscriptions
  void (*stop)(Core&);       // free EVERYTHING: tasks ended, buffers released, subs dropped
  void (*tick)(Core&);       // called only while running
  void (*draw)(Canvas&);     // called only while running
  size_t heap_after_start;   // recorded by the test, not by the module
};
```

Rules the code must enforce (they are the difference between "several apps" and "one app that dies"):
1. **One owner per resource.** Only `core/radio` talks to the LoRa driver and MeshCore. A module holds a
   *handle*, never a driver.
2. **`stop()` returns the exact set `start()` took.** No lazy singletons, no static buffers in a module.
   Big buffers come from the core's PSRAM arena, so "freed" is observable.
3. **The Launcher never runs while a module owns the screen.** Only the Launcher page, or one module page.
   Page switching (five symbol keys) is a core-level route, so a module cannot swallow a key.
4. **Start/stop is testable on the host**: a fake `Core` counts allocations, so
   `start -> stop -> start` must leave the same count and the same free-heap number (a few hundred bytes
   of slack, as the brief says). One test per module, in `test/host/`, run by the same `run.sh` spirit.

The Launcher page shows what the CD launcher taught (`cyberdeck/launcher`: a card per app, a status dot,
a stop ×, a "release" bar for the shared resource), translated to keys: a **module list** with a state
chip per row (`RUNNING` / `STOPPED` / `LATER`), a **detail pane** with the module's memory and what it
owns, and `↵ Open` / `s Stop`. The core's own line ("core ready · radio free") is the release bar.
See `shots/2026-11-13-launcher-home.png`.

## 3. What the L2 gives us, file by file

The L2 is a 320x240 touch device; the Tanmatsu is 800x480 with a keyboard. The *logic* travels, the
*input and drawing* do not. Rule of the house: **include, do not copy, wherever the file is hardware-free**
— and only if the L2 build stays untouched (BRIEF § 7). Where the file is touch- or panel-specific, we
write a Tanmatsu sibling and say so.

| L2 file | How we use it |
|---|---|
| `ui/theme.h` | **Colours, verbatim, by parsing/including.** The preview tool reads its `rgb565()` tokens so the palette cannot fork. Sizes are NOT reused (320x240); the 800x480 metrics go in `ui/tm_theme.h`. |
| `ui/layout.h` | Reference for the rectangle arithmetic; a Tanmatsu sibling `tm_layout.h` with the same field names, so screen code reads the same. |
| `ui/widgets.{h,cpp}` | Included where it draws through the panel abstraction; the parts that hard-code 320 px or touch hit-testing get a Tanmatsu sibling. |
| `ui/tab_bar.h` | Style reference for the key-hint bar (BRIEF § 5: "same style as the L2 tab bar"). New code: `hint_bar()` showing this page's keys. |
| `ui/scroll_indicator.{h,cpp}` | **Reuse as-is** (EST-414): the gold thumb, the track width, the min thumb height — pure geometry. |
| `ui/status_cells.h`, `icons.{h,cpp}`, `battery_bolt.h` | Reuse the *drawing rules* (order, insets, ink). **GPS cell: not drawn while no module is present** — the Tanmatsu has no GPS (see § 7 Q4). |
| `channels.{h,cpp}`, `chat_model.h`, `chat_layout.{h,cpp}`, `chat_selector_*`, `msg_order.h`, `text_wrap.{h,cpp}` | **Reuse**: pure model/layout, no touch. The two-pane Chat changes the rectangles, not the rules. |
| `history_store.{h,cpp}`, `history/` | **Reuse**: the SD history design (BRIEF § 6). Only its file backend is swapped for the Tanmatsu SD mount. |
| `timekeeper.{h,cpp}`, `clock_keep.h` | **Reuse.** Time sources are a core concern. |
| `age.h`, `session`-style age strings, `emoji*` | **Reuse** `age.h`; emoji optional in § 6, so the picker comes later. |
| `press.h`, `tap_rule.h`, `long_press.h`, `delete_arm.h` | The **rules** carry over (press feedback became the touch substitute on the L2); on the Tanmatsu they map to key press-render + confirm-once. `delete_arm.h` (armed delete) is reused as-is: it is the "no change on a mistaken key press" rule (BRIEF § 5). |
| `keynav_logic.h` | **The nearest thing to our input model already exists** — read it first when step 3 starts; it is the L2's keyboard/BLE-keyboard focus logic. |
| `node_info*.h`, `nodes_header.h`, `node_menu.cpp` | Layout reference for the detail pane and the action sheet. |
| `map_*` (`map_layers.h`, `map_pick.cpp`, `map_trace.*`, `map_chrome.*`) | **Reuse the pure parts** (layer model, trace geometry, tile index reading). Pan/zoom becomes keys; `map_pick` (touch) is replaced. |
| `rptr_*` | **Reuse `rptr_model`, `rptr_page`, `rptr_layout`** logic; the console view is a scrolling text pane with keys. |
| `settings_*` (`settings_prefs.h`, `settings_actions.h`, `settings_layout.h`) | **Reuse** prefs/actions (EST-422 confirmations included); layout gets a Tanmatsu sibling. |
| `sd_budget.h`, `psram_alloc.h`, `mem_stats.h` | **Reuse.** These are the numbers the start/stop test prints. |
| `radio*.{h,cpp}`, `mesh_link.*`, `lib/meshcore` | **Not reused as code above the driver line.** `lib/meshcore` is reused as the library (upgradable, `tools/update_meshcore.py`); `mesh_link` is the contract to keep; the radio driver is new (§ 6). |
| `board.{h,cpp}`, `touch_gate.*`, `fade.*`, `screen_change.*`, `cardkb.*`, `usb_touch.*` | **Not reused.** Panel, touch, backlight and the CardKB are L2 hardware. |

**Licence boundary (as on the L2):** WadaMesh stays a reference for *facts*; our code is ours. The L2
files are ours already.

## 4. The look, as a contract

`shots/` holds the **six** host renders at 800x480: Launcher home, OM Lite Nodes, OM Lite Chat, Map, RPTR,
Settings (+ its confirm sheet) (`tools/preview_tanmatsu.py`, one tool, palette parsed from `theme.h`). The
rules the previews encode, so step 2 can be checked against them:

- **Two bars, always:** the L2's status bar (clock left; right: radio, battery + %, gear) and the
  **key-hint bar** at the floor where the L2's tab bar was. Status bar 26 px, hint bar 24 px.
- **Surfaces:** `kScreen #100f0d` page, `kPanel #1c1b19` plates, `kBar #171614` bars, `kLine` separators,
  `kRowLine` row bottoms. Gold `#e8b04f` only for: focus ring, selected text, the primary action, the
  scroll thumb, the current page's symbol key.
- **Focus ring:** 2 px gold, drawn on the focused element's own rectangle (the mock's `.row.sel`).
  Nothing else is ever gold-filled except the primary button's plate (`kSelBg2`).
- **Type:** the same DejaVu family, larger: title 22-26, row name 16-17, body 13, meta 12 mono,
  section labels 11 mono caps with tracking. Never a pixel font (BRIEF § 4).
- **Rows** keep the L2's anatomy: type dot, name, right-aligned meta, 1 px bottom line, 64 px tall at 800.
- **No icons-as-buttons.** The five page switches are physical keys; the UI shows them as sigils in the
  hint bar (`triangle square circle cloud diamond`), drawn as vectors, never as a font glyph.

## 5. Input: the arrows and the five symbol keys

**The model.** Focus-based, no touch: a focus ring on the selected thing; arrows move; `Enter` selects /
opens / confirms; `Esc` (or `Backspace` when no field is focused) goes back; typing goes straight into a
focused text field. The five symbol keys are a global page route. Nothing needs a long press.

**One table, one header.** `src/ui/keys.h` holds the ONLY key table:

```cpp
// Key ids are whatever the BSP reports. They are FILLED IN on the glass by work-order step 2's key
// logger (we log every key once, like the CardKB), never typed from memory.
enum class Key : uint16_t { None, Up, Down, Left, Right, Enter, Esc, Backspace,
                            SymTriangle, SymSquare, SymCircle, SymCloud, SymDiamond, Char };
Page page_for_symbol(Key k);     // the five-key route, one switch, nothing else reads a Symbol key
```

The proposed route (BRIEF § 5, **Filip may change it; if he does, only this table changes**):

| Key (Filip's name) | Page |
|---|---|
| triangle | Launcher home |
| square | Nodes |
| circle | Chat |
| cloud | Map |
| diamond | RPTR (Settings via the hint bar's gear key) |

**Two things must not be guessed, and both are step-2 work:**
1. **The codes.** Nothing in our notes records what the BSP reports for the top-row symbol keys or the
   arrows (grepped: no keycode table exists). WadaMesh's `variants/tanmatsu` would have them and the BSP
   is the authority — **neither is readable from this machine** (§ 6). So: no codes in this plan at all.
   Step 2's key logger prints `scancode / ascii / pressed` for every key and that log becomes the table.
2. **Scan-code stability across a launcher/BSP update.** If the BSP renumbers keys, `keys.h` is the one
   file that changes; a host test pins the route table so a rename cannot silently move a page.

Also worth logging in step 2 and not obvious from any doc: **key repeat** (do the arrows auto-repeat?),
**chords** (does the CH32 coprocessor already use Fn/symbol combos?), and whether `Esc`/`Backspace`
arrive as distinct codes.

## 6. Build and install (re-read from WadaMesh, EST-425)

**Source of every claim below:** the read-only reference
`/home/slofi/paperclip-pilot/sweep/tanmatsu-ref/wadamesh-53d17d0/` (WadaMesh commit `53d17d0`), which is now
on this machine. Citations are `file:line` **inside that tree**. Anything I still could not check from here
is marked `[unverified]`.

### 6.1 The model — an AppFS app, never a flash

- The Tanmatsu boots badge.team's launcher, and our program runs as an **AppFS app underneath it**, exactly
  like the launcher's own apps (`TANMATSU_PORT.md:17-23`, `TANMATSU_SIDELOAD.md:9-15`).
- An AppFS app is a **standard ESP-IDF app binary** installed into the AppFS partition (via
  `appfs_add_file.py` / BadgeLink / the badge.team app store) and started from the launcher; a running app
  owns the screen + input via `badge-bsp` and returns to the launcher on exit (`TANMATSU_PORT.md:20-23`).
- **Never `idf.py flash`:** it writes the bootloader, partition table and OTA data, i.e. the launcher OS
  itself (`TANMATSU_SIDELOAD.md:17-20`, `tools/tan_flash.sh:11-13`). The sideload writes **one app image +
  one metadata sector** and leaves bootloader/partition table/OTA slots alone (`TANMATSU_SIDELOAD.md:13-15`).
- ✓ This confirms §1/§3: we ship **one** app of ours with the inner Launcher inside it. The AppFS entry is
  the only thing that ever appears in the stock launcher's **Apps** list.

### 6.2 Toolchain

- **ESP-IDF 5.5.1**, project-local clone in `tanmatsu/esp-idf` (~3 GB, gitignored), tools in
  `tanmatsu/esp-idf-tools`: `Makefile:5` (`IDF_BRANCH ?= v5.5.1`), `Makefile:82-90` (clone + `install.sh
  all`), `env:1-6`, `build.sh:20`, `TANMATSU_SIDELOAD.md:27-30`.

  ⚠️ **CORRECTED by DeepSeek, S499, 2026-11-13 — 5.5.1 is WadaMesh's pin, not the launcher's, and it does
  not build this app.** Read from the launcher's own `main/idf_component.yml` at the tags:
  **v0.2.8** = `idf >=6.0.2` + `badge-bsp =1.6.3`; **v0.3.1** (on our unit) = `idf >=6.0.2` + `badge-bsp
  =2.0.0` + `pax-gfx =2.2.2`; the v0.3.1 image's own app descriptor reports `v6.0.2`. WadaMesh's
  `idf >=5.5.1` + `badge-bsp ^0.9.9` is the OLD combination — the first build against it failed in
  dependency solving ("no versions of idf match >=6.0.2"). **Our toolchain is ESP-IDF v6.0.2** and the BSP
  pin is **2.0.0**, because the BSP speaks to the CH32 coprocessor and must match the launcher underneath.
  It is installed at **`~/dev/esp-idf-v6.0.2`** (tools `~/.espressif`), *outside* the synced tree — a
  project-local 3 GB clone would have been replicated by Syncthing to every machine. `build.sh` in this
  folder reads `IDF_PATH`/`IDF_TOOLS_PATH` with those defaults.
- Target **`esp32p4`** (`build.sh:44`; `Makefile:24-26` maps `DEVICE=tanmatsu` → `esp32p4`).

- The **PlatformIO/pioarduino route is dead**: "the PlatformIO route in the old plan is dropped; the IDF
  project is now the primary path" (`TANMATSU_PORT.md:25-28`). `platformio.ini` is still in the reference
  but carries **no** `tanmatsu` env (grepped); it is the S3 boards' file.

Managed components — `main/idf_component.yml:1-12`:

| component | version | line |
|---|---|---|
| `idf` | `>=5.5.1` | 2 |
| `badgeteam/badge-bsp` (display MIPI, keyboard, power, LED) | `^0.9.9` | 3 |
| `nicolaielectronics/tanmatsu-wifi` (Wi-Fi/BLE on the C6) | `^1.1.2` | 4 |
| `nicolaielectronics/tanmatsu-lora` (SX1262/1268, remote) | `^0.3.0` | 5 |
| `espressif/arduino-esp32` | `3.3.10` | 8 |
| `h2zero/esp-nimble-cpp` | `^2.3.0` | 10 |

**How arduino-esp32 gets in:** as an **IDF component** (`main/idf_component.yml:6-8`), so the app code can
use the Arduino API while the project stays IDF-native; `esp32p4` has been first-class in arduino since
3.1.3 and 3.3.10 was proven to compile+link alongside `badge-bsp` (241 arduino objects) on 2026-06-20
(`TANMATSU_PORT.md:31-32`, `49-51`). Consequence for us: our code is an Arduino-API C++ **IDF component**,
not a `pio` firmware — `[unverified]` until we compile it ourselves.

**How MeshCore gets in:** **vendored, not managed.** `fetch-deps.sh:10-24` copies
`.pio/libdeps/LilyGo_TDeck_companion_radio_touch/MeshCore/{src,variants,lib/ed25519}` into
`components/meshcore/core/` (gitignored) under the committed wrapper `components/meshcore/CMakeLists.txt`.
So the MeshCore upgrade path is: run one S3 `pio run` (to populate libdeps) → `fetch-deps.sh` →
`build.sh`. `fetch-deps.sh` also applies two hard-failing source patches and a stale-core guard
(`fetch-deps.sh:74-98`, `104-175`, `174-190`). The wrapper mirrors `build_as_lib.py`'s ESP32 +
`MC_VENDORED_TOUCH_APP` source set and explicitly **excludes** `helpers/radiolib/*` (we use the LoRa
bridge), the S3 board classes and the S3 display/UI drivers (`components/meshcore/CMakeLists.txt:1-38`).
**This is the same shape our §2 core needs** — MeshCore as a passive component under a thin owner.

Other build facts worth carrying:
- Arduino libraries MeshCore needs compile as **one** `ardlibs` component so cross-includes resolve
  (`fetch-deps.sh:36-59`, `components/ardlibs/CMakeLists.txt:1-19`); LVGL is vendored the same way into its
  own component (`fetch-deps.sh:26-34`) — **we do not need LVGL**, we draw through LovyanGFX/badge-bsp like
  the L2, so this whole chunk (`fetch-deps.sh:26-59`, `build.sh:13-18, 92-101`) is *not* on our path.
- **esp-hosted override:** a local `components/esp_hosted/` empty forwarder (`REQUIRES
  esp-hosted-tanmatsu`) shadows the registry `espressif/esp_hosted`, because two whole-archive C6 SDIO
  drivers give "multiple definition"; the fork is the one wired to the real SDIO pins
  (`components/esp_hosted/CMakeLists.txt:1-12`, `TANMATSU_PORT.md:65-71`). Keep this trick: it is what makes
  arduino's `WiFi.h` and badge.team's stack coexist.
- `build.sh:67-101` patches **managed** dependencies (libsodium include paths, esp-hosted
  `NOT_ALLOWED`, arduino BLE IRK) at build time; `build.sh:84-85` is explicit that these patch the build
  dependency only and never the device firmware.
- `-Werror=all` is stripped globally for vendored code (`main/CMakeLists.txt:26-34`) — relevant to us only
  as a warning that IDF 5.5's default flags are stricter than PlatformIO's.

### 6.3 The build command and the output file

- **`cd tanmatsu && ./build.sh build`** (`TANMATSU_SIDELOAD.md:56-58`). Do **not** call `idf.py` directly:
  the wrapper configures and applies the patches first (`TANMATSU_SIDELOAD.md:60-61`).
- What that expands to: `idf.py -B build/tanmatsu -DDEVICE=tanmatsu -DSDKCONFIG_DEFAULTS="sdkconfigs/general;
  sdkconfigs/tanmatsu;sdkconfigs/wadamesh;sdkconfigs/wadamesh_tanmatsu" -DIDF_TARGET=esp32p4 …
  gen_project_binary` (`build.sh:40-44` + `106-108`).
- **Output: `tanmatsu/build/tanmatsu/application.bin`** (`TANMATSU_SIDELOAD.md:63-66`; `-B build/tanmatsu`
  at `build.sh:40`). The wrapper builds `gen_project_binary`, not the default target, because the app lives
  in the 8 MB AppFS partition and not in either 2 MB OTA slot, so IDF's OTA size check is irrelevant; a zero
  exit status means the bin is ready (`TANMATSU_SIDELOAD.md:63-66`, `build.sh:103-108`).
- **Hard size ceiling: the launcher maps at most 50 × 64 KB = 3200 KB of an AppFS app** ("appfsBlMmap"),
  and wadamesh was over it before `DISABLE_WIFI_OTA` + a deflated emoji blob
  (`main/CMakeLists.txt:42-45`, `components/wadamesh_app/CMakeLists.txt:12-15`). Our app is far smaller (no
  LVGL, no Lua, no web server, no emoji), but **step 2 must print the binary size against 3200 KB** —
  `[unverified]` our actual size.
- `sdkconfig` trap to keep: an existing `sdkconfig` keeps old values (`SDKCONFIG_DEFAULTS` only apply on
  generation), so `build.sh:46-59` regenerates it when an overlay line is missing. Our project needs the
  same guard or a stale local file silently ignores a radio/size default.

### 6.4 Which partition table

- **`partition_tables/16M.csv`**, selected by `sdkconfigs/tanmatsu:4-5`
  (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partition_tables/16M.csv"`)
  with `CONFIG_PARTITION_TABLE_CUSTOM=y` in `sdkconfigs/general:4`.
- Its layout (`partition_tables/16M.csv:3-9`):

  | name | type | offset | size |
  |---|---|---|---|
  | nvs | data/nvs | 0x19000 | 16K |
  | otadata | data/ota | 0x1D000 | 8K |
  | phy_init | data/phy | 0x1F000 | 4K |
  | ota_0 | app/ota_0 | 0x20000 | 2048K |
  | ota_1 | app/ota_1 | 0x220000 | 2048K |
  | **appfs** | **0x43 / 3** | **0x420000** | **8192K** |
  | locfd | data/fat | 0xC20000 | 3968K |

- So: **our binary goes into `appfs`** (8 MB, and the 50-page map limit sits on top of it), `locfd` is the
  FAT data partition the app mounts for storage (`TANMATSU_PORT.md:106` "storage→the FAT `locfd`
  partition"), and the two 2 MB OTA slots belong to the launcher — we never write them.
- The other tables in the folder (`esp32s3-8mb*`, `mch2022`, `hackerhotel2024`) are for badge.team's other
  boards; the S3 ones are what the L2's relatives would use, not us.
- `[unverified]` that Filip's unit (launcher v0.2.8) ships exactly this table. The reference's own tooling
  assumes it (`tools/dump-pristine.sh:15` reads `0x420000 0x800000`; `tools/tan_flash.sh:25` hard-codes
  `PART_OFF=0x420000`). Read-only first check on the glass: dump the table and compare.

### 6.5 How it is sideloaded into AppFS

Two documented routes. The reference's own (and the one this plan adopts) is the sector-diff one:

1. **Images.** `tools/tan_flash.sh` calls `tools/tan_deploy.py` first (`tan_flash.sh:22`). `tan_deploy.py`
   inserts name + version + `application.bin` into a **pristine dump of the device's own AppFS** using
   badge.team's `appfs.py`, then emits only the changed 64 KB sectors as two blobs:
   `appfs_app.bin` (app data, written FIRST) and `appfs_meta.bin` (sector 0 = metadata, written LAST)
   (`tan_deploy.py:1-15`, `69-93`). It refuses to run if the image size changed, if the metadata sector is
   unchanged, or if the changed data sectors are not contiguous (`tan_deploy.py:72-83`).
2. **Write + verify.** `esptool --chip esp32p4 … write_flash <APP_OFFSET> appfs_app.bin` with `--after
   no_reset`, then `write_flash 0x420000 appfs_meta.bin` with `--after hard_reset`, then `read_flash
   0x420000 0x10000` and `cmp` against the metadata blob (`tan_flash.sh:42-57`). Expected line:
   **`metadata verified — installed`** (`TANMATSU_SIDELOAD.md:91`).
- **Why that order:** an interrupted flash leaves the previous app intact instead of a half-written one that
  the launcher would show as complete (`tan_deploy.py:8-13`, `TANMATSU_SIDELOAD.md:79-89`).
- **Version must increase** or the launcher ignores the update (it keys on name + version, so republishing
  the same number leaves the old binary with no error anywhere) (`tan_deploy.py:51-52, 66`;
  `TANMATSU_SIDELOAD.md:93-95`).
- **One-time prerequisites:** `tools/fetch-appfs.sh` clones badge.team's `esp32-component-appfs` (not
  vendored — the circulating copy has no licence header: `fetch-appfs.sh:1-16`, `TANMATSU_SIDELOAD.md:34-40`);
  `tools/dump-pristine.sh <port>` = `read_flash 0x420000 0x800000` into `tools/dev_appfs.bin`
  (`dump-pristine.sh:15`), taken **before** our app is installed, because the deploy diffs against it
  (`TANMATSU_SIDELOAD.md:42-52`).
- **Port picking:** probe every `/dev/cu.usbmodem*` with `esptool chip_id` and take the one that answers
  `ESP32-P4`; the device exposes two ports (P4 + C6) and the names change between replugs
  (`tan_flash.sh:28-40`, `TANMATSU_SIDELOAD.md:73-77`). This is the same "opening a port can reset a chip"
  hazard as `BRIEF.md:31`.
- **Second route, the badge.team tool:** the project Makefile has `make badgelink` + `make install` →
  `badgelink.sh appfs upload application "template application" 0 ../../build/tanmatsu/application.bin`
  (`Makefile:49-58`). `[unverified]` whether current BadgeLink speaks AppFS v3 to launcher v0.2.8; the
  reference chose esptool, so we do too until a reason appears.
- Store distribution to other people = the badge.team app store (`TANMATSU_PORT.md:34-37`), separate from
  sideloading and not part of step 2.
- **Our step-2 gate (from BRIEF § 8) still applies on top of all this:** write `docs/RECOVERY.md` first, and
  install only with Filip present and with his OK.

### 6.6 The radio path — the C6 is in it, not SPI from the P4

- The LoRa modem (**SX1262 / SX1268**) lives on the **ESP32-C6** and is reached **remote** over the
  C6↔P4 link; MeshCore's RadioLib-over-SPI wrapper is *not* used on this board
  (`variants/tanmatsu/TanmatsuLoraRadio.h:3-8`). Bring-up is literally
  `lora_init_remote(&_h, 32)` (`variants/tanmatsu/TanmatsuLoraRadio.cpp:12`).
- The component is `nicolaielectronics/tanmatsu-lora ^0.3.0` (`main/idf_component.yml:5`;
  `variants/tanmatsu/idf_component.yml:14` "SX1262/1268 LoRa (remote, on the coprocessor)").
- The **same C6** also carries Wi-Fi/BLE via **esp-hosted over SDIO**: `tanmatsu-wifi ^1.1.2`
  (`main/idf_component.yml:4`), SDIO pins on slot 1 (`sdkconfigs/tanmatsu:14-20`),
  `CONFIG_SLAVE_IDF_TARGET_ESP32C6=y` (line 22), hosted NimBLE BT (`sdkconfigs/tanmatsu:9-13, 23`).
  So LoRa and Wi-Fi/BLE are **one coprocessor link, not two**: our core must assume the C6 is shared.
- Because the C6 owns SDIO **slot 1**, the microSD is deliberately on the P4's SDMMC **slot 0**
  (`main/CMakeLists.txt:51-53`: `BOARD_HAS_SDMMC=1 BOARD_SDMMC_SLOT=0`) — carry that into our `core/sdcard`.
- PHY values are confirmed and match the L2's SLO preset: `LORA_FREQ=869.618 LORA_BW=62.5 LORA_SF=8`
  (`main/CMakeLists.txt:47`), `bandwidth` in kHz with 62.5 → 62 (`TanmatsuLoraRadio.cpp:24`),
  `sync_word 0x12` = RadioLib's private sync word on air (`TanmatsuLoraRadio.cpp:28`), preamble 32 for SF≤8
  (line 30).
- Behaviour that must shape `core/radio` (all cited in the bridge, not guessed):
  `lora_send_packet()` is blocking (~2 s semaphore wait, `TANMATSU_PORT.md:146`) and
  `lora_set_config()` can block under continuous RX, so a runtime config change is **deferred to the
  RX-poll thread** (`TanmatsuLoraRadio.h:79-91`) and continuous RX is armed **once** on the first poll
  (`TanmatsuLoraRadio.cpp:50-60`). Translation for us: the radio owner is a dedicated task, the UI never
  calls `lora_*` directly — the same "one owner" rule as §2.
- `[unverified]` on our unit: which modem is fitted (only known at runtime via `lora_get_status`,
  `TanmatsuLoraRadio.cpp:18`), and whether the launcher must release/leave the C6 in a particular state
  before our app starts (nothing in the reference says; the launcher is not multitasking, `BRIEF.md:30`).

### 6.7 What is NOT installed on this machine (checked; nothing installed)

Commands run: `command -v idf.py pio platformio arduino-cli riscv32-esp-elf-gcc xtensa-esp32-elf-gcc ninja
cmake git`; `echo $IDF_PATH`; `ls ~/esp* ~/.espressif /opt/esp*`; `python3 -c "import esptool"`.

| needed | state here |
|---|---|
| ESP-IDF (`idf.py`, `IDF_PATH`) | **absent** — no `idf.py`, `IDF_PATH` unset, no `~/esp*`, no `~/.espressif` |
| ESP32-P4 toolchain (`riscv32-esp-elf-gcc`) | **absent** |
| PlatformIO (`pio`/`platformio`) | **absent** → even the `fetch-deps.sh` bootstrap (an S3 `pio run`) cannot run here |
| `arduino-cli`, `esptool` (python), `ninja` | **absent** |
| present | `cmake`, `git`, Python 3.14 + Pillow (that is why the previews run) |

And the reference tree itself cannot build either: `components/{meshcore/core,ardlibs/upstream,lvgl/upstream}`
**do not exist** in it — only the committed CMake wrappers ship (checked: `ls components/*/`). They are
repopulated by `fetch-deps.sh` from a PlatformIO S3 build.

**So: the build is documented and cited, not runnable on this machine, and nothing was installed.**

## 7. Recovery path (written before the first install, per BRIEF § 8)

Our app is an AppFS entry, and that is what makes this safe. In order of how bad it is:

1. **Our app is broken / crashes on start.** The stock launcher is a separate program in flash: it still
   boots, and the app is still just a row in **Apps**. Remove it with the launcher's own app-management
   screen (uninstall/delete) — `[unverified]` exact menu wording on v0.2.8. Nothing to re-flash.
2. **The launcher cannot start it and you are stuck on a black screen.** Power-cycle (hold the power
   button / the CH32's reset). The launcher comes up because it is the boot program, not an app.
3. **Our app corrupts a shared setting (e.g. the radio preset) and the launcher still works.** Delete the
   app, then set the radio preset back from the launcher's settings — `[unverified]` whether the launcher
   exposes these; if not, they are in our NVS namespace and ours to clear.
4. **Nothing boots / the launcher is gone.** This should be impossible by construction (we never write
   the launcher's region), so treat it as "unexpected, stop and report" (BRIEF § 8): **do not guess a fix.**
   The documented escape is flashing the stock launcher back from the `tanmatsu.zip` release over USB —
   `[unverified]`, and only with Filip present, reading Nicolai's official manual first.
5. **Serial-adjacent hazard:** two Espressif USB-JTAG ports are exposed (`30:ED…` and `98:A3…`, most
   likely P4 and C6, which is which is *not* established). **Opening one can reset that chip.** Never open
   them while Filip is using or updating the device, and never open them "to have a look" during an OTA.

Recovery writing goes into `docs/RECOVERY.md` once § 6 is re-read on a machine that has WadaMesh — this
section is its draft, and steps 1-3 are the ones a first install actually needs.

## 8. Open questions

**For Filip (decides):**
1. **The five-key map**: is triangle/square/circle/cloud/diamond → Launcher/Nodes/Chat/Map/RPTR right, and
   should Settings live behind a gear key in the hint bar (current plan) or on the diamond key with RPTR
   one step further in?
2. **"Later" tiles**: show Mesh-Torry and OPS-TOC Lite on the Launcher now as dim `LATER` rows (the
   preview does), or hide them until they exist? (BRIEF § 10.)
3. **Status bar without GPS**: the Tanmatsu has none. The preview shows radio + battery + gear and no
   satellite at all; when the Qwiic GPS arrives, the L2's GPS cell appears. Agree?
4. **Stop semantics on the OM Lite session**: stopping OM Lite ends the MeshCore session (the radio is
   shared, the *session* is OM Lite's). That means no messages arrive while it is stopped. The alternative
   is a core-level "mesh stays up" mode that keeps receiving with no module running — more RAM, and its
   own radio owner. Which do you want to be true?
5. **Launcher as the app's landing page** every time it starts, or straight back into the last module?

**For Claude / the next session:**
6. ~~**Get WadaMesh back on disk** and re-read § 6 against it.~~ **DONE in EST-425:** the reference is at
   `paperclip-pilot/sweep/tanmatsu-ref/wadamesh-53d17d0/` and § 6 is rewritten from it with `file:line`
   citations. What remains open from it is the on-device half: the partition table on Filip's v0.2.8 unit,
   the launcher's uninstall menu, and a build (no ESP-IDF here, § 6.7).
7. **Confirm the app package/AppFS facts and the launcher's uninstall menu** on v0.2.8, and whether the
   stock launcher can start a default app at boot (BRIEF § 10; also decides Q5).
8. **Decide the reuse mechanics**: symlink or include-path into `firmware/src/` (the brief prefers include
   if the L2 build stays untouched). My preference: a build-time include path plus a header guard list in
   `tanmatsu/` that the L2's own build never sees, so `firmware/` has **zero** edits for step 2.
9. **The preview's type sizes are design intent**, not panel metrics: the L2 generates its faces with
   `tools/mkvlw.py` (=`mkfont.py`). Step 2 should generate 800x480-sized faces through the same tool and
   the preview should then read the real VLW metrics.
10. `[unverified]` **Key codes and key repeat** — step 2's logger is the only source of truth.
11. **`keynav_logic.h`** looks like the L2's existing focus/keyboard logic; step 3 should read it before
    writing a second one (the "two implementations" smell).

## 9. What is NOT in this plan

- **Mesh-Torry and OPS-TOC Lite as code** (design only: they are rows in the same registry).
- **GPS** (external, later): `position_source.h` in the L2 already models an optional source; we keep it
  optional and default to "none".
- ~~**Map, RPTR, Settings previews**: not rendered~~ **Rendered in EST-425** (`shots/`): they are step-1 look
  fills only — no screen code behind them until Filip approves the six PNGs (BRIEF work order).
- **Anything on the device.** No install, no flash, no serial port opened in this run; no build either, since
  no ESP-IDF toolchain exists on this machine (§ 6.7).
