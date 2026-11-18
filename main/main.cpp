// SPDX-License-Identifier: MIT
// The Tanmatsu app: ONE AppFS app under the stock badge.team launcher, with our own Launcher inside.
//
// Step 2 (BRIEF § 9 / NEXT-FOR-DS.md): the shell only. It brings badge-bsp up, draws the approved status
// bar + Launcher home at 800x480, logs EVERY key it sees (including the five symbol keys — their codes are
// unknown by design, so this is the logger that fills core/keys.h), mounts the SD card, shows the battery,
// and wires the host-tested core (tanmatsu/core/) so Start/Stop is the real registry.
//
// It does NOT install anything and does NOT flash: the AppFS sideload (docs/RECOVERY.md) is a separate,
// Filip-present step.
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern "C" {
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "bsp/power.h"
#include "bsp/storage.h"
#include "bsp/tanmatsu.h"
#include "esp_hosted.h"   // esp_hosted_connect_to_slave() after the C6 cycle (S500)
#include "tanmatsu_coprocessor.h"
}

#include "bootloader_common.h"   // bootloader_common_get_rtc_retain_mem() (AppFS boot select lives there)
#include "core.h"
#include "menu_cards.h"          // S513/EST-432: the start menu's pure key decisions
#include "font.h"
#include "launcher_page.h"
#include "chat_page.h"           // step 4: the Chat page
#include "chrome.h"               // status bar, page head, hint bar: one home
#include "nodes_page.h"          // step 4: the Nodes page
#include "sd_history.h"          // step 5: chat history on the card
#include <unistd.h>
#include "map_link.h"            // step 6: the map pack on the card
#include "map_page.h"            // step 6: the Map page
#include "map_pack.h"            // step 6: tile coordinates for the default view
#include "map_view.h"            // step 6: view tiles, pan, zoom
#include "time_source.h"         // S502: the app's own clock (CH32 RTC -> NVS floor -> a peer's time)
#include "mc_wire.h"             // step 3b: the MeshCore packet + channel codec (portable, host-tested)
#include "mc_link.h"             // step 3b slice 2: channels out of NVS + the receive path
#include "radio_link.h"          // step 3a: the only path to the SX1262
#include "radio_plan.h"          // step 3a: the numbers, host-tested in core/
#include "screen.h"
#include "tm_theme.h"
#include "../../firmware/src/ui/theme.h"   // the L2 palette (kBg/kInk/kDim): one source of truth, never copied

using namespace om::ui;

namespace {

const char* TAG = "tm/app";

void batteryPoll();   // defined below the modules; the radio task needs it for the beacon's millivolts

Screen          g_screen;
QueueHandle_t   g_input_queue = nullptr;
bsp_power_battery_information_t g_batt = {};
bool            g_sd_mounted = false;

// Leaving the app (Filip, S499: "after the last ESC it reboots, it doesn't go to the launcher").
//
// WHY A REBOOT IS THE ONLY WAY BACK (sources read in S501, not assumed): the launcher and this app are two
// images. The launcher starts an app with `appfsBootSelect(fd, path)` + `esp_restart()` (launcher
// main/menu/apps.c:170-177), so going the other way is the same reset with nothing selected. There is no
// "return to the caller" path; a resume would need the launcher to keep running behind us.
//
// What decides the next boot, each line read where it lives:
//   1. the AppFS boot-select block in RTC-retained memory (`appfs_bootsel_t`: magic + fd + valid, at
//      rtc_retain_mem_t.custom). The BOOTLOADER tests `magic` ONLY (appfs bootloader_start.c:173-177) and
//      never reads `valid`, so clearing the magic is the whole mechanism; `valid=false` means "the app was
//      loaded but did not confirm it came up" and is written by the bootloader itself after the load.
//      The BSP's `bsp_device_restart_to_launcher()` already does the essential part (zeroes the first 8
//      bytes = the magic) and then restarts — S500 wrote that no Tanmatsu implementation exists; it does,
//      in badge-bsp/stub/badge_bsp_device.c:50. We zero the whole CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE
//      block anyway because the block is ours to hand back clean, but that is belt and braces.
//   2. the launcher's autostart entry: NVS `app_fav` / key `autostart`. It fires ONLY on a power-on or
//      JTAG reset (launcher main/main.c:587: `ESP_RST_POWERON || ESP_RST_JTAG`), so it cannot bring us back
//      after our own `esp_restart()` (SW_CPU_RESET). It matters for the OTHER case: the device is power
//      cycled while our app was the last thing running.
// Measured in S501, because S500 claimed the app comes back: it does not. 75 s of launcher console after a
// reset with zero app starts, and a second Esc inside the S500 capture behaved the same way (the one app
// boot in that capture lost its ROM header, i.e. the port was re-opened, so it was not the launcher).
static constexpr const char* kOurSlug = "om-lite";

static void clearLauncherAutostart() {
    static bool done = false;
    if (done) return;   // the launcher cannot change the value while we run: once per boot is enough
    nvs_handle_t h;
    if (nvs_open("app_fav", NVS_READWRITE, &h) != ESP_OK) {
        // NOT marked done: if this was the startup call, the exit path tries again rather than staying silent.
        ESP_LOGW(TAG, "launcher autostart: app_fav unreadable, left alone");
        return;
    }
    char      slug[32] = {0};
    size_t    len      = sizeof(slug);
    esp_err_t r        = nvs_get_str(h, "autostart", slug, &len);
    if (r == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "launcher autostart: not set");
    } else if (r != ESP_OK) {
        // INVALID_LENGTH (a slug longer than 31 chars) or something else: say so, touch nothing.
        ESP_LOGW(TAG, "launcher autostart: unreadable (%s), left alone", esp_err_to_name(r));
    } else if (strcmp(slug, kOurSlug) == 0) {
        // "Always boot to the menu" (Filip, S499) is about OUR app not autostarting itself.
        if (nvs_erase_key(h, "autostart") == ESP_OK && nvs_commit(h) == ESP_OK)
            ESP_LOGI(TAG, "launcher autostart pointed at us (%s): cleared", kOurSlug);
        else
            ESP_LOGW(TAG, "launcher autostart pointed at us but could not be cleared");
    } else {
        // It is the user's own choice, made in the stock launcher (F3 + FUNCTION toggles it, apps.c:573-577);
        // erasing it here would silently undo a setting that belongs to him.
        ESP_LOGI(TAG, "launcher autostart: \"%s\" is not us, left alone", slug);
    }
    nvs_close(h);
    done = true;
}

// A fresh C6 for whoever boots next (WadaMesh variants/tanmatsu main.cpp:326-338): without this,
// attaching to a C6 still in the previous owner's state wedges esp-hosted and ends in a crash or a P4
// reboot loop — which is exactly what the launcher did after our first exit (assert in xQueueGenericSend
// right after its own display init, black screen). Called at startup AND before leaving.
static void powerCycleC6() {
    tanmatsu_coprocessor_handle_t cph = nullptr;
    if (bsp_tanmatsu_coprocessor_get_handle(&cph) == ESP_OK && cph != nullptr) {
        tanmatsu_coprocessor_radio_disable(cph);
        vTaskDelay(pdMS_TO_TICKS(150));
        tanmatsu_coprocessor_radio_enable_application(cph);   // boot the C6 into its esp-hosted slave fw
        vTaskDelay(pdMS_TO_TICKS(600));                       // let it come up before anyone attaches
        ESP_LOGI(TAG, "C6 power-cycled through the CH32");
    } else {
        ESP_LOGW(TAG, "no coprocessor handle: C6 not power-cycled");
    }
}

// ⚠️ A power cycle alone is NOT enough — it was the S500 bug (found on glass, v10): the radio's custom
// RPCs timed out twice for 5 s each (`rpc_core: Timeout waiting for Resp for [0x184](Req_CustomRpc)`) and
// `set_config failed: ESP_FAIL`, right after this cycle, with `Tanmatsu coprocessor: Communication fault`
// logged in the same millisecond.
//
// Why: esp-hosted is brought up by a CONSTRUCTOR before app_main, so the host<->C6 link is already
// established when we power the C6 off. Cycling it invalidates that link; the transport has to be
// re-attached. The reference does exactly this and says so (WadaMesh tanmatsu/main/main.cpp:295-300 +
// 326-340: after the cycle, `hostedConnectC6()` — 4 tries, 250 ms apart — "must precede radio_init").
//
// Returns true only if the C6 answered, so the caller can say so instead of failing later in the radio.
static bool hostedConnectC6() {
    int rc = -1;
    for (int i = 0; i < 4 && rc != 0; i++) {
        rc = esp_hosted_connect_to_slave();
        if (rc != 0) {
            ESP_LOGW(TAG, "C6 hosted attach try %d -> %d", i + 1, rc);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
    ESP_LOGI(TAG, "C6 hosted link: %s", rc == 0 ? "UP" : "FAILED");
    return rc == 0;
}

static void leaveToLauncher() {
#if CONFIG_BOOTLOADER_RESERVE_RTC_MEM
    if (rtc_retain_mem_t* mem = bootloader_common_get_rtc_retain_mem()) {
        memset(mem->custom, 0, sizeof(mem->custom));
        ESP_LOGI(TAG, "cleared the AppFS boot-select (%u bytes of RTC-retained custom)",
                 (unsigned)sizeof(mem->custom));
    }
#endif
    clearLauncherAutostart();
    // NOTE (S503 step 6 slice 1): there is no `sync()` on this target, and a clean unmount needs the sdmmc card
    // handle that bsp_storage_mount_advanced() keeps to itself, so the FAT is still left with its dirty bit set.
    // No written DATA is at risk: every path on the card (history) opens, writes, fflushes and closes per record.
    // Closing this properly is the next slice: an unmount wrapper in the BSP, or mounting through the IDF ourselves.
    // Say it FIRST, then do the slow work underneath the words. Measured on glass (S501): with the C6
    // cycle in front, Esc left ~950 ms of frozen app page and the message flashed past unread, which is
    // exactly the "it just restarts / it died" reading this frame exists to kill.
    // The black frame at the end is NOT decoration: it is the settled state that cured the launcher's black
    // screen after our first exit (S499), so it stays the last thing on the glass.
    g_screen.clear(kBg);
    drawTextCentred(g_screen, Face::Row, kTmW / 2, 236, "Back to the menu", kInk);
    drawTextCentred(g_screen, Face::Body, kTmW / 2, 268, "The device restarts to switch apps.", kDim);
    const bool shown = g_screen.present();
    ESP_LOGI(TAG, "back-to-the-menu frame drawn (panel blit %s)", shown ? "ok" : "REFUSED");
    powerCycleC6();          // hand the launcher a C6 it can attach to; the message stays on screen
    vTaskDelay(pdMS_TO_TICKS(1500));   // S501: long enough to read (600 ms was missed on glass)
    g_screen.clear(0x0000);
    g_screen.present();
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}

// --------------------------------------------------------------------------------------------- modules
// OM Lite is a real module in the core, so Start/Stop is not a fake. Step 3a gives it the radio for real:
// start() takes the Radio lease and brings the SX1262 up through the C6, stop() puts it back to standby
// and frees what start() took.
//
// ⚠️ Honesty about "Stop frees everything": the core's allocator only sees what WE ask it for. The FreeRTOS
// task stack below is allocated by the kernel and is not in that count — so a flat allocator is not the
// same as a flat heap. Step 4 should put the stack under the allocator or measure the heap directly.
void*        g_om_block  = nullptr;   // what start() asked for; stop() must return exactly this
TaskHandle_t g_radio_task = nullptr;

// Listen for packets and put a beacon on the air every 10 s. Step 3a's whole purpose is to prove this path:
// RX is proven by real mesh traffic appearing with a plausible RSSI/SNR, TX by the C6 ACKing the send.
void radioTask(void*) {
    uint8_t buf[256];
    int64_t next_batt = esp_timer_get_time() + 10 * 1000 * 1000;
    for (;;) {
        int rssi = -127;
        int snr  = 0;
        // The radio is the mesh's now: nothing is transmitted on a timer any more. Step 3a's 11-byte beacon
        // was proof that the chip could send, but no peer can decode it (it is not a MeshCore packet), so a
        // periodic beacon would just be noise on the channel. Sending happens on request (sendText).
        om::mc::serviceRequests();   // the radio task is the only writer: build, sign, transmit here
        const int n = om::radio::poll(buf, sizeof(buf), &rssi, &snr, 250);
        if (n > 0) om::mc::onPacket(buf, (size_t)n, rssi, snr);
        if (esp_timer_get_time() >= next_batt) {
            next_batt = esp_timer_get_time() + 10 * 1000 * 1000;
            batteryPoll();   // the status bar's millivolts; no transmission involved
        }
    }
}

bool omStart(tm::Core& core) {
    g_om_block = core.alloc().alloc("om-lite", 4096);
    if (g_om_block == nullptr) return false;
    // A radio fault is reported, not fatal: the module's screens must still come up (step 4) and the
    // status bar shows "radio FAIL" instead of pretending.
    if (om::radio::begin()) {
        om::mc::begin();   // step 3b slice 2: load the channel table (NVS) before any packet arrives
        // 12 KiB, not 4: this task now verifies Ed25519 signatures (an incoming advert), and 4 KB panicked
        // with a stack protection fault the first time our own advert came back (S501). The task logs its
        // own low-water mark after the work, so the number is measured rather than assumed.
        xTaskCreate(radioTask, "om-radio", 12288, nullptr, 5, &g_radio_task);
    } else {
        ESP_LOGW(TAG, "OM Lite started without the radio: %s", om::radio::statusText());
    }
    return true;
}
void omStop(tm::Core& core) {
    if (g_radio_task != nullptr) {
        vTaskDelete(g_radio_task);
        g_radio_task = nullptr;
    }
    om::radio::end();
    if (g_om_block != nullptr) {
        core.alloc().release("om-lite", g_om_block);
        g_om_block = nullptr;
    }
}

// Field order is core/module.h: id, name, meta, pages, available, owns, start, stop, tick, draw, onKey.
tm::Module g_om_lite = {
    "om-lite",
    "OM Lite",
    "mesh companion · 4 pages",
    0x1F,                 // all five pages (Launcher, Nodes, Chat, Map, Rptr)
    true,                 // available
    tm::Resource::Radio | tm::Resource::SdCard,
    omStart,
    omStop,
    nullptr,              // tick
    nullptr,              // draw
    nullptr,              // onKey
};

// S513/EST-432: the two LATER modules are real registry rows with available = false, so the menu's LATER
// chip comes from the registry and not from a string in the shell. They own nothing, have no pages and no
// hooks: listed, never startable, and neither app is built.
tm::Module g_ops_toc = {"ops-toc", "OPS-TOC", "field ops map & checklists", 0, false, tm::Resource::None,
                        nullptr, nullptr, nullptr, nullptr, nullptr};
tm::Module g_mesh_torry = {"mesh-torry", "Mesh-Torry", "the territory game", 0, false, tm::Resource::None,
                           nullptr, nullptr, nullptr, nullptr, nullptr};

// --------------------------------------------------------------------------------------------- helpers
void batteryPoll() {
    g_batt = {};
    if (bsp_power_get_battery_information(&g_batt) != ESP_OK) g_batt.battery_available = false;
}

// The SD card goes through the BSP, NOT through a hand-rolled esp_vfs_fat_sdmmc_mount (S500).
//
// Why the hand-rolled version could never work: esp-hosted-tanmatsu installs a CONSTRUCTOR
// (host/port/esp/freertos/src/port_esp_hosted_host_init.c:17 -> esp_hosted_init() ->
// sdmmc_host_init()), so the ONE ESP32-P4 SDMMC host controller is claimed before app_main runs.
// Our own mount then failed in sd_host_claim_controller -> "no available sd host controller"
// (esp_driver_sdmmc/src/sd_host_sdmmc.c:84). The BSP's Tanmatsu storage driver already solves
// exactly that combination (targets/tanmatsu/badge_bsp_storage.c): slot 0 + the native IOMUX pins
// CLK 43 / CMD 44 / D0-D3 39-42, 4-bit, the on-chip LDO channel 4 as the card's power control, a
// power cycle before every mount, and host.init = noop so esp-hosted keeps the controller.
// bsp_device_initialize() has already called bsp_storage_initialize() (common/badge_bsp_device.c:98).
//
// It also gives us the card-detect answer for free: a missing card is NOT_PRESENT, not an error.
void mountSd() {
    bsp_storage_status_t st = bsp_storage_get_status(BSP_STORAGE_TYPE_SDCARD);
    if (st == BSP_STORAGE_STATUS_NOT_PRESENT) {
        g_sd_mounted = false;
        ESP_LOGW(TAG, "SD: no card inserted (status NOT_PRESENT)");
        return;
    }
    esp_err_t err = bsp_storage_mount_advanced(BSP_STORAGE_TYPE_SDCARD, "/sdcard", 10, false);
    g_sd_mounted  = (err == ESP_OK);
    if (g_sd_mounted) {
        ESP_LOGI(TAG, "SD mounted at /sdcard");
    } else {
        ESP_LOGW(TAG, "SD not mounted: %s (status was %d)", esp_err_to_name(err), (int)st);
    }
}

// The key logger: EVERY event, verbatim, once — that is the deliverable for core/keys.h.
void logInputEvent(const bsp_input_event_t& ev) {
    switch (ev.type) {
        case INPUT_EVENT_TYPE_NAVIGATION:
            printf("[key] nav key=%u state=%u\n", (unsigned)ev.args_navigation.key,
                   (unsigned)ev.args_navigation.state);
            break;
        case INPUT_EVENT_TYPE_KEYBOARD:
            printf("[key] kbd ascii=%u '%c'\n", (unsigned)(uint8_t)ev.args_keyboard.ascii,
                   (ev.args_keyboard.ascii >= 32 && ev.args_keyboard.ascii < 127) ? ev.args_keyboard.ascii : '.');
            break;
        default:
            printf("[key] type=%d\n", (int)ev.type);
            break;
    }
}

// Map the keys we KNOW (names taken from the launcher's own use of the BSP enum) into the core's Keys.
// The five symbol keys are deliberately not mapped yet: their codes are logged above and fill keys.h
// later — guessing them is exactly what BRIEF § 5 forbids.
tm::Key mapKey(const bsp_input_event_t& ev) {
    // S499 on glass: Esc never reached the app because it arrives as a KEYBOARD event (ASCII 0x1B), not as
    // a navigation key — the first mapping only looked at navigation and swallowed everything else.
    if (ev.type == INPUT_EVENT_TYPE_KEYBOARD) {
        switch ((uint8_t)ev.args_keyboard.ascii) {
            case 0x1B: return tm::Key::Esc;
            case 0x0D:   // CR
            case 0x0A:   // LF
                return tm::Key::Enter;
            case 0x08:
            case 0x7F: return tm::Key::Backspace;
            default: return (ev.args_keyboard.ascii >= 32) ? tm::Key::Char : tm::Key::None;
        }
    }
    if (ev.type != INPUT_EVENT_TYPE_NAVIGATION || !ev.args_navigation.state) return tm::Key::None;
    switch (ev.args_navigation.key) {
        case BSP_INPUT_NAVIGATION_KEY_UP: return tm::Key::Up;
        case BSP_INPUT_NAVIGATION_KEY_DOWN: return tm::Key::Down;
        case BSP_INPUT_NAVIGATION_KEY_LEFT: return tm::Key::Left;
        case BSP_INPUT_NAVIGATION_KEY_RIGHT: return tm::Key::Right;
        case BSP_INPUT_NAVIGATION_KEY_RETURN: return tm::Key::Enter;
        case BSP_INPUT_NAVIGATION_KEY_ESC:
        case BSP_INPUT_NAVIGATION_KEY_HOME: return tm::Key::Esc;
        case BSP_INPUT_NAVIGATION_KEY_BACKSPACE: return tm::Key::Backspace;
        default:
            // Everything else goes through the ONE key table, which now holds the codes read off this
            // board's glass (S499): the five symbol keys are nav keys 21..25. An unlogged code resolves
            // to Key::None, so this can never turn an unknown key into an action (core/keys.h).
            return tm::key_from_code((uint16_t)ev.args_navigation.key);
    }
}

void batteryStrings(char* out, size_t n, bool* charging) {
    *charging = g_batt.battery_available && g_batt.battery_charging;
    if (!g_batt.battery_available) {
        snprintf(out, n, "--");
        return;
    }
    snprintf(out, n, "%d%%", (int)(g_batt.remaining_percentage + 0.5f));
}

}  // namespace

// --- step 3b slice 1: prove the MeshCore codec ON THE CHIP ------------------------------------------
// The host test proves the codec against an independent Python implementation. This proves the other half:
// that the SAME file, built against the IDF's own mbedTLS and PSA backend, produces the SAME bytes on the
// ESP32-P4. It uses the host test's public vector key (00,01,..,0f) — never a real channel key, which has no
// business being in a firmware image.
//
// Expected in the log (the vector the Python oracle and the host test both agree on):
//   hash 0xBE, frame 53 B starting 1540be2b1d0b22ff38... and ending ...2f4cb6bacb8f52, and the decode back
//   says EDCT / "hello from the tanmatsu".
static void toHex(char* out, const uint8_t* in, size_t n) {
    static const char* d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 0x0F];
    }
    out[2 * n] = '\0';
}

static void meshcoreSelfTest() {
    uint8_t key[meshcore::kKeyBytes];
    for (size_t i = 0; i < sizeof(key); i++) key[i] = (uint8_t)i;

    meshcore::Channel ch;
    if (!meshcore::channelSetKey(ch, key, "selftest")) {
        ESP_LOGE(TAG, "mc codec: channelSetKey failed, no usable crypto backend");
        return;
    }

    uint8_t      frame[128];
    const size_t n = meshcore::buildGroupText(ch, 1750000000u, "EDCT", "hello from the tanmatsu", 2, frame,
                                              sizeof(frame));
    if (n == 0) {
        ESP_LOGE(TAG, "mc codec: buildGroupText failed (hash 0x%02X)", ch.hash);
        return;
    }
    char hex[2 * sizeof(frame) + 1];
    toHex(hex, frame, n);
    ESP_LOGI(TAG, "mc codec: hash 0x%02X frame %u B %s", ch.hash, (unsigned)n, hex);

    meshcore::View v;
    char           sender[32] = {0};
    char           text[64]   = {0};
    uint32_t       ts         = 0;
    if (!meshcore::decode(frame, n, &v) ||
        !meshcore::openGroupText(ch, v, sender, sizeof(sender), text, sizeof(text), &ts)) {
        ESP_LOGE(TAG, "mc codec: decode/open of our own frame FAILED");
        return;
    }
    ESP_LOGI(TAG, "mc codec: open ok, hash_size %u, from \"%s\", ts %u, text \"%s\"",
             (unsigned)v.hash_size, sender, (unsigned)ts, text);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "OM Lite (Tanmatsu) step-2 shell starting");
    // NVS is needed for the exit path (clearing the launcher's autostart) — init it ourselves: an AppFS
    // app boots fresh and does not inherit the launcher's initialisation.
    {
        esp_err_t r = nvs_flash_init();
        if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            r = nvs_flash_init();
        }
        if (r != ESP_OK) ESP_LOGW(TAG, "nvs_flash_init: %s", esp_err_to_name(r));
    }
    clearLauncherAutostart();   // Filip S499: no autostart, ever — every boot shows the menu
    meshcoreSelfTest();         // step 3b: the wire codec must produce the same bytes as the host/Python

    // ---- badge-bsp: display, input, power. This is what an AppFS app inherits; the launcher's own VFS
    // mounts are NOT inherited, so the SD is mounted here.
    // The proven shape on this board (WadaMesh variants/tanmatsu app main.cpp:315): badge-bsp brings up
    // the panel + input + power, and the ONE field we ask for is RGB565 (what screen.cpp writes).
    const bsp_configuration_t bspcfg = {
        .display = {.requested_color_format = BSP_DISPLAY_COLOR_FORMAT_16_565RGB, .num_fbs = 1},
    };
    esp_err_t init = bsp_device_initialize(&bspcfg);
    if (init != ESP_OK) ESP_LOGE(TAG, "bsp_device_initialize failed: %s", esp_err_to_name(init));
    if (bsp_input_get_queue(&g_input_queue) != ESP_OK) {
        ESP_LOGE(TAG, "no input queue");
        g_input_queue = nullptr;
    }
    bsp_display_set_backlight_brightness(100);

    // S502: the clock, before anything that stamps a time. It must come AFTER bsp_device_initialize (the RTC
    // lives in the CH32 coprocessor, which the BSP brings up) and after nvs_flash_init (the stored floor).
    // Nothing after this point may call time() and get 1970 without the log having said so first.
    om::time_source::begin();

    powerCycleC6();          // never inherit the launcher's C6 session
    // …and re-attach: the cycle breaks the link esp-hosted built in its constructor before app_main.
    // Without this every custom RPC (the radio's) times out — the S500 bug, v10.
    hostedConnectC6();

    loadFonts();
    if (!g_screen.begin()) {
        ESP_LOGE(TAG, "screen allocation failed — stopping here rather than drawing garbage");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();   // back to the stock launcher
    }

#ifdef TM_CALIBRATION
    // One-shot panel calibration (S499): four landscape quadrants in pure R/G/B/W, a black border, a
    // black square at the landscape TOP-LEFT and a "BR" label at the bottom-right. Filip's description of
    // where those land tells us the real rotation/colour mapping in one round instead of guessing.
    {
        const int W = kTmW, H = kTmH;
        g_screen.fillRect(0, 0, W / 2, H / 2, 0xF800);        // landscape TL = RED
        g_screen.fillRect(W / 2, 0, W / 2, H / 2, 0x07E0);    // TR = GREEN
        g_screen.fillRect(0, H / 2, W / 2, H / 2, 0x001F);    // BL = BLUE
        g_screen.fillRect(W / 2, H / 2, W / 2, H / 2, 0xFFFF);   // BR = WHITE
        g_screen.frame(0, 0, W, H, 4, 0x0000);                 // black border all round
        g_screen.fillRect(0, 0, 96, 96, 0x0000);               // black square, landscape top-left
        drawTextCentred(g_screen, Face::Title, 48, 60, "TL", 0xFFFF);
        drawTextCentred(g_screen, Face::Title, W - 60, H - 24, "BR", 0x0000);
        g_screen.present();
        ESP_LOGI(TAG, "calibration pattern drawn (TL red / TR green / BL blue / BR white, black corner TL)");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));   // stay up; keys are ignored in this build
    }
#endif

    batteryPoll();
    mountSd();
    // Step 5: the chat history lives on the card, so it starts only once the card is mounted. A missing card is
    // not fatal: the app runs with RAM-only history and the log says so.
    om::sdhistory::begin();
    // S513/EST-437: the contacts come back BEFORE the DM logs and before the radio task. That ordering matters:
    // the DM restore then finds its peer already in the table by its full key (instead of adding a prefix-only
    // row), and a DM from a node heard before the restart can be opened without waiting for its next advert.
    om::mc::restoreContacts();
    // S513/EST-434 item 4: restore the per-contact DM logs before the radio task exists, so nothing races it.
    om::mc::restoreDmHistory();
    // Step 6 slice 1: prove the pack path (index search, tile read, JPEG decode) with real tiles. central Slovenia,
    // because it is where this device actually is. No UI yet: the numbers in the log are the result.
    om::maplink::begin();
    if (g_sd_mounted) om::maplink::probe(46.15, 14.99);
    // S506: the map is only fast because the P4's hardware JPEG codec does the tiles. Prove at boot that it agrees
    // with the software decoder, which is the one whose pixels were checked on the glass in S503.
    if (g_sd_mounted) om::maplink::verifyDecoders(46.15, 14.99);

    // ---- the core: one owner, the registry, the router
    static tm::Core core;
    core.registry().add(g_om_lite);
    core.registry().add(g_ops_toc);       // LATER: listed, never startable
    core.registry().add(g_mesh_torry);    // LATER: listed, never startable
    core.start("om-lite");   // the menu shows it running, like the approved preview
    int64_t om_started_us = esp_timer_get_time();

    // ---- the start menu's three app cards (S513/EST-432), Filip's names and order. `running` is kept in
    // step with the registry by the key branch below; `memory`/`uptime` are the two numbers the old detail
    // pane carried.
    char om_mem[48]    = "";
    char om_uptime[32] = "";
    MenuCard cards[3] = {
        {"OM Lite", "mesh companion", true, true, om_mem, om_uptime, "radio 868 · MeshCore session · SD history"},
        {"OPS-TOC", "field ops map & checklists", false, false, nullptr, nullptr,
         "not built yet"},
        {"Mesh-Torry", "the territory game", false, false, nullptr, nullptr,
         "not built yet"},
    };
    const char* const kCardIds[3]  = {"om-lite", "ops-toc", "mesh-torry"};
    const tm::Page    kCardHome[3] = {tm::Page::Nodes, tm::Page::Launcher, tm::Page::Launcher};
    int               menu_focus   = 0;   // OM Lite, as Filip wants the focus to start

    auto refreshOmNumbers = [&]() {
        const size_t bytes = core.alloc().stats("om-lite").bytes;
        snprintf(om_mem, sizeof(om_mem), "%.1f KB requested", (double)bytes / 1024.0);
        if (!cards[0].running || om_started_us < 0) {
            snprintf(om_uptime, sizeof(om_uptime), "up < 1 m");
            return;
        }
        const int64_t secs = (esp_timer_get_time() - om_started_us) / 1000000;
        if (secs < 60) snprintf(om_uptime, sizeof(om_uptime), "up < 1 m");
        else if (secs < 3600) snprintf(om_uptime, sizeof(om_uptime), "up %d m", (int)(secs / 60));
        else snprintf(om_uptime, sizeof(om_uptime), "up %d h %d m", (int)(secs / 3600), (int)((secs / 60) % 60));
    };
    refreshOmNumbers();

    // S513/EST-432: the head's right slot can be taken by a short notice (the EST-428 pattern) — Enter on a
    // LATER card says why nothing happened, for three seconds, and then the core line returns.
    char    launcher_notice[64]      = {0};
    int64_t launcher_notice_until_us = 0;

    char battery[8];
    bool charging = false;
    batteryStrings(battery, sizeof(battery), &charging);

    LauncherView view;
    view.cards      = cards;
    view.card_count = 3;
    view.focused    = menu_focus;
    view.right_head = om::radio::statusText();   // step 3a: live, rebuilt every 2 s in the loop
    snprintf(view.clock, sizeof(view.clock), "%s", om::time_source::hhmm());   // S502: real time or "--:--"
    view.battery_pct = g_batt.battery_available ? (int)(g_batt.remaining_percentage + 0.5f) : -1;
    view.charging    = charging;
    snprintf(view.battery, sizeof(view.battery), "%s", battery);

    drawLauncher(g_screen, view);
    g_screen.present();
    ESP_LOGI(TAG, "first frame drawn (%dx%d)", kTmW, kTmH);

    // ---- the pages beyond the Launcher (S502, step 4) -----------------------------------------------
    // WHO is on screen is the core router's business (symbol keys, Esc and the history). What each page keeps
    // for itself lives here: its cursor, whether a channel is open, and the composer's text.
    constexpr size_t kChatPreview = 96;   // one "sender: text" line for the channel list
    int    nodes_focus   = 0;
    // S513/EST-430: the Chat list is two columns of cards, so the selection is a (column, row) pair like the model
    // in core/chat_cards.h. `chat_focus` is the row in the Channels column (and therefore the channel index);
    // `chat_dm_focus` is the row in the Direct messages column, which cannot be focused while it has no cards.
    int    chat_col        = om::mc::kColChannels;
    int    chat_focus      = 0;
    int    chat_dm_focus   = 0;
    // The Direct messages column's model. There is no TXT_MSG receive or send in this app yet, so the count is 0
    // and the page draws the L2's "No direct messages yet" line; a DM slice fills this and the view's dms[] and
    // nothing in the layout moves.
    int    chat_dm_count   = 0;
    // Each column scrolls by itself to keep its focused card visible (core/chat_cards::cardScroll).
    int    chat_card_scroll[om::mc::kCardCols] = {0, 0};
    bool   chat_open     = false;
    // S513/EST-434: when the open chat is a DM thread, who it is. `chat_dm_pub` is the thread key; a restored thread
    // may still be prefix-only until an advert fills the rest in (mc_link's rebind).
    bool   chat_dm_open     = false;
    uint8_t chat_dm_pub[32] = {0};
    char   chat_dm_name[32] = "";
    // S503: how many messages the open channel is scrolled BACK from the newest (0 = pinned to the bottom), how
    // far it can go (from the last draw — a channel that all fits has nowhere to scroll), and how many messages it
    // had at the last draw, so arriving messages do not drag the view out from under the reader.
    int    chat_scroll     = 0;
    int    chat_max_scroll = 0;
    int    chat_seen_count = -1;
    // Step 6: the Map page's view. wx/wy are the world pixel at the top-left of the map area; -1 means
    // "not placed yet", which the first draw turns into central Slovenia at z16.
    int64_t map_wx = -1, map_wy = -1;
    int     map_z  = 16;
    char   composer[120] = {0};
    size_t composer_len  = 0;
    // S513: a short-lived line for the one thing the composer cannot say by itself — that a send was REFUSED
    // because the radio's queue is full. The text stays in the composer; this says why nothing left.
    const char* chat_notice          = nullptr;
    int64_t     chat_notice_until_us = 0;

    auto clockText = []() { return om::time_source::hhmm(); };

    // The measure the wrap needs (S503): the real body font, over a byte range rather than a C string. A null
    // font measures 0, so every character would "fit" — the screen then clips the line, which is the old
    // behaviour and never worse than dropping the text.
    auto measureBody = [](void* ctx, const char* ptr, int len) -> int {
        const om::ui::Font* f = (const om::ui::Font*)ctx;
        return (f != nullptr && f->ready()) ? f->textWidthRange(ptr, len) : 0;
    };

    // Both page views are BUILT and DRAWN under the store lock; the lock is released before the panel blit, so
    // the radio task never waits for an 800x480 push, and a draw never sees a half-refreshed node.
    auto drawNodesPage = [&]() {
        om::ui::NodesView nv;
        nv.clock       = clockText();
        nv.battery_pct = view.battery_pct;
        nv.charging    = view.charging;
        int order[om::mc::kMaxContacts];
        {
            om::mc::StoreLock l;
            nv.store       = &om::mc::contacts();
            nv.order       = order;
            nv.order_count = (int)om::mc::contactsOrder(om::mc::contacts(), order, om::mc::kMaxContacts);
            if (nodes_focus >= nv.order_count) nodes_focus = nv.order_count > 0 ? nv.order_count - 1 : 0;
            if (nodes_focus < 0) nodes_focus = 0;
            const uint32_t now = om::time_source::epochNow();
            nv.focused         = nodes_focus;
            nv.now             = now;
            nv.in_last_hour    = (int)om::mc::contactsHeardWithin(om::mc::contacts(), now, 3600);
            om::ui::drawNodes(g_screen, nv);
        }
    };

    auto drawChatPage = [&]() {
        om::ui::ChatView cv;
        cv.open          = chat_open;
        cv.dm_open       = chat_dm_open;
        cv.clock         = clockText();
        cv.battery_pct   = view.battery_pct;
        cv.charging      = view.charging;
        cv.focus_col     = chat_col;
        cv.focus_row     = (chat_col == om::mc::kColChannels) ? chat_focus : chat_dm_focus;
        cv.scroll[0]     = chat_card_scroll[0];
        cv.scroll[1]     = chat_card_scroll[1];
        cv.composer      = composer;
        // S513: the notice is only in the view while it is live, so the page needs no clock of its own.
        cv.notice        = (chat_notice != nullptr && esp_timer_get_time() < chat_notice_until_us) ? chat_notice : nullptr;
        const int chans  = (int)om::mc::channelCount();
        cv.channel_count = chans;
        {
            om::mc::StoreLock l;
            om::mc::MessageRing& msgs = om::mc::messages();
            om::mc::ReadState&   rs   = om::mc::readState();
            if (chat_focus >= chans) chat_focus = chans > 0 ? chans - 1 : 0;
            if (chat_focus < 0) chat_focus = 0;

            if (!chat_open) {
                int total    = 0;
                cv.row_count = 0;
                for (int i = 0; i < chans && cv.row_count < om::ui::kChatRows; i++) {
                    const int            slot = cv.row_count++;
                    om::ui::ChatRowView& row  = cv.rows[slot];
                    const uint32_t       unread = om::mc::unreadForChannel(msgs, rs, (uint8_t)i);
                    total += (int)unread;
                    snprintf(row.left, sizeof(row.left), "%u", (unsigned)(unread > 9 ? 9 : unread));
                    row.title = om::mc::channelName((size_t)i);
                    const om::mc::ChannelSummary sum = om::mc::channelSummary(msgs, (uint8_t)i);
                    static char previews[om::ui::kChatRows][kChatPreview];
                    char*       preview = previews[slot];
                    if (sum.has) snprintf(preview, kChatPreview, "%s: %s", sum.sender, sum.text);
                    else snprintf(preview, kChatPreview, "no messages yet");
                    row.body   = preview;
                    row.unread = unread > 0;
                    if (sum.has) {
                        // S513/EST-430: a card's right-hand value is the newest message's LOCAL time (the ticket's
                        // HH:MM), not an age. The summary's ts is already our clock when it arrived (local_ts when
                        // set), so a peer with a wrong clock cannot put a nonsense hour on the card.
                        om::time_source::hhmmOf(sum.ts ? sum.ts : om::time_source::epochNow(), row.right,
                                                sizeof(row.right));
                    }
                }
                cv.unread_total = total;

                // S513/EST-434: the DM column. Threads come newest first from the store (item 3); the card is the
                // name (or the 8-hex prefix while a restored thread has no name), unread badge, the newest text
                // and its LOCAL HH:MM.
                om::mc::ContactStore& cs  = om::mc::contacts();
                om::mc::DmRing&       dmr = om::mc::dmThreads();
                static char dm_titles[om::ui::kChatRows][20];
                static char dm_previews[om::ui::kChatRows][kChatPreview];
                om::mc::DmSummary dsums[om::ui::kChatRows];
                const size_t      dn = om::mc::dmThreadSummaries(dmr, cs, dsums, om::ui::kChatRows);
                chat_dm_count        = (int)dn;
                cv.dm_count          = chat_dm_count;
                for (size_t i = 0; i < dn; i++) {
                    om::ui::ChatRowView& row = cv.dms[i];
                    if (dsums[i].name != nullptr && dsums[i].name[0] != '\0') {
                        snprintf(dm_titles[i], sizeof(dm_titles[i]), "%s", dsums[i].name);
                    } else {
                        snprintf(dm_titles[i], sizeof(dm_titles[i]), "%02x%02x%02x%02x", dsums[i].pub[0],
                                 dsums[i].pub[1], dsums[i].pub[2], dsums[i].pub[3]);
                    }
                    row.title = dm_titles[i];
                    snprintf(dm_previews[i], kChatPreview, "%s", dsums[i].text[0] ? dsums[i].text : "no messages yet");
                    row.body   = dm_previews[i];
                    row.unread = dsums[i].unread > 0;
                    snprintf(row.left, sizeof(row.left), "%u", (unsigned)(dsums[i].unread > 9 ? 9 : dsums[i].unread));
                    om::time_source::hhmmOf(dsums[i].ts ? dsums[i].ts : om::time_source::epochNow(), row.right,
                                            sizeof(row.right));
                }

                // The card columns: the channels the store gave us, and the DM threads above.
                // The scroll is decided HERE, with the page's own grid (chatCardGrid), so the drawer and this
                // decision cannot disagree about how many cards fit.
                const om::mc::CardGrid grid = om::ui::chatCardGrid();
                for (int col = 0; col < om::mc::kCardCols; col++) {
                    const int count = (col == om::mc::kColChannels) ? cv.row_count : cv.dm_count;
                    const int row   = (col == om::mc::kColChannels) ? chat_focus : chat_dm_focus;
                    chat_card_scroll[col] = om::mc::cardScroll(grid, count, row, chat_card_scroll[col]);
                    cv.scroll[col]        = chat_card_scroll[col];
                }
            } else {
                // The open view is one of two things: a channel's messages, or a DM thread's. Both go through the
                // SAME window/wrap code below, so a thread and a channel cannot drift in how they scroll.
                struct Src {
                    const char* text;
                    const char* sender;
                    uint32_t    local_ts;
                    uint32_t    ts;
                    bool        outgoing;
                    uint8_t     delivery;   // S513/EST-439: an outgoing DM's DmState (dm_delivery.h)
                };
                static Src srcs[om::mc::kMaxDms];
                int        idx[om::mc::kMaxDms];
                size_t     n = 0;
                if (chat_dm_open) {
                    snprintf(cv.channel_name, sizeof(cv.channel_name), "%s", chat_dm_name);
                    om::mc::DmRing& dmr = om::mc::dmThreads();
                    n = om::mc::dmForPeer(dmr, chat_dm_pub, idx, om::mc::kMaxDms);
                    for (size_t i = 0; i < n; i++) {
                        const om::mc::DmMessage* m = om::mc::dmAt(dmr, (size_t)idx[i]);
                        if (m == nullptr) continue;
                        srcs[i] = {m->text, m->peer_name, m->local_ts, m->ts, m->outgoing,
                                   (uint8_t)m->delivery};
                    }
                } else {
                    snprintf(cv.channel_name, sizeof(cv.channel_name), "%s",
                             om::mc::channelName((size_t)chat_focus));
                    n = om::mc::messagesForChannel(msgs, (uint8_t)chat_focus, idx, om::mc::kMaxMessages);
                    for (size_t i = 0; i < n; i++) {
                        const om::mc::Message* m = om::mc::messagesAt(msgs, (size_t)idx[i]);
                        if (m == nullptr) continue;
                        srcs[i] = {m->text, m->sender, m->local_ts, m->ts, m->outgoing, 0};
                    }
                }

                // A message arriving must not drag the view out from under someone who scrolled back (S503):
                // move the scroll by the same amount the chat grew, so the same messages stay on screen.
                if (chat_seen_count >= 0 && (int)n > chat_seen_count && chat_scroll > 0) {
                    chat_scroll += (int)n - chat_seen_count;
                }
                chat_seen_count = (int)n;
                if (chat_scroll < 0) chat_scroll = 0;

                // How many lines every message takes. Wrapping is core logic (chat_layout) measured with the real
                // body font; chatWindow then decides which messages fit in the space above the composer.
                static int             line_counts[om::mc::kMaxDms];
                static om::mc::Wrapped wraps[om::mc::kMaxDms];
                const om::ui::Font*    body = om::ui::fontFor(Face::Body);
                for (size_t i = 0; i < n; i++) {
                    wraps[i] = om::mc::wrapText(srcs[i].text != nullptr ? srcs[i].text : "", om::ui::kTmChatBodyW,
                                                measureBody, (void*)body);
                    line_counts[i] = 1 + (int)(wraps[i].count ? wraps[i].count : 1);
                }
                const om::mc::Window win = om::mc::chatWindow(line_counts, (int)n, om::ui::kTmChatLineH,
                                                             om::ui::kTmChatGapH, om::ui::kTmChatHeight,
                                                             chat_scroll);
                // The window clamps, so keep the counter in step: otherwise Up would spend presses on values the
                // screen never showed and Down would seem to do nothing at first.
                chat_max_scroll    = win.max_scroll;
                if (chat_scroll > chat_max_scroll) chat_scroll = chat_max_scroll;
                cv.hidden_above    = win.hidden_above;
                cv.hidden_below    = win.hidden_below;
                const uint32_t now = om::time_source::epochNow();
                cv.msg_count       = 0;
                for (int i = win.first; i <= win.last && cv.msg_count < om::ui::kChatRows; i++) {
                    const Src&             s  = srcs[i];
                    om::ui::ChatView::Msg& mv = cv.msgs[cv.msg_count++];
                    om::time_source::hhmmOf(s.local_ts ? s.local_ts : s.ts, mv.hhmm, sizeof(mv.hhmm));
                    snprintf(mv.sender, sizeof(mv.sender), "%s", s.sender != nullptr ? s.sender : "");
                    snprintf(mv.text, sizeof(mv.text), "%s", s.text != nullptr ? s.text : "");
                    om::mc::formatAge(now, s.local_ts ? s.local_ts : s.ts, mv.age, sizeof(mv.age));
                    mv.outgoing   = s.outgoing;
                    // S513/EST-439: the state after the time, in words (the font has no check glyph). "sending"
                    // until the ACK proves it, "delivered", or "no confirmation" after the last attempt. The L2's
                    // own tokens are dmStateToken()'s; the ticket's wording "no confirmation" wins for that one.
                    mv.state[0]   = '\0';
                    if (s.outgoing && chat_dm_open) {
                        switch ((om::ui::DmState)s.delivery) {
                            case om::ui::DmState::Sending: snprintf(mv.state, sizeof(mv.state), "sending"); break;
                            case om::ui::DmState::Delivered: snprintf(mv.state, sizeof(mv.state), "delivered"); break;
                            case om::ui::DmState::NotConfirmed:
                                snprintf(mv.state, sizeof(mv.state), "no confirmation");
                                break;
                            case om::ui::DmState::Failed: snprintf(mv.state, sizeof(mv.state), "not sent"); break;
                        }
                    }
                    mv.line_count = wraps[i].count ? wraps[i].count : 1;
                    mv.clipped    = wraps[i].clipped;
                    for (int l = 0; l < om::mc::kMaxLines; l++) mv.line[l] = wraps[i].line[l];
                }
            }
            om::ui::drawChat(g_screen, cv);
        }
    };

    // ---- the Map page (step 6): the offline tiles from the card, pan with the arrows, zoom with +/-.
    auto drawMapPage = [&]() {
        om::ui::MapView mv;
        mv.clock       = clockText();
        mv.battery_pct = view.battery_pct;
        mv.charging    = view.charging;
        mv.z           = map_z;
        if (!om::maplink::ready() || !g_sd_mounted) {
            mv.problem = "No card: the map packs live on the SD card.";
        } else {
            if (map_wx < 0 || map_wy < 0) {
                // First draw: centre ON Ljubljana (Presernov trg, 46.0514 N 14.5060 E), Filip's ask in S513. It used
                // to centre the TILE containing 46.15 N 14.99 E (near Litija), so the map opened somewhere else and
                // up to half a tile off. The bounds of the data are wider than the screen, so clamping here only
                // matters if the pack covers less than a screen at this zoom.
                map_wx = om::map::worldPxXForLon(14.5060, map_z) - om::ui::kMapAreaW / 2;
                map_wy = om::map::worldPxYForLat(46.0514, map_z) - om::ui::kMapAreaH / 2;
                om::map::clampWorldPixel(map_z, &map_wx, &map_wy, om::ui::kMapAreaW, om::ui::kMapAreaH);
            }
            mv.wx = map_wx;
            mv.wy = map_wy;
        }
        om::ui::drawMap(g_screen, mv);
    };

    auto drawPlaceholder = [&](tm::Page p) {
        static const struct {
            tm::Page    page;
            const char* title;
            const char* line;
        } kLater[] = {
            {tm::Page::Map, "Map", "Offline tiles from the SD pack: step 6."},
            {tm::Page::Rptr, "RPTR", "Repeater admin, the L2's pages and console: step 6."},
            {tm::Page::Settings, "Settings", "Radio, node name, time sources: step 6."},
        };
        const char* title = "Later";
        const char* line  = "This page is not built yet.";
        for (const auto& l : kLater) {
            if (l.page == p) {
                title = l.title;
                line  = l.line;
            }
        }
        g_screen.clear((uint16_t)kBg);
        om::ui::drawStatusBar(g_screen, clockText(), view.battery_pct, view.charging);
        om::ui::drawPageHead(g_screen, title, "not built yet");
        om::ui::drawText(g_screen, Face::Body, kTmPad, kTmStatusH + kTmHeadH + 40, line, om::ui::kDim);
        static const om::ui::HintItem hints[] = {{"esc", "back"}};
        om::ui::drawHintBar(g_screen, p, hints, 1);
    };

    // ---- the loop: input + a slow status refresh
    TickType_t lastPoll = xTaskGetTickCount();
    // S499's dev guard ("90 s with no input => leave, so a build that cannot see keys cannot own the
    // device") is GONE as of S500. It did its job: the key path is proven on glass (codes 21-25, Esc as a
    // keyboard event). It was actively harmful after that — it ended the app after 90 quiet seconds, which
    // is exactly what a radio test or an unattended listen looks like. Leave by Esc, which works.
    bool dirty = true;

    // S513/EST-435: an advert press either queues one or says why not, and the sentence comes from the rule itself
    // (core/advert_rule.cpp), so the wording the host test pins is the wording on the glass. A refusal is a 3 s
    // notice in the shared chrome; a fresh advert clears the last refusal, which would now be stale.
    auto pressAdvert = [&]() {
        const om::mc::AdvertDecision d = om::mc::queueAdvert();
        char                        text[48];
        if (om::mc::advertRefusalText(d, text, sizeof(text))) {
            om::ui::noticeSet(text, 3000);
        } else {
            om::ui::noticeClear();
        }
        dirty = true;
    };
    for (;;) {
        bsp_input_event_t ev = {};
        // While the map is filling in, wake up often so a decoded tile reaches the panel within a frame instead of
        // waiting for the next 250 ms poll (S506). With nothing being fetched this is the old 250 ms.
        const TickType_t wait = om::maplink::busy() ? pdMS_TO_TICKS(30) : pdMS_TO_TICKS(250);
        if (g_input_queue && xQueueReceive(g_input_queue, &ev, wait) == pdTRUE) {
            logInputEvent(ev);
            const tm::Page page_now = core.router().current();
            const tm::Key  k        = mapKey(ev);
            if (k != tm::Key::None) {
                bool handled = false;

                // ---- the Chat page owns its keys while a channel is open: typing goes to the composer.
                if (page_now == tm::Page::Chat && chat_open) {
                    if (k == tm::Key::Esc) {
                        // A DM thread and a channel leave the same way; each marks itself read on the way up.
                        if (chat_dm_open) {
                            om::mc::noteDmRead(chat_dm_pub);
                            chat_dm_open = false;
                        } else {
                            om::mc::noteChannelRead((uint8_t)chat_focus);
                        }
                        chat_open       = false;
                        chat_scroll     = 0;
                        chat_seen_count = -1;
                        // What you were looking at is READ — in the store AND on the card (the seen point), so it
                        // is still read after a restart. Without this a message that arrived while the channel
                        // was open came back as a badge the moment you went up a level (Filip, S502).
                        handled = true;
                    } else if (k == tm::Key::Enter) {
                        if (composer_len > 0) {
                            // S513/EST-439: a DM goes through queueDm (compose + pending + the radio queue), a
                            // channel through queueText. Both refuse rather than replace when the queue is full,
                            // so a refusal leaves the text in the composer and says why for three seconds.
                            const bool queued = chat_dm_open ? om::mc::queueDm(chat_dm_pub, composer)
                                                             : om::mc::queueText((size_t)chat_focus, composer);
                            if (queued) {
                                ESP_LOGI(TAG, "chat: queued %s (%u chars)", chat_dm_open ? "a DM" : "a channel text",
                                         (unsigned)composer_len);
                                composer[0]           = '\0';
                                composer_len          = 0;
                                chat_notice           = nullptr;   // a send that worked clears the last refusal
                                chat_notice_until_us  = 0;
                            } else {
                                // S513: the queue is FULL (four requests waiting). Nothing is replaced, so the
                                // text stays in the composer and the page says why for three seconds.
                                chat_notice          = "radio busy, not sent";
                                chat_notice_until_us = esp_timer_get_time() + 3 * 1000 * 1000;
                                ESP_LOGW(TAG, "chat: the send queue is full; the text is kept in the composer");
                            }
                        }
                        handled = true;
                    } else if (k == tm::Key::Backspace) {
                        // delete one UTF-8 sequence, not one byte: a Slovenian letter must not become garbage
                        while (composer_len > 0) {
                            const unsigned char c = (unsigned char)composer[--composer_len];
                            if ((c & 0xC0) != 0x80) break;
                        }
                        composer[composer_len] = '\0';
                        handled                = true;
                    } else if (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD) {
                        if (composer_len + 1 < sizeof(composer)) {
                            composer[composer_len++] = (char)ev.args_keyboard.ascii;
                            composer[composer_len]   = '\0';
                        }
                        handled = true;
                    } else if (k == tm::Key::Up || k == tm::Key::Down) {
                        // Scrolling back through the channel (S503). Up goes older, Down newer. The limit comes
                        // from the last draw: a channel whose messages all fit has nowhere to scroll, so Up does
                        // nothing instead of hiding the newest message (found on the glass at three messages).
                        if (k == tm::Key::Up) {
                            if (chat_scroll < chat_max_scroll) ++chat_scroll;
                        } else if (chat_scroll > 0) {
                            --chat_scroll;
                        }
                        handled = true;
                    }
                    dirty = true;
                } else if (page_now == tm::Page::Chat) {
                    // S513/EST-430: Up/Down move inside a column, Left/Right switch column (landing on the nearest
                    // row, clamped to that column's last card), and an empty column cannot take focus. All of that
                    // is core/chat_cards::cardMoveFocus — this only hands it the counts.
                    const bool is_move = k == tm::Key::Up || k == tm::Key::Down || k == tm::Key::Left ||
                                         k == tm::Key::Right;
                    if (is_move) {
                        const om::mc::CardMove move = (k == tm::Key::Up)     ? om::mc::CardMove::Up
                                                      : (k == tm::Key::Down) ? om::mc::CardMove::Down
                                                      : (k == tm::Key::Left) ? om::mc::CardMove::Left
                                                                             : om::mc::CardMove::Right;
                        const int counts[om::mc::kCardCols] = {(int)om::mc::channelCount(), chat_dm_count};
                        const om::mc::CardFocus from{chat_col, chat_col == om::mc::kColChannels ? chat_focus
                                                                                              : chat_dm_focus};
                        const om::mc::CardFocus to = om::mc::cardMoveFocus(from, move, counts, om::mc::kCardCols);
                        chat_col = to.col;
                        if (to.col == om::mc::kColChannels)
                            chat_focus = to.row;
                        else
                            chat_dm_focus = to.row;
                        handled = true;
                        dirty   = true;
                    } else if (k == tm::Key::Enter) {
                        // Enter opens the focused card: a channel, or a DM thread. The DM thread's key and name
                        // come from the same summaries the column drew, so the card and the open view agree.
                        if (chat_col == om::mc::kColChannels && om::mc::channelCount() > 0) {
                            chat_dm_open    = false;
                            chat_open       = true;
                            chat_scroll     = 0;    // always opens at the newest message
                            chat_seen_count = -1;
                            om::mc::noteChannelRead((uint8_t)chat_focus);   // opening a channel marks it read
                        } else if (chat_col == om::mc::kColDms && chat_dm_count > 0) {
                            om::mc::DmSummary sums[om::ui::kChatRows];
                            size_t            dn = 0;
                            {
                                om::mc::StoreLock l;
                                dn = om::mc::dmThreadSummaries(om::mc::dmThreads(), om::mc::contacts(), sums,
                                                               om::ui::kChatRows);
                            }
                            if (chat_dm_focus >= 0 && (size_t)chat_dm_focus < dn) {
                                const om::mc::DmSummary& sum = sums[chat_dm_focus];
                                memcpy(chat_dm_pub, sum.pub, sizeof(chat_dm_pub));
                                snprintf(chat_dm_name, sizeof(chat_dm_name), "%s",
                                         sum.name != nullptr && sum.name[0] != '\0' ? sum.name : "");
                                if (chat_dm_name[0] == '\0') {
                                    snprintf(chat_dm_name, sizeof(chat_dm_name), "%02x%02x%02x%02x", sum.pub[0],
                                             sum.pub[1], sum.pub[2], sum.pub[3]);
                                }
                                chat_dm_open    = true;
                                chat_open       = true;
                                chat_scroll     = 0;
                                chat_seen_count = -1;
                                om::mc::noteDmRead(chat_dm_pub);   // opening a thread marks it read
                            }
                        }
                        handled = true;
                        dirty   = true;
                    }
                } else if (page_now == tm::Page::Map) {
                    // Pan by a quarter of a tile per press: fine enough to place a town, coarse enough to cross the
                    // country without holding the key. Zoom keeps the centre (core/map_view::zoomTo).
                    const int step = om::map::kTilePx / 4;
                    if (k == tm::Key::Up || k == tm::Key::Down || k == tm::Key::Left || k == tm::Key::Right) {
                        const int dx = (k == tm::Key::Left) ? -step : (k == tm::Key::Right) ? step : 0;
                        const int dy = (k == tm::Key::Up) ? -step : (k == tm::Key::Down) ? step : 0;
                        om::map::panBy(map_z, &map_wx, &map_wy, dx, dy, om::ui::kMapAreaW, om::ui::kMapAreaH);
                        handled = true;
                        dirty   = true;
                    } else if (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD &&
                               (ev.args_keyboard.ascii == '+' || ev.args_keyboard.ascii == '=' ||
                                ev.args_keyboard.ascii == '-')) {
                        const int want = (ev.args_keyboard.ascii == '-') ? map_z - 1 : map_z + 1;
                        map_z = om::map::zoomTo(map_z, want, &map_wx, &map_wy, om::ui::kMapAreaW,
                                                om::ui::kMapAreaH);
                        handled = true;
                        dirty   = true;
                    }
                } else if (page_now == tm::Page::Nodes) {
                    if (k == tm::Key::Up || k == tm::Key::Down) {
                        nodes_focus += (k == tm::Key::Down) ? 1 : -1;
                        if (nodes_focus < 0) nodes_focus = 0;
                        handled = true;
                        dirty   = true;
                    }
                }

                if (!handled) {
                    if (tm::is_symbol(k)) {
                        core.handleKey(k);   // the core's route: a module never sees a symbol key
                        dirty = true;
                    } else if (page_now == tm::Page::Launcher) {
                        // S513/EST-432: the menu is three app cards. The S499 two-area rule (Enter -> the
                        // actions, Esc back to the list) is deliberately RETIRED here: there is no second area
                        // any more, so Enter opens the focused app and Esc leaves the app. The exit path below
                        // is S501's, untouched.
                        if (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD &&
                            ev.args_keyboard.ascii == 'a') {
                            pressAdvert();   // the advert key still works on the menu
                        } else if (k == tm::Key::Esc) {
                            ESP_LOGI(TAG, "leaving to the stock launcher");
                            leaveToLauncher();
                        } else if (k == tm::Key::Left || k == tm::Key::Right || k == tm::Key::Up ||
                                   k == tm::Key::Down) {
                            // Left/Right (Up/Down aliases) move between the cards; core/menu_cards clamps at
                            // both ends, so there is no wrap.
                            const tm::MenuMove move = (k == tm::Key::Left)     ? tm::MenuMove::Left
                                                      : (k == tm::Key::Right) ? tm::MenuMove::Right
                                                      : (k == tm::Key::Up)    ? tm::MenuMove::Up
                                                                              : tm::MenuMove::Down;
                            menu_focus = tm::menuFocusMove(menu_focus, move, view.card_count);
                            dirty      = true;
                        } else if (k == tm::Key::Enter) {
                            const tm::MenuAction act = tm::menuEnterAction(cards[menu_focus].available);
                            if (act == tm::MenuAction::Open) {
                                if (!cards[menu_focus].running) {
                                    // Enter OPENS: a stopped app is started first, then the router goes to its
                                    // home page (OM Lite: Nodes). Only OM Lite is available today.
                                    const tm::StartResult r = core.start(kCardIds[menu_focus]);
                                    ESP_LOGI(TAG, "open -> start %s: code=%d busy=%s", kCardIds[menu_focus],
                                             (int)r.code, tm::resource_name(r.busy));
                                    if ((bool)r) {
                                        cards[menu_focus].running = true;
                                        om_started_us             = esp_timer_get_time();
                                        refreshOmNumbers();
                                    }
                                }
                                if (cards[menu_focus].running) {
                                    core.goPage(kCardHome[menu_focus]);
                                    dirty = true;
                                }
                            } else if (act == tm::MenuAction::ComingLater) {
                                // A LATER card changes nothing visible except a three-second notice in the
                                // head's right slot (the EST-428 pattern: main.cpp only passes it while live).
                                snprintf(launcher_notice, sizeof(launcher_notice), "%s: coming later",
                                         cards[menu_focus].name);
                                launcher_notice_until_us = esp_timer_get_time() + 3 * 1000 * 1000;
                                ESP_LOGI(TAG, "open: %s is not available yet", cards[menu_focus].name);
                                dirty = true;
                            }
                        } else if (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD &&
                                   ev.args_keyboard.ascii == 's') {
                            // s STOPS the focused running app: no confirm, it is reversible. The leak line
                            // is logged exactly as before.
                            const tm::MenuAction act =
                                tm::menuStopAction(cards[menu_focus].available, cards[menu_focus].running);
                            if (act == tm::MenuAction::Stop) {
                                const tm::StopResult r = core.stop(kCardIds[menu_focus]);
                                char                 leak[96];
                                core.leakLine(leak, sizeof(leak));
                                ESP_LOGI(TAG, "stop %s: code=%d leaked=%s %s", kCardIds[menu_focus], (int)r.code,
                                         r.leaked ? r.leaked : "none", leak);
                                cards[menu_focus].running = false;
                                refreshOmNumbers();
                                dirty = true;
                            }
                        } else if (core.handleKey(k)) {
                            dirty = true;   // a non-symbol key a module claimed (none on the menu today)
                        }
                    } else if (page_now == tm::Page::Nodes && k == tm::Key::Char &&
                               ev.type == INPUT_EVENT_TYPE_KEYBOARD && ev.args_keyboard.ascii == 'a') {
                        // S513/EST-432: the advert key is on the Nodes page too (its empty-list text points at
                        // it), so the page that shows who was heard can ask for a fresh advert without leaving.
                        pressAdvert();
                    } else if (k == tm::Key::Esc) {
                        core.handleKey(k);   // back one page through the router's history
                        dirty = true;
                    }
                }
            }
        }

        // A tile the worker finished: repaint, so the map fills in from the centre out while the keys stay live.
        if (om::maplink::takeLanded() && core.router().current() == tm::Page::Map) dirty = true;

        if (xTaskGetTickCount() - lastPoll > pdMS_TO_TICKS(2000)) {
            lastPoll = xTaskGetTickCount();
            batteryPoll();
            batteryStrings(battery, sizeof(battery), &charging);
            view.battery_pct = g_batt.battery_available ? (int)(g_batt.remaining_percentage + 0.5f) : -1;
            view.charging    = charging;
            // Step 3a: the status bar carries the radio's live state and packet counters.
            view.right_head = om::radio::statusText();
            // A message that arrives while its channel is open is being READ, not waiting: the badge means
            // "you have not seen this", and you are looking at it (Filip, S502). The seen point on the card moves
            // with it, so after a restart it is still read (S503) — and it only costs a write when it moved.
            if (chat_open && core.router().current() == tm::Page::Chat) {
                if (chat_dm_open) om::mc::noteDmRead(chat_dm_pub);
                else om::mc::noteChannelRead((uint8_t)chat_focus);
            }
            // S502: apply a time the radio task heard (and correct the CH32 RTC / store the floor), then show it.
            om::time_source::service();
            snprintf(view.clock, sizeof(view.clock), "%s", om::time_source::hhmm());
            dirty = true;
            core.tick();
            refreshOmNumbers();   // the running card's memory + uptime, refreshed with the rest of the chrome
        }

        // S513/EST-437: the contact table's one NVS write, on the MAIN task. The radio task only raised a flag;
        // a structural change goes on this pass, last-heard/SNR waits for the blob's 10-minute floor. Cheap when
        // nothing is dirty, which is almost always.
        om::mc::contactsPersistTick();

        // S513/EST-439: the DM retry rule, on the MAIN task — it composes the next attempt and QUEUES it, and
        // marks "no confirmation" when the attempts are spent. True = a message's state moved, so repaint.
        if (om::mc::dmTick()) dirty = true;

        if (dirty) {
            switch (core.router().current()) {
                case tm::Page::Launcher:
                    view.focused = menu_focus;
                    // The notice is only in the view while it is live (EST-428), so the drawer needs no clock:
                    // when it lapses, the head's right slot is the core line again.
                    view.notice = (launcher_notice[0] != '\0' && esp_timer_get_time() < launcher_notice_until_us)
                                      ? launcher_notice
                                      : nullptr;
                    drawLauncher(g_screen, view);
                    break;
                case tm::Page::Nodes:
                    drawNodesPage();
                    break;
                case tm::Page::Chat:
                    drawChatPage();
                    break;
                case tm::Page::Map:
                    drawMapPage();
                    break;
                default:
                    drawPlaceholder(core.router().current());
                    break;
            }
            const int64_t present_t0 = esp_timer_get_time();
            g_screen.present();
            const long present_us = (long)(esp_timer_get_time() - present_t0);
            // On the Map page the panel blit is a big share of a frame (it transposes 384k pixels on the CPU), so
            // it is measured, not assumed: a fill-in only looks smooth if compose + present stays short.
            if (core.router().current() == tm::Page::Map) {
                ESP_LOGI(TAG, "map frame: present %ld ms", present_us / 1000);
            }
            dirty = false;
        }
    }
}
