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
#include "esp_heap_caps.h"   // EST-445: the DEVICE rows read free internal heap and PSRAM
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
#include "own_top.h"             // S514/EST-454: the own key at the top -> first row (pure, host-tested)
#include "menu_cards.h"          // S513/EST-432: the start menu's pure key decisions
#include "font.h"
#include "emoji_blob.h"
#include "launcher_page.h"
#include "chat_page.h"           // step 4: the Chat page
#include "chrome.h"               // status bar, page head, hint bar: one home
#include "nodes_page.h"          // step 4: the Nodes page
#include "nodes_search.h"        // S514/EST-449: the Nodes search (matcher, count, focus rules)
#include "node_menu.h"           // S514/EST-455: the per-node actions + the favourite notice/focus rule
#include "nodes_fresh.h"         // S515/EST-461: the NEW contact's frozen order + ack (pure, host-tested)
#include "screen_power.h"        // S515/EST-461: the power button's screen-off state machine (pure, host-tested)
#include "rptr_page.h"           // S514/EST-453: RPTR part 1 (list, login, status)
#include "rptr_link.h"           // S514/EST-453: the RPTR radio requests + remembered passwords
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
#include "settings.h"        // EST-445: the Settings frame + General rules (host-tested)
#include "settings_page.h"   // EST-445: the Settings drawing
#include "channel_admin.h"   // S514/EST-446: hashtag/base64 rules + the safe NVS rewrite order
#include "channel_scope.h"   // S513/EST-440: the scope modes and the region-name rule the CHAT rows use
#include "contact_admin.h"   // S514/EST-447: remove/clear a contact, type words, the notice default
#include "map_settings.h"    // S514/EST-447: the MAP rows' choices and the last-position write rule
#include "sound_events.h"    // S514/EST-450: the events, their L2 defaults and every sound rule
#include "sound_store.h"     // S514/EST-450: the sound settings' NVS blob (mcsnd)
#include "audio_out.h"       // S514/EST-450: the one owner of I2S (queue-only, never blocks the UI)
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

// ---- S515/EST-461: the power button = screen off (the DECISION is core/screen_power.h) ---------------------
// This trio is the only BSP work: remember both brightnesses, set both to 0, restore them. The loop decides
// WHEN (press edge, key wake) and skips the panel blit while the state says OFF.
om::ui::ScreenPower g_screen_power;
uint8_t             g_disp_bright = 100;   // what the display was before OFF
uint8_t             g_kbd_bright  = 100;   // …and the keyboard backlight

void screenLightsOff() {
    uint8_t d = 100, k = 100;
    if (bsp_display_get_backlight_brightness(&d) != ESP_OK) d = 100;
    if (bsp_input_get_backlight_brightness(&k) != ESP_OK) k = 100;
    // A second press before the first restore must not remember 0 as "the" brightness.
    if (d > 0) g_disp_bright = d;
    if (k > 0) g_kbd_bright = k;
    bsp_display_set_backlight_brightness(0);
    bsp_input_set_backlight_brightness(0);
}

void screenLightsOn() {
    bsp_display_set_backlight_brightness(g_disp_bright);
    bsp_input_set_backlight_brightness(g_kbd_bright);
}

static void leaveToLauncher() {
    // S515/EST-461: the launcher must never start DARK. Esc with the screen off is a wake as well as an exit.
    if (g_screen_power.off) {
        g_screen_power.off = false;
        screenLightsOn();
        ESP_LOGI(TAG, "[screen] on (launcher)");
    }
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
        om::rptrlink::begin();   // S514/EST-453: remembered repeater passwords (NVS "mcrp")
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

// ---- Settings as data (S514/EST-448) -----------------------------------------------------------
// General is the CORE's tab: these rows are the shell's own, and every value getter reads its real source
// on each draw (NVS node name, mcid/advint, core/radio_plan, time_source, sdhistory, mc_store, the heap).
// Nothing here is a stored-for-display copy.
enum : uint16_t {
    kSetRowName = 1,
    kSetRowAdvertNow,
    kSetRowAdvertInterval,
    // S514/EST-450: the SOUND section. Event rows carry kSetRowSoundEventBase + the Event value, so
    // the row id can never be mistaken for one of the three rows above.
    kSetRowSoundMaster = 10,
    kSetRowSoundVolume,
    kSetRowSoundEventBase = 20,
};

static char gset_name[32], gset_pub[32], gset_adv[8], gset_freq[24], gset_bw[16];
static char gset_sf[12], gset_cr[12], gset_tx[12], gset_hash[8];
static char gset_time[48], gset_sd[48], gset_contacts[24], gset_ver[12], gset_heap[24], gset_psram[24];

const char* gsetName(tm::Core&) {
    snprintf(gset_name, sizeof(gset_name), "%s", om::mc::nodeName());
    return gset_name;
}
const char* gsetPublicKey(tm::Core&) {
    snprintf(gset_pub, sizeof(gset_pub), "%s", om::mc::publicKeyHex());
    return gset_pub;
}
const char* gsetAdvert(tm::Core&) {
    snprintf(gset_adv, sizeof(gset_adv), "%s",
             tm::advertIntervalLabel(tm::advertIntervalFromSeconds(om::mc::advertIntervalSeconds())));
    return gset_adv;
}
const char* gsetFrequency(tm::Core&) {
    snprintf(gset_freq, sizeof(gset_freq), "%.3f MHz", (double)tm::kSloPlan.freq_hz / 1000000.0);
    return gset_freq;
}
const char* gsetBandwidth(tm::Core&) {
    // bw_khz is the chip's integer step: 62 means 62.5 kHz (see core/radio_plan.h).
    snprintf(gset_bw, sizeof(gset_bw), "%s kHz", tm::kSloPlan.bw_khz == 62 ? "62.5" : "?");
    return gset_bw;
}
const char* gsetSpreading(tm::Core&) {
    snprintf(gset_sf, sizeof(gset_sf), "SF %u", (unsigned)tm::kSloPlan.sf);
    return gset_sf;
}
const char* gsetCoding(tm::Core&) {
    snprintf(gset_cr, sizeof(gset_cr), "4/%u", (unsigned)tm::kSloPlan.cr);
    return gset_cr;
}
const char* gsetTxPower(tm::Core&) {
    snprintf(gset_tx, sizeof(gset_tx), "%u dBm", (unsigned)tm::kSloPlan.power_dbm);
    return gset_tx;
}
const char* gsetPathHash(tm::Core&) {
    snprintf(gset_hash, sizeof(gset_hash), "%u B", (unsigned)meshcore::kFloodPathHashSize);
    return gset_hash;
}
const char* gsetTime(tm::Core&) {
    snprintf(gset_time, sizeof(gset_time), "%s · %s", om::time_source::hhmm(), om::time_source::bootSource());
    return gset_time;
}
const char* gsetSd(tm::Core&) {
    if (g_sd_mounted)
        snprintf(gset_sd, sizeof(gset_sd), "mounted · history %s", om::sdhistory::ready() ? "on" : "off");
    else
        snprintf(gset_sd, sizeof(gset_sd), "not mounted · history off");
    return gset_sd;
}
const char* gsetContacts(tm::Core&) {
    size_t contact_count = 0;
    {
        om::mc::StoreLock l;
        contact_count = om::mc::contacts().count;
    }
    snprintf(gset_contacts, sizeof(gset_contacts), "%u / %u", (unsigned)contact_count,
             (unsigned)om::mc::kMaxContacts);
    return gset_contacts;
}
const char* gsetVersion(tm::Core&) {
    snprintf(gset_ver, sizeof(gset_ver), "%s", tm::kAppVersion);
    return gset_ver;
}
const char* gsetHeap(tm::Core&) {
    const size_t bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (bytes >= 1024u * 1024u)
        snprintf(gset_heap, sizeof(gset_heap), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else
        snprintf(gset_heap, sizeof(gset_heap), "%u KB", (unsigned)(bytes / 1024u));
    return gset_heap;
}
const char* gsetPsram(tm::Core&) {
    const size_t bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (bytes >= 1024u * 1024u)
        snprintf(gset_psram, sizeof(gset_psram), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else
        snprintf(gset_psram, sizeof(gset_psram), "%u KB", (unsigned)(bytes / 1024u));
    return gset_psram;
}

// The two General rows that change something. The frame only REPORTS the row; these handlers are what
// actually touch the store, so a wrong row id can never silently write the wrong value.
void generalAdvertNow(tm::Core&, const tm::SettingRow&) {
    const om::mc::AdvertDecision d = om::mc::queueAdvert();
    char text[48];
    if (om::mc::advertRefusalText(d, text, sizeof(text)))
        om::ui::noticeSet(text, 3000);
    else
        om::ui::noticeClear();
}
void generalCycleAdvert(tm::Core&, const tm::SettingRow&) {
    const tm::AdvertInterval next =
        tm::advertIntervalCycle(tm::advertIntervalFromSeconds(om::mc::advertIntervalSeconds()));
    if (om::mc::setAdvertIntervalSeconds(tm::advertIntervalSeconds(next))) {
        ESP_LOGI(TAG, "settings: advert interval -> %s", tm::advertIntervalLabel(next));
    }
}

// S514/EST-450: the SOUND section. Every value is read from the live store (sound_store), never from a
// copy kept for the panel. The event rows are SubPages; the picker itself is built further down from the
// L2's own library, grouped by its shapes.
bool     g_sound_sub          = false;
int      g_sound_event        = -1;
int      g_sound_sub_focus    = -1;
int      g_sound_sub_scroll   = 0;
uint8_t  g_sound_preview_id   = 0xFE;   // no preview yet (real ids are < 0x100 and Off is 0xFF)
int      g_sound_preview_row  = -1;

constexpr uint8_t kNoSoundPreview = 0xFE;

const char* gsetSoundMaster(tm::Core&) {
    return om::soundstore::settings().master ? "On" : "Off";
}
const char* gsetSoundVolume(tm::Core&) {
    static char buf[16];
    // S514/EST-451: the row shows the keys themselves so '+' / '-' are discoverable on the glass.
    snprintf(buf, sizeof(buf), "- %u %% +", (unsigned)om::soundstore::settings().volume);
    return buf;
}
const char* gsetSoundChoiceValue(tm::Core&, const tm::SettingRow& row) {
    const int ev = (int)row.id - (int)kSetRowSoundEventBase;
    if (ev < 0 || ev >= tm::soundcfg::kEventCount) return "";
    return tm::soundcfg::choiceName(om::soundstore::settings().ids[ev]);
}

void generalToggleSound(tm::Core&, const tm::SettingRow&) {
    const bool on = !om::soundstore::settings().master;
    if (!om::soundstore::setMaster(on)) {
        om::ui::noticeSet("Sound setting not saved", 3000);
    } else {
        // A phrase already in the queue must not keep playing after "Off"; "On" restores the stored
        // volume for the next phrase (the audio task applies it again before every play).
        om::audioout::setVolume(on ? om::soundstore::settings().volume : 0);
        ESP_LOGI(TAG, "settings: sound master -> %s", on ? "on" : "off");
    }
}
void generalCycleSoundVolume(tm::Core&, const tm::SettingRow&) {
    const uint8_t next = tm::soundcfg::nextVolume(om::soundstore::settings().volume);
    if (!om::soundstore::setVolume(next))
        om::ui::noticeSet("Volume not saved", 3000);
    else {
        om::audioout::setVolume(next);
        ESP_LOGI(TAG, "settings: volume -> %u%%", (unsigned)next);
    }
}

// S514/EST-451: '+' / '=' / '-' on the Volume row. Ten at a time, clamped 0..100: the store is written
// first, then the new level is applied and the "Sent" sound previewed THROUGH the 400 ms gate (playEvent
// reads the store, so the audio task uses the new level). At a clamp there is no change, so nothing is
// written and no preview plays. Returns true when the value moved.
bool adjustSoundVolume(int delta) {
    const uint8_t cur  = om::soundstore::settings().volume;
    const uint8_t next = tm::soundcfg::stepVolume(cur, delta);
    if (next == cur) return false;
    if (!om::soundstore::setVolume(next)) {
        om::ui::noticeSet("Volume not saved", 3000);
        return false;
    }
    om::audioout::setVolume(next);
    om::audioout::playEvent(tm::soundcfg::Event::Sent);
    ESP_LOGI(TAG, "settings: volume -> %u%% (+/-)", (unsigned)next);
    return true;
}

static const tm::SettingRow kGeneralNodeRows[] = {
    {tm::SettingKind::SubPage, kSetRowName, "Name", "", gsetName, nullptr},
    {tm::SettingKind::Info, 0, "Public key", "", gsetPublicKey, nullptr},
    {tm::SettingKind::Action, kSetRowAdvertNow, "Advert now", "Enter", nullptr, generalAdvertNow},
};
static const tm::SettingRow kGeneralAdvertRows[] = {
    {tm::SettingKind::Cycle, kSetRowAdvertInterval, "Automatic advert", "", gsetAdvert, generalCycleAdvert},
};
static const tm::SettingRow kGeneralSoundRows[] = {
    {tm::SettingKind::Toggle, kSetRowSoundMaster, "Sound", "", gsetSoundMaster, generalToggleSound},
    {tm::SettingKind::Cycle, kSetRowSoundVolume, "Volume", "", gsetSoundVolume, generalCycleSoundVolume},
    {tm::SettingKind::SubPage, (uint16_t)(kSetRowSoundEventBase + (int)tm::soundcfg::Event::Message),
     "Message", "", tm::RowValueFn(gsetSoundChoiceValue), nullptr},
    {tm::SettingKind::SubPage, (uint16_t)(kSetRowSoundEventBase + (int)tm::soundcfg::Event::Dm),
     "Direct message", "", tm::RowValueFn(gsetSoundChoiceValue), nullptr},
    {tm::SettingKind::SubPage, (uint16_t)(kSetRowSoundEventBase + (int)tm::soundcfg::Event::Sent),
     "Sent", "", tm::RowValueFn(gsetSoundChoiceValue), nullptr},
    {tm::SettingKind::SubPage, (uint16_t)(kSetRowSoundEventBase + (int)tm::soundcfg::Event::NewNode),
     "New node", "", tm::RowValueFn(gsetSoundChoiceValue), nullptr},
    {tm::SettingKind::SubPage, (uint16_t)(kSetRowSoundEventBase + (int)tm::soundcfg::Event::Error),
     "Error", "", tm::RowValueFn(gsetSoundChoiceValue), nullptr},
    {tm::SettingKind::SubPage, (uint16_t)(kSetRowSoundEventBase + (int)tm::soundcfg::Event::Boot),
     "Boot", "", tm::RowValueFn(gsetSoundChoiceValue), nullptr},
};
static const tm::SettingRow kGeneralRadioRows[] = {
    {tm::SettingKind::Info, 0, "Frequency", "", gsetFrequency, nullptr},
    {tm::SettingKind::Info, 0, "Bandwidth", "", gsetBandwidth, nullptr},
    {tm::SettingKind::Info, 0, "Spreading", "", gsetSpreading, nullptr},
    {tm::SettingKind::Info, 0, "Coding", "", gsetCoding, nullptr},
    {tm::SettingKind::Info, 0, "TX power", "", gsetTxPower, nullptr},
    {tm::SettingKind::Info, 0, "Path hash", "", gsetPathHash, nullptr},
};
static const tm::SettingRow kGeneralDeviceRows[] = {
    {tm::SettingKind::Info, 0, "Time", "", gsetTime, nullptr},
    {tm::SettingKind::Info, 0, "SD card", "", gsetSd, nullptr},
    {tm::SettingKind::Info, 0, "Contacts", "", gsetContacts, nullptr},
    {tm::SettingKind::Info, 0, "App version", "", gsetVersion, nullptr},
    {tm::SettingKind::Info, 0, "Free internal heap", "", gsetHeap, nullptr},
    {tm::SettingKind::Info, 0, "Free PSRAM", "", gsetPsram, nullptr},
};
static const tm::SettingsSection kGeneralSections[] = {
    {"NODE", kGeneralNodeRows, 3},
    {"ADVERT", kGeneralAdvertRows, 1},
    {"SOUND", kGeneralSoundRows,
     (int)(sizeof(kGeneralSoundRows) / sizeof(kGeneralSoundRows[0]))},
    {"RADIO (READ ONLY)", kGeneralRadioRows, 6},
    {"DEVICE (READ ONLY)", kGeneralDeviceRows, 6},
};
constexpr int kGeneralSectionCount = 5;

// The CORE's General tab. Not a module: the shell owns it, exactly as the L2's General does.
const tm::SettingsSection* generalSettings(int* count) {
    if (count != nullptr) *count = kGeneralSectionCount;
    return kGeneralSections;
}

// OM Lite's tab: ONE scrolling list with the same sections General has. S514/EST-446 fills CHAT with the
// device's LIVE channels and the two add pages; S514/EST-447 fills NODES and MAP. RPTR stays the one honest
// line the ticket asks for.
//
// Everything below is read on EVERY draw (the scope getter through om::mc::channelScopeStored, the names
// from the live table, the pack rows from maplink::packStatus's cache), so a channel added, a contact
// removed or a pack pulled out shows up on the next frame without a restart.

// ---- S514/EST-447: the NODES section ------------------------------------------------------------------
// "Remove a contact" is a LIST sub-page (a SubPage row, built from the live table) and "Clear all contacts"
// raises the same confirm sheet; both destructive paths are performed in the Settings key branch, where the
// frame that owns the sheet lives. The notice row is a plain toggle through its handler.
enum : uint16_t {
    kNodesRowRemove = 1,
    kNodesRowClear,
    kNodesRowNotice,
};

const char* nodesNoticeValue(tm::Core&) { return om::mc::newNodeNoticeEnabled() ? "On" : "Off"; }

void nodesToggleNotice(tm::Core&, const tm::SettingRow&) {
    if (!om::mc::setNewNodeNoticeEnabled(!om::mc::newNodeNoticeEnabled()))
        om::ui::noticeSet("New-node notice not saved", 3000);
}

static const tm::SettingRow kOmNodesRows[] = {
    {tm::SettingKind::Info, 0, "Contacts", "", gsetContacts, nullptr},
    {tm::SettingKind::SubPage, kNodesRowRemove, "Remove a contact"},
    {tm::SettingKind::Action, kNodesRowClear, "Clear all contacts"},
    {tm::SettingKind::Toggle, kNodesRowNotice, "New-node notice", "", nodesNoticeValue, nodesToggleNotice},
};

// ---- S514/EST-447: the MAP section --------------------------------------------------------------------
enum : uint16_t {
    kMapRowStartPos = 20,
    kMapRowStartZoom,
};

const char* mapStartPositionValue(tm::Core&) {
    return om::map::startPositionLabel(om::maplink::mapStartPosition());
}

void mapCycleStartPosition(tm::Core&, const tm::SettingRow&) {
    const uint8_t next = om::map::startPositionCycle(om::maplink::mapStartPosition());
    if (!om::maplink::setMapStartPosition(next)) om::ui::noticeSet("Start position not saved", 3000);
}

const char* mapStartZoomValue(tm::Core&) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "z%u", (unsigned)om::maplink::mapStartZoom());
    return buf;
}

void mapCycleStartZoom(tm::Core&, const tm::SettingRow&) {
    const int next = om::map::startZoomCycle((int)om::maplink::mapStartZoom());
    if (!om::maplink::setMapStartZoom((uint8_t)next)) om::ui::noticeSet("Start zoom not saved", 3000);
}

// The read-only pack rows. The tile count is the index file's size / one record; `bytes` is the pack file.
void packText(const char* dir, bool with_bytes, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    if (!g_sd_mounted) {
        snprintf(out, cap, "no card");
        return;
    }
    om::maplink::PackStatus st;
    if (!om::maplink::packStatus(dir, &st) || !st.present) {
        snprintf(out, cap, "absent");
        return;
    }
    if (!with_bytes) {
        snprintf(out, cap, "%u tiles", (unsigned)st.tiles);
        return;
    }
    const double gb = (double)st.bytes / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0)
        snprintf(out, cap, "%u tiles · %.1f GB", (unsigned)st.tiles, gb);
    else
        snprintf(out, cap, "%u tiles · %.0f MB", (unsigned)st.tiles, (double)st.bytes / (1024.0 * 1024.0));
}

const char* mapPackSloValue(tm::Core&) {
    static char buf[40];
    packText("slo", false, buf, sizeof(buf));
    return buf;
}
const char* mapPackSlo16Value(tm::Core&) {
    static char buf[40];
    packText("slo16", false, buf, sizeof(buf));
    return buf;
}
const char* mapPackSlo17Value(tm::Core&) {
    static char buf[48];
    packText("slo17", true, buf, sizeof(buf));
    return buf;
}

static const tm::SettingRow kOmMapRows[] = {
    {tm::SettingKind::Cycle, kMapRowStartPos, "Start position", "", mapStartPositionValue,
     mapCycleStartPosition},
    {tm::SettingKind::Cycle, kMapRowStartZoom, "Start zoom", "", mapStartZoomValue, mapCycleStartZoom},
    {tm::SettingKind::Info, 0, "slo (z9-15)", "", mapPackSloValue, nullptr},
    {tm::SettingKind::Info, 0, "slo16 (z16)", "", mapPackSlo16Value, nullptr},
    {tm::SettingKind::Info, 0, "slo17 (z17)", "", mapPackSlo17Value, nullptr},
};
// ---- S514/EST-453: the RPTR section ----------------------------------------------------------------------
// Part 1 changes nothing on a repeater, so the section carries only the two local rules: whether a password may
// be remembered (the L2's On, its default) and one way to forget them all. "Forget all" raises the shared sheet.
enum : uint16_t {
    kRptrRowRemember = 300,
    kRptrRowForget,
};

const char* rptrRememberValue(tm::Core&) { return om::rptrlink::rememberEnabled() ? "On" : "Off"; }

void rptrToggleRemember(tm::Core&, const tm::SettingRow&) {
    if (!om::rptrlink::setRememberEnabled(!om::rptrlink::rememberEnabled()))
        om::ui::noticeSet("Remember passwords not saved", 3000);
}

static const tm::SettingRow kOmRptrRows[] = {
    {tm::SettingKind::Toggle, kRptrRowRemember, "Remember passwords", "", rptrRememberValue,
     rptrToggleRemember},
    {tm::SettingKind::Action, kRptrRowForget, "Forget all passwords"},
};

// ---- S514/EST-446: the CHAT section's channels, and the pages behind them ------------------------------
enum : uint16_t {
    kChatRowAddTag      = 100,
    kChatRowAddKey      = 101,
    kChatRowChannelBase = 200,   // + slot, like the L2's A_ChanBase
};
// The channel page's own rows, and the scope page's three choices.
enum : uint16_t {
    kChatSubName = 1,
    kChatSubScope,
    kChatSubKeyShare,
    kChatSubRemove,
    kChatSubScopeDefault,
    kChatSubScopeNone,
    kChatSubScopeRegion,
    kChatSubAddName,
    kChatSubAddKey,
    kChatSubAddCommit,
    kChatSubKeyValue,
};
enum class ChatSub : uint8_t { None = 0, Channel, Scope, AddKey, KeyShown };
enum class ChatField : uint8_t { None = 0, Tag, KeyName, KeyB64, ScopeRegion, Rename };
enum class ConfirmTarget : uint8_t { Name = 0, RemoveChannel, ShowKey, RemoveContact, ClearContacts, ForgetRptr };

// S514/EST-453: the RPTR page's UI state. The link owns the found list, the login machine and the status;
// these are only where the cursor and the typed password are.
int          g_rptr_mode      = 0;    // 0 list, 1 login, 2 status (matches om::ui::RptrMode)
int          g_rptr_focus     = 0;
char         g_rptr_pw[om::rptr::kPasswordMax + 1] = "";
size_t       g_rptr_pw_len    = 0;
uint32_t     g_rptr_status_ms = 0;
char         g_rptr_note[64]  = "";

ChatSub     g_chat_sub         = ChatSub::None;
ChatField   g_chat_field       = ChatField::None;
int         g_chat_channel     = -1;
int         g_chat_sub_focus   = -1;
int         g_chat_sub_scroll  = 0;
const char* g_chat_sub_header  = "";
char        g_chat_scope_text[meshcore::kMaxChannels][24];
char        g_chat_names[meshcore::kMaxChannels][32];
char        g_chat_show_key[25];
char        g_chat_add_name[meshcore::kChannelLabelMax + 1];
char        g_chat_add_key[64];
char        g_chat_title[48];
tm::SettingRow       g_chat_tab_rows[meshcore::kMaxChannels + 2];
tm::SettingRow       g_chat_sub_rows[6];
tm::SettingsSection  g_om_sections[4];
tm::SettingsSection  g_chat_sub_sections[1];

// S514/EST-447: the NODES list sub-page. Like the CHAT rows, it is rebuilt on every draw from the live table,
// in the Nodes page's own order (favourites first, then most recently heard), so the list and the page agree.
bool                g_nodes_list        = false;
int                 g_nodes_sub_focus   = -1;
int                 g_nodes_sub_scroll  = 0;
char                g_nodes_names[om::mc::kMaxContacts][om::mc::kNameBytes];
char                g_nodes_meta[om::mc::kMaxContacts][32];
tm::SettingRow      g_nodes_sub_rows[om::mc::kMaxContacts];
tm::SettingsSection g_nodes_sub_sections[1];
int                 g_confirm_contact = -1;

ConfirmTarget g_confirm_target  = ConfirmTarget::Name;
int           g_confirm_channel = -1;
char          g_confirm_title[48] = "Change the node name?";
char          g_confirm_text[160] = "Other nodes will see the new name after the next advert";
char          g_confirm_apply[16] = "Apply";
bool          g_confirm_danger  = false;

// One getter per row, with the ROW's id naming the slot (core/settings_model.h's row-aware getter). It
// reads the live table + stored scope on every draw; the ticket's words are "default"/"no scope"/"scope #x".
const char* chatScopeValue(tm::Core&, const tm::SettingRow& row) {
    const size_t i = (size_t)(row.id - kChatRowChannelBase);
    if (i >= meshcore::kMaxChannels) return "";
    char stored[meshcore::kScopeNameMax + 1] = "";
    om::mc::channelScopeStored(i, stored, sizeof(stored));
    meshcore::channelScopeRowText(stored, g_chat_scope_text[i], sizeof(g_chat_scope_text[i]));
    return g_chat_scope_text[i];
}

int buildChatTabRows() {
    int n = 0;
    const size_t count = om::mc::channelCount();
    for (size_t i = 0; i < count && i < meshcore::kMaxChannels; ++i) {
        snprintf(g_chat_names[i], sizeof(g_chat_names[i]), "%s", om::mc::channelName(i));
        g_chat_tab_rows[n++] = tm::SettingRow(tm::SettingKind::SubPage, (uint16_t)(kChatRowChannelBase + i),
                                              g_chat_names[i], "", tm::RowValueFn(chatScopeValue), nullptr);
    }
    g_chat_tab_rows[n++] = tm::SettingRow(tm::SettingKind::SubPage, kChatRowAddTag, "Add hashtag channel");
    g_chat_tab_rows[n++] = tm::SettingRow(tm::SettingKind::SubPage, kChatRowAddKey, "Add channel by key");
    return n;
}

void fillStoredScope(int slot) {
    char stored[meshcore::kScopeNameMax + 1] = "";
    if (slot >= 0) om::mc::channelScopeStored((size_t)slot, stored, sizeof(stored));
    meshcore::channelScopeRowText(stored, g_chat_scope_text[slot >= 0 ? slot : 0],
                                  sizeof(g_chat_scope_text[0]));
}

int buildChatSubRows() {
    int n = 0;
    switch (g_chat_sub) {
        case ChatSub::Channel: {
            g_chat_sub_header = "CHANNEL";
            // The LIVE name, so a rename is visible on the very next frame (the table reloads on the radio
            // task a moment later; this pointer is only used while drawing).
            const char* chname = g_chat_channel >= 0 ? om::mc::channelName((size_t)g_chat_channel) : "Channel";
            const bool hashtag = chname[0] == '#';
            snprintf(g_chat_title, sizeof(g_chat_title), "%s", chname);
            if (hashtag) {
                // A hashtag's name IS its key: nothing to rename.
                g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Info, kChatSubName, "Name", chname);
            } else {
                g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::SubPage, kChatSubName, "Name", chname);
            }
            fillStoredScope(g_chat_channel);
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::SubPage, kChatSubScope, "Region / scope",
                                                  g_chat_scope_text[g_chat_channel >= 0 ? g_chat_channel : 0]);
            if (!hashtag) {
                // Only a private channel has a key worth sharing (a hashtag's is derivable by anyone).
                g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Action, kChatSubKeyShare, "Key", "share");
            }
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Action, kChatSubRemove, "Remove channel");
            break;
        }
        case ChatSub::Scope: {
            g_chat_sub_header = "SEND SCOPE";
            snprintf(g_chat_title, sizeof(g_chat_title), "Region / scope");
            fillStoredScope(g_chat_channel);
            const meshcore::ScopeMode mode =
                meshcore::scopeModeFor(g_chat_scope_text[g_chat_channel >= 0 ? g_chat_channel : 0]);
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Action, kChatSubScopeDefault, "Default",
                                                  mode == meshcore::ScopeMode::Default ? "now" : "");
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Action, kChatSubScopeNone, "No scope",
                                                  mode == meshcore::ScopeMode::None ? "now" : "");
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Action, kChatSubScopeRegion, "Region",
                                                  mode == meshcore::ScopeMode::Region ? "now" : "type a name");
            break;
        }
        case ChatSub::AddKey: {
            g_chat_sub_header = "ADD CHANNEL BY KEY";
            snprintf(g_chat_title, sizeof(g_chat_title), "Add channel by key");
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::SubPage, kChatSubAddName, "Name",
                                                  g_chat_add_name[0] ? g_chat_add_name : "not set");
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::SubPage, kChatSubAddKey, "Key",
                                                  g_chat_add_key[0] ? "set" : "not set");
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Action, kChatSubAddCommit, "Add channel");
            break;
        }
        case ChatSub::KeyShown: {
            g_chat_sub_header = "CHANNEL KEY";
            snprintf(g_chat_title, sizeof(g_chat_title), "Channel key");
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Info, kChatSubKeyValue, "Key", g_chat_show_key);
            g_chat_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Info, 0, "Shown on screen only.", "");
            break;
        }
        default:
            g_chat_sub_header = "";
            break;
    }
    return n;
}

const tm::SettingsSection* chatSubSections(int* count) {
    const int n = buildChatSubRows();
    g_chat_sub_sections[0] = tm::SettingsSection(g_chat_sub_header, g_chat_sub_rows, n);
    if (count != nullptr) *count = n > 0 ? 1 : 0;
    return n > 0 ? g_chat_sub_sections : nullptr;
}

// S514/EST-447: the Remove-a-contact list. Built on every draw from the LIVE table, in the Nodes page's own
// order, so a removal (or an advert that added somebody) is visible on the next frame. The row's id is the
// STORE index, which is what contactRemoveAt() takes; the name and the type/last-heard pair are copied into
// this file's buffers because the rows must outlive the store lock.
int buildNodesSubRows() {
    int n = 0;
    om::mc::StoreLock l;
    om::mc::ContactStore& cs = om::mc::contacts();
    int order[om::mc::kMaxContacts];
    const int count = (int)om::mc::contactsOrder(cs, order, om::mc::kMaxContacts);
    if (count == 0) {
        g_nodes_sub_rows[n++] = tm::SettingRow(tm::SettingKind::Info, 0, "No contacts yet");
        return n;
    }
    const uint32_t now = om::time_source::epochNow();
    for (int i = 0; i < count && n < (int)om::mc::kMaxContacts; ++i) {
        const int idx = order[i];
        const om::mc::Contact& c = cs.c[idx];
        snprintf(g_nodes_names[n], sizeof(g_nodes_names[n]), "%s", c.name);
        char age[12];
        om::mc::formatAge(now, c.heard_at, age, sizeof(age));
        // "now ago" would read wrong; the type then the age, in the L2's words.
        if (strcmp(age, "now") == 0)
            snprintf(g_nodes_meta[n], sizeof(g_nodes_meta[n]), "%s · now", om::mc::contactTypeLabel(c.type));
        else
            snprintf(g_nodes_meta[n], sizeof(g_nodes_meta[n]), "%s · %s ago", om::mc::contactTypeLabel(c.type),
                     age);
        g_nodes_sub_rows[n] = tm::SettingRow(tm::SettingKind::Action, (uint16_t)idx, g_nodes_names[n],
                                             g_nodes_meta[n], nullptr, nullptr);
        ++n;
    }
    return n;
}

const tm::SettingsSection* nodesSubSections(int* count) {
    const int n = buildNodesSubRows();
    g_nodes_sub_sections[0] = tm::SettingsSection("REMOVE A CONTACT", g_nodes_sub_rows, n);
    if (count != nullptr) *count = n > 0 ? 1 : 0;
    return n > 0 ? g_nodes_sub_sections : nullptr;
}

const tm::SettingsSection* omLiteSettings(tm::Core&, int* count) {
    const int chat_n = buildChatTabRows();
    g_om_sections[0] = tm::SettingsSection("NODES", kOmNodesRows,
                                           (int)(sizeof(kOmNodesRows) / sizeof(kOmNodesRows[0])));
    g_om_sections[1] = tm::SettingsSection("CHAT", g_chat_tab_rows, chat_n);
    g_om_sections[2] = tm::SettingsSection("MAP", kOmMapRows,
                                           (int)(sizeof(kOmMapRows) / sizeof(kOmMapRows[0])));
    g_om_sections[3] = tm::SettingsSection("RPTR", kOmRptrRows, 2);
    if (count != nullptr) *count = 4;
    return g_om_sections;
}

// ---- S514/EST-450: the per-event sound picker ---------------------------------------------------------
// The L2's OWN picker order (EST-369): the whole shared library grouped by SHAPE (Solo, Duo, Trio, Multi,
// Alert), then Off. The ids are the L2's library ids, which is exactly what NVS holds, so nothing here
// invents a second numbering. `row.id` is the library id; kSoundPickOffId is the Off row.
constexpr uint16_t kSoundPickOffId = 0xFFFF;
constexpr int      kSoundPickMaxRows = 64;

tm::SettingRow      g_sound_pick_rows[kSoundPickMaxRows];
tm::SettingsSection g_sound_pick_sections[(int)om::sound::kShapeCount + 1];

int buildSoundPickSections() {
    if (g_sound_event < 0 || g_sound_event >= tm::soundcfg::kEventCount) return 0;
    const uint8_t current = om::soundstore::settings().ids[g_sound_event];
    int n = 0;
    int sections = 0;
    const int many = om::sound::libraryCount();
    for (int sh = 0; sh < (int)om::sound::kShapeCount; ++sh) {
        const om::sound::Shape shape = (om::sound::Shape)sh;
        const int first = n;
        for (int i = 0; i < many && n < kSoundPickMaxRows - 1; ++i) {
            const uint8_t id = om::sound::libraryId(i);
            if (om::sound::soundShape(id) != shape) continue;
            const char* name = om::sound::soundName(id);
            const char* value = id == g_sound_preview_id ? "preview" : (id == current ? "now" : "");
            g_sound_pick_rows[n++] = tm::SettingRow(tm::SettingKind::Action, (uint16_t)id,
                                                    name != nullptr ? name : "?", value, nullptr, nullptr);
        }
        if (n > first) {
            g_sound_pick_sections[sections++] =
                tm::SettingsSection(om::sound::shapeName(shape), &g_sound_pick_rows[first], n - first);
        }
    }
    // Off is its own short section at the end, so a shape can never change meaning when the library grows.
    const int off_first = n;
    g_sound_pick_rows[n++] =
        tm::SettingRow(tm::SettingKind::Action, kSoundPickOffId, "Off",
                       current == om::sound::kSoundOff ? "now" : "", nullptr, nullptr);
    g_sound_pick_sections[sections++] = tm::SettingsSection("OFF", &g_sound_pick_rows[off_first], 1);
    return sections;
}

const tm::SettingsSection* soundSubSections(int* count) {
    const int sections = buildSoundPickSections();
    if (count != nullptr) *count = sections;
    return sections > 0 ? g_sound_pick_sections : nullptr;
}

// Field order is core/module.h: id, name, meta, pages, available, owns, start, stop, tick, draw, onKey, onTop,
// settings.
tm::Module g_om_lite = {
    "om-lite",
    "OM Lite",
    "mesh companion · 5 pages",
    0x3F,                 // all six pages (Launcher, Nodes, Chat, Map, Rptr, Settings)
    true,                 // available
    tm::Resource::Radio | tm::Resource::SdCard,
    omStart,
    omStop,
    nullptr,              // tick
    nullptr,              // draw
    nullptr,              // onKey
    nullptr,              // onTop — installed in app_main, where the Chat open state lives (S514/EST-444)
    omLiteSettings,       // S514/EST-448: this app's own tab, as data
};

// S513/EST-432: the two LATER modules are real registry rows with available = false, so the menu's LATER
// chip comes from the registry and not from a string in the shell. They own nothing, have no pages and no
// hooks: listed, never startable, and neither app is built.
// Both are LATER and have no settings data yet, so Module::settings is nullptr and their tabs show the one
// honest line. That is the state EST-446/447 will replace from inside each app, not from this shell.
tm::Module g_ops_toc = {"ops-toc", "OPS-TOC", "field ops map & checklists", 0, false, tm::Resource::None,
                        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
tm::Module g_mesh_torry = {"mesh-torry", "Mesh-Torry", "the territory game", 0, false, tm::Resource::None,
                           nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

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
    // S514/EST-457: the emoji subset, parsed once with the L2's own table reader. A rejected blob logs
    // FAILED and leaves every emoji at today's behaviour; it must never take the app down.
    loadEmoji();
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
    // S514/EST-450: the sound path starts HERE, after the card is mounted and after hostedConnectC6()
    // above has brought the C6 link up. The BSP already initialised the codec/I2S inside
    // bsp_device_initialize (common/badge_bsp_device.c:84); audioout::begin() takes that handle rather than
    // initialising a second channel. A failure is one log line and the app runs silent: no crash, no
    // boot block.
    om::soundstore::begin();
    om::audioout::begin();
    // Boot is a normal event row but its stored default is Off (the L2's own default is Bell TK-TK).
    // This is the ONLY place the Boot event is raised: with the default it does nothing, and a sound
    // Filip chooses in Settings is what he hears on the next start.
    om::audioout::playEvent(tm::soundcfg::Event::Boot);
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
    static int nodes_focus = 0;
    // S514/EST-449: the Nodes search. SESSION-ONLY on purpose (never NVS): the query, its byte length and
    // which half of the page owns the keys live as long as the app does, so leaving the page and coming
    // back keeps the search. `nodes_focus` above is an index into the FILTERED list.
    char   nodes_query[om::ui::kNodeQueryBytes] = {0};
    size_t nodes_query_len = 0;
    static bool nodes_bar = false;   // true = the search bar has the focus; false = the list does
    // S514/EST-455: the node menu. `node_menu_index` is the STORE row the menu belongs to — not the focused
    // position — so an advert or a re-sort cannot make the sheet act on somebody else. The focus follows that
    // row after a favourite toggle (core/node_menu.h::nodeMenuFocusFollow).
    static bool node_menu_open  = false;
    static int  node_menu_focus = 0;
    // S514/EST-456: the last trace's hop list stays up until Esc or another node is traced.
    static bool node_trace_shown = false;
    static int  node_menu_index = -1;
    // S515/EST-461: the NEW contact's frozen order + the ack dwell. `nodes_frozen` is a snapshot of STORE rows
    // taken when the page OPENS (so an acked row keeps its place until the user leaves), and `nodes_ack` is the
    // 1.5 s rest timer. `nodes_key_moved` is raised by the key handlers below and by the own-key hook, and the
    // loop resolves it into one ack — so a move acks without every handler knowing about the rule.
    static om::ui::NodesFrozen nodes_frozen;
    static om::ui::NodesAck      nodes_ack;
    static bool                  nodes_key_moved = false;
    static tm::Page              nodes_page_prev = tm::Page::Launcher;
    // S513/EST-430: the Chat list is two columns of cards, so the selection is a (column, row) pair like the model
    // in core/chat_cards.h. `chat_focus` is the row in the Channels column (and therefore the channel index);
    // `chat_dm_focus` is the row in the Direct messages column, which cannot be focused while it has no cards.
    static int chat_col = om::mc::kColChannels;
    // S514/EST-444: these variables are `static` so the module's onTop hook (installed below, next to the
    // page state it acts on) can be a captureless lambda. S514/EST-454 added the focus/scroll fields to
    // that list because "already at the top" now also resets the selection.
    static int chat_focus  = 0;
    int    chat_dm_focus   = 0;
    // The Direct messages column's model. There is no TXT_MSG receive or send in this app yet, so the count is 0
    // and the page draws the L2's "No direct messages yet" line; a DM slice fills this and the view's dms[] and
    // nothing in the layout moves.
    int    chat_dm_count   = 0;
    // Each column scrolls by itself to keep its focused card visible (core/chat_cards::cardScroll).
    static int chat_card_scroll[om::mc::kCardCols] = {0, 0};
    static bool chat_open  = false;
    // S513/EST-434: when the open chat is a DM thread, who it is. `chat_dm_pub` is the thread key; a restored thread
    // may still be prefix-only until an advert fills the rest in (mc_link's rebind).
    static bool    chat_dm_open     = false;
    static uint8_t chat_dm_pub[32]  = {0};
    char   chat_dm_name[32] = "";
    // S503: how many messages the open channel is scrolled BACK from the newest (0 = pinned to the bottom), how
    // far it can go (from the last draw — a channel that all fits has nowhere to scroll), and how many messages it
    // had at the last draw, so arriving messages do not drag the view out from under the reader.
    static int chat_scroll     = 0;
    int    chat_max_scroll = 0;
    static int chat_seen_count = -1;
    // Step 6: the Map page's view. wx/wy are the world pixel at the top-left of the map area; -1 means
    // "not placed yet", which the first draw places from the MAP settings (Last position or Ljubljana) at the
    // chosen Start zoom (S514/EST-447; the default is still today's Ljubljana at z16).
    int64_t map_wx = -1, map_wy = -1;
    int     map_z  = om::map::startZoomFromStored((uint32_t)om::maplink::mapStartZoom(), true);
    // The last-position write's state: the monotonic ms of the last successful write (0 = none this boot) and
    // whether the view has moved since. The RULE is core/map_settings.h::lastPositionWriteDue (time passed in).
    uint32_t map_last_write_ms = 0;
    bool     map_view_changed  = false;
    char   composer[120] = {0};
    size_t composer_len  = 0;
    // S513: a short-lived line for the one thing the composer cannot say by itself — that a send was REFUSED
    // because the radio's queue is full, or because a DM's peer has no key yet. The text stays in the composer;
    // this says why nothing left. `chat_notice_buf` holds the one message that has to name the peer (EST-441).
    const char* chat_notice          = nullptr;
    int64_t     chat_notice_until_us = 0;
    char        chat_notice_buf[80]  = {0};

    auto clockText = []() { return om::time_source::hhmm(); };

    // The measure the wrap needs (S503): the real body font, over a byte range rather than a C string. A null
    // font measures 0, so every character would "fit" — the screen then clips the line, which is the old
    // behaviour and never worse than dropping the text.
    auto measureBody = [](void* ctx, const char* ptr, int len) -> int {
        const om::ui::Font* f = (const om::ui::Font*)ctx;
        return (f != nullptr && f->ready()) ? f->textWidthRange(ptr, len) : 0;
    };

    // ---- Settings (EST-445, per-app tabs S514/EST-448) ---------------------------------
    // The frame's decisions are core/settings.h (host-tested); every tab's CONTENT is data: General from
    // this shell, each app from Module::settings. The value getters read the real sources on every draw:
    //   NODE .... mcid/name (om::mc::nodeName), mcid seed's public key (om::mc::publicKeyHex)
    //   ADVERT .. mcid/advint (om::mc::advertIntervalSeconds) mapped by core/settings.h
    //   RADIO ... tm::kSloPlan, the exact plan radio_link.cpp pushes to the chip, and
    //             meshcore::kFloodPathHashSize, the value the advert/channel frames are built with
    //   DEVICE .. time_source::hhmm/bootSource, g_sd_mounted + sdhistory::ready, mc_store's contact count,
    //             tm::kAppVersion, heap_caps_get_free_size (internal / PSRAM)
    tm::SettingsFrame settings_frame;
    tm::Page           settings_return_page = tm::Page::Launcher;   // Esc returns HERE, not always the menu
    // S514/EST-446: one shared field, so it must hold a pasted base64 key (24 chars) as well as a name.
    char               settings_draft[64] = {0};
    size_t             settings_draft_len = 0;
    char               settings_pending_name[tm::kNodeNameMaxBytes + 1] = {0};

    static tm::SettingsTabDef settings_tabs[tm::kSettingsMaxTabs];
    int                       settings_tab_count = 0;

    auto refreshSettingsTabs = [&]() {
        settings_tab_count = tm::settingsTabDefs(core.registry(), settings_tabs, tm::kSettingsMaxTabs);
    };
    refreshSettingsTabs();

    // The current tab's sections. General is the core's; every other tab asks its module through the hook.
    // A nullptr/0 answer is valid and is what the one honest line draws.
    auto settingsSectionsForTab = [&](int tab, int* count) -> const tm::SettingsSection* {
        if (tab <= tm::kSettingsGeneralTab) return generalSettings(count);
        if (tab < settings_tab_count) return tm::settingsTabContent(settings_tabs[tab], core, count);
        if (count != nullptr) *count = 0;
        return nullptr;
    };

    // A stopped module still SHOWS its stored settings; only an action that needs it running is refused.
    auto settingsTabRunning = [&](int tab) -> bool {
        if (tab <= tm::kSettingsGeneralTab) return true;   // the core
        if (tab < settings_tab_count && settings_tabs[tab].module != nullptr)
            return core.registry().isRunning(settings_tabs[tab].module->id);
        return false;   // a LATER module
    };

    auto drawSettingsPage = [&]() {
        refreshSettingsTabs();
        // A registry that shrank must not leave the frame on a tab that no longer exists.
        settings_frame.tab = tm::settingsTabIndex(settings_frame.tab, settings_tab_count);
        int n = 0;
        const tm::SettingsSection* sections = settingsSectionsForTab(settings_frame.tab, &n);
        if (settings_frame.focus < 0 || settings_frame.focus >= tm::settingsElementCount(sections, n))
            settings_frame.focusFirst(sections, n);
        om::ui::SettingsView sv;
        snprintf(sv.clock, sizeof(sv.clock), "%s", clockText());
        sv.battery_pct   = view.battery_pct;
        sv.charging      = view.charging;
        sv.tabs          = settings_tabs;
        sv.tab_count     = settings_tab_count;
        sv.tab           = settings_frame.tab;
        sv.sections      = sections;
        sv.section_count = n;
        sv.core          = &core;
        sv.tab_running   = settingsTabRunning(settings_frame.tab);
        sv.focus         = settings_frame.focus;
        sv.scroll        = settings_frame.scroll;
        sv.sub_page      = settings_frame.subPage();
        // S514/EST-451: the Volume row's hint bar carries '+' / '-'. Only a ROOT row can be the Volume row:
        // a sub-page has its own focus index into a different list.
        sv.volume_row = false;
        if (!settings_frame.subPage() && settings_frame.tab == tm::kSettingsGeneralTab) {
            const tm::SettingRow* frow = tm::settingsRowAt(sections, n, settings_frame.focus);
            sv.volume_row = (frow != nullptr && frow->id == kSetRowSoundVolume);
        }
        sv.sub_title     = "Node name";
        sv.draft         = settings_draft;
        sv.draft_len     = settings_draft_len;
        // S514/EST-446: a chat LIST sub-page draws its own sections with the frame's own row renderer;
        // a chat TEXT page draws the shared field with that page's rules. The General Name editor keeps
        // the defaults above.
        if (settings_frame.subPage() && g_chat_field != ChatField::None) {
            sv.sections      = nullptr;
            sv.section_count = 0;
            sv.sub_title     = g_chat_title;
            switch (g_chat_field) {
                case ChatField::Tag:
                    sv.sub_hint = "One #, no spaces; A-Z becomes a-z. 30 characters maximum";
                    sv.sub_max  = meshcore::kChannelTagMax;
                    break;
                case ChatField::KeyB64:
                    sv.sub_hint = "Base64 of 16 bytes; spaces are ignored, = ends the text";
                    sv.sub_max  = 64;
                    break;
                case ChatField::ScopeRegion:
                    sv.sub_hint = "Region, #tag characters, 30 maximum; case matters";
                    sv.sub_max  = meshcore::kScopeNameMax;
                    break;
                default:
                    sv.sub_hint = "Allowed: letters, digits, space; no [ ] \\ : , ? *";
                    sv.sub_max  = tm::kNodeNameMaxBytes;
                    break;
            }
        } else if (settings_frame.subPage() && g_sound_sub) {
            // S514/EST-450: the per-event picker. Same row renderer as every tab, grouped by the L2's
            // shapes. The sub-title names the event; the row's value says "now" (stored) or "preview".
            int sn = 0;
            sv.sections      = soundSubSections(&sn);
            sv.section_count = sn;
            sv.sub_title     = tm::soundcfg::eventLabel((tm::soundcfg::Event)g_sound_event);
            sv.focus         = g_sound_sub_focus;
            sv.scroll        = g_sound_sub_scroll;
        } else if (settings_frame.subPage() && g_nodes_list) {
            // S514/EST-447: the NODES list sub-page. Same row renderer as a tab; the focus/scroll are its own.
            int sn = 0;
            sv.sections      = nodesSubSections(&sn);
            sv.section_count = sn;
            sv.sub_title     = "Remove a contact";
            sv.focus         = g_nodes_sub_focus;
            sv.scroll        = g_nodes_sub_scroll;
        } else if (settings_frame.subPage() && g_chat_sub != ChatSub::None) {
            int sn = 0;
            sv.sections      = chatSubSections(&sn);
            sv.section_count = sn;
            sv.sub_title     = g_chat_title;
            sv.focus         = g_chat_sub_focus;
            sv.scroll        = g_chat_sub_scroll;
        }
        sv.confirm       = settings_frame.confirmOpen();
        sv.confirm_title = g_confirm_title;
        sv.confirm_text  = g_confirm_text;
        sv.confirm_apply = g_confirm_apply;
        sv.confirm_danger = g_confirm_danger;
        om::ui::drawSettings(g_screen, sv);
        if (sv.confirm) om::ui::drawConfirmSheet(g_screen, sv);
    };

    // ---- S514/EST-446: the chat pages' actions, in ONE place so a tab row and a sub-page row run the same
    // code. The key itself is only ever copied into g_chat_show_key for the on-screen page; it is never
    // logged, and the tab rows read names/scopes from the live table on every draw.
    auto closeChatPage = [&]() {
        settings_frame.closeSubPage();
        g_sound_sub    = false;   // S514/EST-450
        g_chat_sub     = ChatSub::None;
        g_chat_field   = ChatField::None;
        g_chat_channel = -1;
        settings_draft[0] = '\0';
        settings_draft_len = 0;
        g_chat_sub     = ChatSub::None;
        g_chat_field   = ChatField::None;
        g_chat_channel = -1;
    };
    auto openChatList = [&](ChatSub sub) {
        g_chat_sub   = sub;
        g_chat_field = ChatField::None;
        g_sound_sub  = false;   // S514/EST-450: only one sub-page's state may be live
        if (!settings_frame.subPage()) settings_frame.openSubPage();
        int n = 0;
        const tm::SettingsSection* ss = chatSubSections(&n);
        g_chat_sub_focus  = tm::settingsFirstFocusable(ss, n);
        g_chat_sub_scroll = 0;
    };

    // S514/EST-447: the NODES list sub-page. Its own two state variables, so closing it cannot disturb the
    // chat pages' focus/scroll (and vice versa); the frame's sub-page depth is shared, as it is for every page.
    auto closeNodesPage = [&]() {
        settings_frame.closeSubPage();
        g_sound_sub        = false;   // S514/EST-450
        g_nodes_list       = false;
        g_nodes_sub_focus  = -1;
        g_nodes_sub_scroll = 0;
        // S514/EST-455: leaving the Nodes page (or finishing a removal) closes its menu too.
        node_menu_open  = false;
        node_menu_focus = 0;
        node_menu_index = -1;
    };
    auto openNodesPage = [&]() {
        g_nodes_list = true;
        g_chat_sub   = ChatSub::None;
        g_chat_field = ChatField::None;
        g_chat_channel = -1;
        g_sound_sub  = false;   // S514/EST-450
        if (!settings_frame.subPage()) settings_frame.openSubPage();
        int n = 0;
        const tm::SettingsSection* ss = nodesSubSections(&n);
        g_nodes_sub_focus  = tm::settingsFirstFocusable(ss, n);
        g_nodes_sub_scroll = 0;
    };
    auto openChatFieldPage = [&](ChatField f, const char* initial, const char* title) {
        g_chat_field = f;
        g_sound_sub  = false;   // S514/EST-450
        snprintf(g_chat_title, sizeof(g_chat_title), "%s", title);
        snprintf(settings_draft, sizeof(settings_draft), "%s", initial != nullptr ? initial : "");
        settings_draft_len = strlen(settings_draft);
        if (!settings_frame.subPage()) settings_frame.openSubPage();
    };
    auto fieldMax = [](ChatField f) -> size_t {
        switch (f) {
            case ChatField::Tag:         return meshcore::kChannelTagMax;
            case ChatField::KeyB64:      return 64;
            case ChatField::ScopeRegion: return meshcore::kScopeNameMax;
            default:                     return tm::kNodeNameMaxBytes;
        }
    };
    auto fieldAllows = [](ChatField f, char c) -> bool {
        const unsigned char u = (unsigned char)c;
        if (u < 0x20 || u > 0x7E) return false;
        switch (f) {
            case ChatField::Tag:         return true;   // the normaliser removes '#' and spaces, lowercases A-Z
            case ChatField::KeyB64:      return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                                                (u >= '0' && u <= '9') || u == '+' || u == '/' || u == '=' ||
                                                u == ' ';
            case ChatField::ScopeRegion: return u == '#' || u == ' ' || meshcore::scopeNameChar(c);
            default:                     return tm::nodeNameCharAllowed(c);
        }
    };
    auto applyChatField = [&]() {
        switch (g_chat_field) {
            case ChatField::Tag: {
                char name[meshcore::kChannelTagMax + 1];
                if (om::mc::channelsAddHashtag(settings_draft, name, sizeof(name))) {
                    char m[64];
                    snprintf(m, sizeof(m), "Added %s", name);
                    om::ui::noticeSet(m, 3000);
                    closeChatPage();
                } else {
                    om::ui::noticeSet("Not added: duplicate or not a hashtag", 3000);
                }
                return;
            }
            case ChatField::KeyName:
                snprintf(g_chat_add_name, sizeof(g_chat_add_name), "%s", settings_draft);
                g_chat_field = ChatField::None;
                openChatList(ChatSub::AddKey);
                return;
            case ChatField::KeyB64:
                snprintf(g_chat_add_key, sizeof(g_chat_add_key), "%s", settings_draft);
                g_chat_field = ChatField::None;
                openChatList(ChatSub::AddKey);
                return;
            case ChatField::ScopeRegion: {
                char canon[meshcore::kScopeNameMax + 1];
                if (g_chat_channel >= 0 &&
                    meshcore::scopeNameCanonical(settings_draft, canon, sizeof(canon)) > 0 &&
                    om::mc::channelScopeSet((size_t)g_chat_channel, canon)) {
                    om::ui::noticeSet("Scope saved", 3000);
                    g_chat_field = ChatField::None;
                    openChatList(ChatSub::Scope);
                } else {
                    om::ui::noticeSet("Region: 1-30 chars, letters/digits - $ #", 3000);
                }
                return;
            }
            case ChatField::Rename:
                if (g_chat_channel >= 0 && om::mc::channelsRename((size_t)g_chat_channel, settings_draft))
                    om::ui::noticeSet("Name saved", 3000);
                else
                    om::ui::noticeSet("Name not saved", 3000);
                g_chat_field = ChatField::None;
                openChatList(ChatSub::Channel);
                return;
            default:
                return;
        }
    };
    auto chatSubAction = [&](uint16_t id) {
        const size_t ch = g_chat_channel >= 0 ? (size_t)g_chat_channel : 0;
        switch (id) {
            case kChatSubName:
                openChatFieldPage(ChatField::Rename, om::mc::channelName(ch), "Channel name");
                break;
            case kChatSubScope:
                fillStoredScope(g_chat_channel);
                openChatList(ChatSub::Scope);
                break;
            case kChatSubKeyShare:
                g_confirm_target  = ConfirmTarget::ShowKey;
                g_confirm_channel = g_chat_channel;
                snprintf(g_confirm_title, sizeof(g_confirm_title), "Show the key?");
                snprintf(g_confirm_text, sizeof(g_confirm_text), "Show the secret key on screen?");
                snprintf(g_confirm_apply, sizeof(g_confirm_apply), "Show");
                g_confirm_danger = false;
                settings_frame.beginConfirm(g_chat_sub_focus);
                break;
            case kChatSubRemove:
                g_confirm_target  = ConfirmTarget::RemoveChannel;
                g_confirm_channel = g_chat_channel;
                snprintf(g_confirm_title, sizeof(g_confirm_title), "Remove channel");
                snprintf(g_confirm_text, sizeof(g_confirm_text),
                         "Remove %s? Its history stays on the card.", om::mc::channelName(ch));
                snprintf(g_confirm_apply, sizeof(g_confirm_apply), "Remove");
                g_confirm_danger = true;
                settings_frame.beginConfirm(g_chat_sub_focus);
                break;
            case kChatSubScopeDefault:
                if (om::mc::channelScopeSet(ch, "")) om::ui::noticeSet("Scope: default", 3000);
                fillStoredScope(g_chat_channel);
                openChatList(ChatSub::Scope);
                break;
            case kChatSubScopeNone:
                if (om::mc::channelScopeSet(ch, meshcore::kScopeNone)) om::ui::noticeSet("Scope: none", 3000);
                fillStoredScope(g_chat_channel);
                openChatList(ChatSub::Scope);
                break;
            case kChatSubScopeRegion: {
                char stored[meshcore::kScopeNameMax + 1] = "";
                om::mc::channelScopeStored(ch, stored, sizeof(stored));
                const char* initial =
                    meshcore::scopeModeFor(stored) == meshcore::ScopeMode::Region ? stored : "";
                openChatFieldPage(ChatField::ScopeRegion, initial, "Region name");
                break;
            }
            case kChatSubAddName:
                openChatFieldPage(ChatField::KeyName, g_chat_add_name, "Channel name");
                break;
            case kChatSubAddKey:
                openChatFieldPage(ChatField::KeyB64, g_chat_add_key, "Channel key");
                break;
            case kChatSubAddCommit:
                if (om::mc::channelsAddByKey(g_chat_add_name, g_chat_add_key)) {
                    om::ui::noticeSet("Channel added", 3000);
                    closeChatPage();
                } else {
                    om::ui::noticeSet("Not added: bad key, name or duplicate", 3000);
                }
                break;
            default:
                break;
        }
    };

    // S514/EST-447: Enter on a contact in the Remove-a-contact list. The NAME is copied now, under the lock,
    // so the sheet's question still names the right node even if an advert refreshes the table while the
    // question is up; the APPLY path removes by the store index captured here, and contactRemoveAt() refuses
    // an index that no longer exists rather than removing a different row.
    auto nodesSubAction = [&](uint16_t id) {
        char name[om::mc::kNameBytes] = {0};
        {
            om::mc::StoreLock l;
            om::mc::ContactStore& cs = om::mc::contacts();
            if ((size_t)id < cs.count) snprintf(name, sizeof(name), "%s", cs.c[id].name);
        }
        if (name[0] == '\0') snprintf(name, sizeof(name), "this contact");
        g_confirm_target  = ConfirmTarget::RemoveContact;
        g_confirm_contact = (int)id;
        snprintf(g_confirm_title, sizeof(g_confirm_title), "Remove contact");
        snprintf(g_confirm_text, sizeof(g_confirm_text),
                 "Remove %s? A DM from it cannot be opened until it advertises again.", name);
        snprintf(g_confirm_apply, sizeof(g_confirm_apply), "Remove");
        g_confirm_danger = true;
        settings_frame.beginConfirm(g_nodes_sub_focus);
    };

    // Clear-all's question, raised from the NODES root row. The count is read at the moment the sheet opens;
    // the Apply path re-reads the live table, so the number in the question is never used as a target.
    auto raiseClearContacts = [&](int focus_row) {
        size_t n = 0;
        {
            om::mc::StoreLock l;
            n = om::mc::contacts().count;
        }
        if (n == 0) {
            om::ui::noticeSet("No contacts yet", 3000);
            return;
        }
        g_confirm_target = ConfirmTarget::ClearContacts;
        snprintf(g_confirm_title, sizeof(g_confirm_title), "Clear all contacts");
        snprintf(g_confirm_text, sizeof(g_confirm_text),
                 "Remove all %u contacts? A DM from one cannot be opened until it advertises again.",
                 (unsigned)n);
        snprintf(g_confirm_apply, sizeof(g_confirm_apply), "Remove all");
        g_confirm_danger = true;
        settings_frame.beginConfirm(focus_row);
    };

    // Both page views are BUILT and DRAWN under the store lock; the lock is released before the panel blit, so
    // the radio task never waits for an 800x480 push, and a draw never sees a half-refreshed node.
    // S514/EST-456: the captured menu row's public key, copied under the store lock. False for a stale index, so
    // no action and no pending read can touch a row that moved or vanished.
    auto nodeMenuPub = [&](uint8_t pub[32]) -> bool {
        om::mc::StoreLock l;
        om::mc::ContactStore& cs = om::mc::contacts();
        if (node_menu_index < 0 || (size_t)node_menu_index >= cs.count) return false;
        memcpy(pub, cs.c[node_menu_index].pub, 32);
        return true;
    };

    // S515/EST-461: the order EVERY Nodes lambda draws and filters. On the Nodes page it is the FROZEN snapshot
    // (kept true to the table: a contact that arrived is appended, a removed one dropped; never re-sorted), so
    // an acked row keeps its place until the user leaves. Anywhere else the live contactsOrder().
    auto nodesOrderForPage = [&](int* out) -> int {
        om::mc::StoreLock     l;
        om::mc::ContactStore& cs = om::mc::contacts();
        if (core.router().current() == tm::Page::Nodes) {
            om::ui::nodesFrozenSync(nodes_frozen, cs);
            const int n = nodes_frozen.count;
            for (int i = 0; i < n; ++i) out[i] = nodes_frozen.order[i];
            return n;
        }
        return (int)om::mc::contactsOrder(cs, out, om::mc::kMaxContacts);
    };

    auto drawNodesPage = [&]() {
        om::ui::NodesView nv;
        nv.clock       = clockText();
        nv.battery_pct = view.battery_pct;
        nv.charging    = view.charging;
        int order[om::mc::kMaxContacts];
        int shown[om::mc::kMaxContacts];
        {
            om::mc::StoreLock l;
            nv.store       = &om::mc::contacts();
            const int total = nodesOrderForPage(order);
            // S514/EST-449: the list shows the FILTERED order; the bar prints "N of M" against the whole
            // contact table (store->count), exactly the L2's count line.
            nv.order       = shown;
            nv.order_count = om::ui::nodeSearchFilter(&om::mc::contacts(), order, total, nodes_query, shown);
            if (nodes_focus >= nv.order_count) nodes_focus = nv.order_count > 0 ? nv.order_count - 1 : 0;
            if (nodes_focus < 0) nodes_focus = 0;
            // S514/EST-455: the menu belongs to a STORE row; after a toggle (or an advert) the focus follows it,
            // so the row the sheet names and the row the list rings are always the same node.
            bool menu_shown = false;
            if (node_menu_open && node_menu_index >= 0) {
                const int pos = om::ui::nodeMenuFocusFollow(shown, nv.order_count, node_menu_index);
                if (pos >= 0) {
                    nodes_focus = pos;
                    menu_shown  = true;
                }
            }
            nv.query          = nodes_query;
            nv.search_focused = nodes_bar;
            nv.menu_open      = menu_shown;
            nv.menu_focus     = node_menu_focus;
            // S514/EST-456: a ping/trace in flight says so in the row; the trace result gets its own sheet.
            if (menu_shown) {
                uint8_t pub[32];
                if (nodeMenuPub(pub)) {
                    nv.menu_ping_pending  = om::mc::nodePingPendingFor(pub);
                    nv.menu_trace_pending = om::mc::nodeTracePendingFor(pub);
                }
            }
            static om::ops::TraceView trace_view;
            nv.trace = nullptr;
            if (node_trace_shown && om::mc::nodeTraceResult(&trace_view)) nv.trace = &trace_view;
            const uint32_t now = om::time_source::epochNow();
            nv.focused         = nodes_focus;
            nv.now             = now;
            nv.in_last_hour    = (int)om::mc::contactsHeardWithin(om::mc::contacts(), now, 3600);
            om::ui::drawNodes(g_screen, nv);
        }
        // S514/EST-455: Remove raises the EST-447 confirm on THIS page. The sheet is drawn over the list with
        // the same shared frame and look the Settings sub-page uses.
        if (settings_frame.confirmOpen() && g_confirm_target == ConfirmTarget::RemoveContact) {
            om::ui::SettingsView sv;
            sv.confirm        = true;
            sv.confirm_title  = g_confirm_title;
            sv.confirm_text   = g_confirm_text;
            sv.confirm_apply  = g_confirm_apply;
            sv.confirm_danger = g_confirm_danger;
            om::ui::drawConfirmSheet(g_screen, sv);
        }
    };

    // S514/EST-449: how many rows the current query keeps. The key handler needs the count for Down from
    // the bar (the first MATCH) and the list's clamp; it is the SAME filter the draw uses, not a second one.
    auto nodesShownCount = [&]() -> int {
        int order[om::mc::kMaxContacts];
        int shown[om::mc::kMaxContacts];
        om::mc::StoreLock l;
        const int total = nodesOrderForPage(order);   // S515/EST-461: the frozen order on the page
        return om::ui::nodeSearchFilter(&om::mc::contacts(), order, total, nodes_query, shown);
    };

    // ---- S514/EST-455: the NODE MENU's side effects ---------------------------------------------------------
    // The rows and their order are core/node_menu.h (host-tested); these lambdas only perform what a chosen row
    // does, and every read of the contact table is under the store lock.
    auto nodesShownOrder = [&](int* shown) -> int {
        int order[om::mc::kMaxContacts];
        om::mc::StoreLock l;
        const int total = nodesOrderForPage(order);   // S515/EST-461: the frozen order on the page
        return om::ui::nodeSearchFilter(&om::mc::contacts(), order, total, nodes_query, shown);
    };

    auto nodesFocusedStoreIndex = [&]() -> int {
        int       shown[om::mc::kMaxContacts];
        const int n = nodesShownOrder(shown);
        if (nodes_focus < 0 || nodes_focus >= n) return -1;
        return shown[nodes_focus];
    };

    // Enter on Favourite (or 'f' on the list). Structural change: RAM under the lock, the EST-437 dirty flag,
    // the notice, then the list re-sorts and the focus FOLLOWS the store row.
    auto toggleFavourite = [&](int store_index) {
        if (store_index < 0) return;
        char name[om::mc::kNameBytes] = {0};
        bool on = false;
        {
            om::mc::StoreLock      l;
            om::mc::ContactStore&  cs = om::mc::contacts();
            if ((size_t)store_index >= cs.count) return;
            snprintf(name, sizeof(name), "%s", cs.c[store_index].name);
            on = !cs.c[store_index].favourite;
        }
        if (!om::mc::contactsSetFavourite((size_t)store_index, on)) {
            om::ui::noticeSet("Favourite not saved", 3000);
            return;
        }
        char note[64];
        om::ui::nodeFavouriteNotice(note, sizeof(note), name, on);
        om::ui::noticeSet(note, 3000);

        int       shown[om::mc::kMaxContacts];
        const int n   = nodesShownOrder(shown);
        const int pos = om::ui::nodeMenuFocusFollow(shown, n, store_index);
        if (pos >= 0) nodes_focus = pos;
        ESP_LOGI(TAG, "nodes: favourite %s -> %s", name, on ? "on" : "off");
    };

    // Direct message: open this contact's thread in Chat. There need not be a message yet — the thread view is
    // the contact's key + name, and the Chat page already draws its honest empty state ("No messages in this
    // thread yet."). The DM card column only lists threads that have messages, so this is also how a first DM
    // is started.
    auto openDmThread = [&](int store_index) {
        uint8_t pub[32] = {0};
        char    name[om::mc::kNameBytes] = {0};
        {
            om::mc::StoreLock     l;
            om::mc::ContactStore& cs = om::mc::contacts();
            if ((size_t)store_index >= cs.count) return;
            memcpy(pub, cs.c[store_index].pub, sizeof(pub));
            snprintf(name, sizeof(name), "%s", cs.c[store_index].name);
        }
        if (name[0] == '\0')
            snprintf(name, sizeof(name), "%02x%02x%02x%02x", pub[0], pub[1], pub[2], pub[3]);
        memcpy(chat_dm_pub, pub, sizeof(chat_dm_pub));
        snprintf(chat_dm_name, sizeof(chat_dm_name), "%s", name);
        chat_dm_open    = true;
        chat_open       = true;
        chat_scroll     = 0;
        chat_seen_count = -1;
        om::mc::noteDmRead(chat_dm_pub);
        core.goPage(tm::Page::Chat);
        ESP_LOGI(TAG, "nodes: DM thread with %s", name);
    };

    // Remove: the EST-447 confirm, raised HERE on the Nodes page (the shared sheet + target, so Cancel changes
    // nothing and Apply is the same contactsRemove() the Settings sub-page uses).
    auto raiseRemoveContactConfirm = [&](int store_index) {
        char name[om::mc::kNameBytes] = {0};
        {
            om::mc::StoreLock     l;
            om::mc::ContactStore& cs = om::mc::contacts();
            if ((size_t)store_index >= cs.count) return;
            snprintf(name, sizeof(name), "%s", cs.c[store_index].name);
        }
        if (name[0] == '\0') snprintf(name, sizeof(name), "this contact");
        g_confirm_target  = ConfirmTarget::RemoveContact;
        g_confirm_contact = store_index;
        snprintf(g_confirm_title, sizeof(g_confirm_title), "Remove contact");
        snprintf(g_confirm_text, sizeof(g_confirm_text),
                 "Remove %s? A DM from it cannot be opened until it advertises again.", name);
        snprintf(g_confirm_apply, sizeof(g_confirm_apply), "Remove");
        g_confirm_danger = true;
        settings_frame.beginConfirm(nodes_focus);
    };

    // The sheet's current rows, read under the lock: the count for Up/Down and the action Enter would run.
    auto nodeMenuRowCount = [&](int store_index, om::ui::NodeMenuRow* out, int max) -> int {
        om::mc::StoreLock     l;
        om::mc::ContactStore& cs = om::mc::contacts();
        if ((size_t)store_index >= cs.count) return 0;
        om::ui::NodeMenuContext ctx;
        ctx.advType      = cs.c[store_index].type;
        ctx.hasKey       = !om::mc::contactKeyIncomplete(cs.c[store_index].pub);
        ctx.favourite    = cs.c[store_index].favourite;
        ctx.pathKnown    = om::mc::contactHasOutPath(cs.c[store_index]);
        ctx.pingPending  = om::mc::nodePingPendingFor(cs.c[store_index].pub);
        ctx.tracePending = om::mc::nodeTracePendingFor(cs.c[store_index].pub);
        return om::ui::nodeMenuRows(ctx, out, max);
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
                    // S513/EST-440: the open channel's scope, in the ticket's words on the head's dim
                    // right side ("scope #si" / "no scope"; empty = unscoped). Read through the live
                    // hash and cached after the first read, so this is not a flash read per redraw.
                    om::mc::channelScopeHead((size_t)chat_focus, cv.scope, sizeof(cv.scope));
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
                // First draw. S514/EST-447: the MAP settings decide. "Last position" restores the centre of the
                // view the Map page was left at (NVS), converted to the chosen Start zoom; Ljubljana is the
                // default and today's behaviour. Either answer is clamped, and a missing/invalid saved view
                // falls back to Ljubljana rather than guessing.
                bool placed = false;
                if (om::map::startPositionFromStored(om::maplink::mapStartPosition(), true) == om::map::kStartLast) {
                    om::map::SavedView saved;
                    if (om::maplink::loadLastView(&saved)) {
                        placed = om::map::startViewFromSaved(saved, map_z, om::ui::kMapAreaW, om::ui::kMapAreaH,
                                                            &map_wx, &map_wy);
                    }
                }
                if (!placed) {
                    // Centre ON Ljubljana (Presernov trg, 46.0514 N 14.5060 E), Filip's ask in S513. It used to
                    // centre the TILE containing 46.15 N 14.99 E (near Litija), so the map opened somewhere else
                    // and up to half a tile off.
                    map_wx = om::map::worldPxXForLon(14.5060, map_z) - om::ui::kMapAreaW / 2;
                    map_wy = om::map::worldPxYForLat(46.0514, map_z) - om::ui::kMapAreaH / 2;
                }
                om::map::clampWorldPixel(map_z, &map_wx, &map_wy, om::ui::kMapAreaW, om::ui::kMapAreaH);
            }
            mv.wx = map_wx;
            mv.wy = map_wy;
        }
        om::ui::drawMap(g_screen, mv);
    };

    // S514/EST-447: the Map page's "last position". The RULE (core/map_settings.h) is: only when the view moved,
    // while on the page at most once a minute, and ALWAYS on leaving. `leaving` is why this is not called from a
    // key handler (a pan marks the view changed; the loop decides when that may reach flash).
    auto saveMapLastView = [&](bool leaving) {
        if (!om::maplink::ready() || !g_sd_mounted) return;   // nothing on screen to remember
        if (map_wx < 0 || map_wy < 0) return;
        if (om::map::startPositionFromStored(om::maplink::mapStartPosition(), true) != om::map::kStartLast) return;
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (!om::map::lastPositionWriteDue(leaving, map_view_changed, now_ms, map_last_write_ms)) return;
        om::map::SavedView saved;
        saved.valid = true;
        saved.wx    = (int32_t)map_wx;
        saved.wy    = (int32_t)map_wy;
        saved.z     = (uint8_t)map_z;
        if (om::maplink::saveLastView(saved)) {
            map_last_write_ms = now_ms;
            map_view_changed  = false;
        }
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

    // S514/EST-444 (Filip): the module's own-symbol-key hook. Inside an inner level it closes one step:
    // an open channel/DM (mark read, list returns, message scroll resets, composer kept), the Nodes search
    // bar, an RPTR login/status page. S514/EST-454 extends the SAME answer: when there is no inner level
    // (the page is already at its top), the key moves the selection to the first row instead of doing
    // nothing. The arithmetic is core/own_top.h, host-tested; this lambda only copies the state in and out
    // and performs the close side effects.
    // A captureless lambda because Module stores a plain function pointer; the state is `static` above.
    // `page` is why the guard is first: OM Lite owns five pages, and one page's level must not close another's.
    g_om_lite.onTop = +[](tm::Core&, tm::Page page) -> bool {
        if (page == tm::Page::Nodes) {
            tm::ListTop top;
            top.inner = nodes_bar;
            top.row   = nodes_focus;
            const bool closed = tm::ownTopList(top);
            nodes_bar   = top.inner;
            nodes_focus = top.row;   // first node at the top; unchanged when the bar was the one closed
            // S515/EST-461: the own key's RETURN TO THE TOP is a key press landing on row 0, so it acks that
            // row like Up/Down do. Closing the search bar is not a move and acks nothing.
            if (!closed) nodes_key_moved = true;
            return closed;           // the typed query is deliberately untouched
        }
        if (page == tm::Page::Chat) {
            tm::CardListTop top;
            top.inner = chat_open;
            top.col   = chat_col;
            top.row   = chat_focus;
            for (int i = 0; i < om::mc::kCardCols; ++i) top.scroll[i] = chat_card_scroll[i];
            const bool closed = tm::ownTopCardList(top);
            chat_open  = top.inner;
            chat_col   = top.col;
            chat_focus = top.row;
            for (int i = 0; i < om::mc::kCardCols; ++i) chat_card_scroll[i] = top.scroll[i];
            if (closed) {
                if (chat_dm_open) {
                    om::mc::noteDmRead(chat_dm_pub);
                    chat_dm_open = false;
                } else {
                    om::mc::noteChannelRead((uint8_t)chat_focus);
                }
                chat_scroll     = 0;
                chat_seen_count = -1;
            }
            return closed;
        }
        if (page == tm::Page::Rptr) {
            // S514/EST-454: RPTR has a list too, so it gets the same rule: first row at the top; inside
            // login/status one press returns to the list and does not also jump.
            tm::ListTop top;
            top.inner = g_rptr_mode != 0;
            top.row   = g_rptr_focus;
            const bool closed = tm::ownTopList(top);
            g_rptr_focus = top.row;
            if (closed) {
                g_rptr_mode  = 0;
                g_rptr_pw[0] = '\0';
                g_rptr_pw_len = 0;
                om::rptrlink::logout();
            }
            return closed;
        }
        return false;   // Map has no list; Settings is not reached by a symbol key, so neither has a row 0
    };

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

    // EST-445/EST-448: the one way into Settings. The tab and SECTION come from the registry
    // (core/settings.h::settingsTargetForPage): an OM Lite page lands on the OM Lite tab at that page's
    // section (Nodes->NODES, Chat->CHAT, Map->MAP, RPTR->RPTR), the menu and the Settings bar land on
    // General. `from` is remembered because Esc at the root returns THERE, not always to the Launcher.
    auto openSettings = [&](tm::Page from) {
        settings_return_page = from;
        refreshSettingsTabs();
        const tm::SettingsTarget target = tm::settingsTargetForPage(core.registry(), from);
        settings_frame.reset(target.tab, settings_tab_count);
        settings_frame.setBodyHeight(om::ui::kSetBodyH);
        settings_draft[0] = '\0';
        settings_draft_len = 0;
        // A previous visit may have ended on a list sub-page; the frame's depth is reset above, and these
        // flags must not outlive it (S514/EST-447: the NODES list has its own flag, like the CHAT pages).
        g_nodes_list = false;
        g_chat_sub   = ChatSub::None;
        g_chat_field = ChatField::None;
        g_chat_channel = -1;
        if (!cards[0].running) {
            const tm::StartResult r = core.start("om-lite");
            ESP_LOGI(TAG, "settings: start om-lite: code=%d busy=%s", (int)r.code, tm::resource_name(r.busy));
            if ((bool)r) {
                cards[0].running = true;
                om_started_us    = esp_timer_get_time();
                refreshOmNumbers();
            }
        }
        if (cards[0].running) {
            // Land on the requested section (or the tab's first row) before the first frame is drawn.
            int ns = 0;
            const tm::SettingsSection* secs = settingsSectionsForTab(settings_frame.tab, &ns);
            if (target.section >= 0)
                settings_frame.focusSection(secs, ns, target.section);
            else
                settings_frame.focusFirst(secs, ns);
            core.goPage(tm::Page::Settings);
            ESP_LOGI(TAG, "settings: opened on %s%s (from %s)",
                     tm::settingsTabLabel(settings_tabs, settings_tab_count, settings_frame.tab),
                     target.section >= 0 ? " at its page's section" : "", tm::page_name(from));
        } else {
            om::ui::noticeSet("OM Lite is not running", 3000);
        }
        dirty = true;
    };
    auto drawRptrPage = [&]() {
        om::ui::RptrView rv;
        rv.clock       = clockText();
        rv.battery_pct = view.battery_pct;
        rv.charging    = view.charging;
        rv.mode        = (om::ui::RptrMode)g_rptr_mode;
        // ONE copy under the link lock: the radio task writes these fields as answers arrive.
        static om::rptr::FoundList found_copy;
        static om::rptr::LoginMachine login_copy;
        static om::rptr::Status status_copy;
        static char target_copy[48];
        bool pending = false;
        uint32_t serial = 0;
        om::rptrlink::lock();
        found_copy = om::rptrlink::found();
        login_copy = om::rptrlink::login();
        status_copy = om::rptrlink::status();
        pending = om::rptrlink::statusPending();
        serial = om::rptrlink::statusSerial();
        snprintf(target_copy, sizeof(target_copy), "%s", om::rptrlink::target());
        om::rptrlink::unlock();
        (void)serial;
        rv.scanning     = found_copy.listening;
        rv.found        = &found_copy;
        rv.focus        = g_rptr_focus;
        rv.target       = target_copy;
        rv.login_state  = login_copy.state;
        rv.password_len = g_rptr_pw_len;
        rv.password_dots = "";
        rv.remember     = om::rptrlink::rememberEnabled();
        rv.status       = &status_copy;
        rv.status_pending = pending;
        rv.status_age_s = status_copy.valid && g_rptr_status_ms != 0
                              ? (uint32_t)((esp_timer_get_time() / 1000 - g_rptr_status_ms) / 1000)
                              : 0;
        if (g_rptr_mode == 1) {
            if (g_rptr_note[0] != '\0')
                rv.note = g_rptr_note;
            else if (login_copy.state == om::rptr::LoginState::Sending)
                rv.note = "waiting for the answer…";
            else if (login_copy.state == om::rptr::LoginState::NoAnswer)
                rv.note = "no answer (wrong password, or out of range)";
            else if (login_copy.state == om::rptr::LoginState::Refused)
                rv.note = "refused by the repeater";
        }
        om::ui::drawRptr(g_screen, rv);
    };
    for (;;) {
        bsp_input_event_t ev = {};
        // While the map is filling in, wake up often so a decoded tile reaches the panel within a frame instead of
        // waiting for the next 250 ms poll (S506). With nothing being fetched this is the old 250 ms.
        const TickType_t wait = om::maplink::busy() ? pdMS_TO_TICKS(30) : pdMS_TO_TICKS(250);
        if (g_input_queue && xQueueReceive(g_input_queue, &ev, wait) == pdTRUE) {
            logInputEvent(ev);
            // ---- S515/EST-461: the power button and the screen-off state machine (core/screen_power.h).
            // Press toggles; the release edge is never acted on; while OFF every KEY is SWALLOWED and the screen
            // stays dark (Filip, S515: the power button is also a key-lock for transport, only it wakes the
            // screen). An arriving message never comes through here at all, so it cannot wake the panel.
            if (ev.type == INPUT_EVENT_TYPE_ACTION) {
                if (ev.args_action.type == BSP_INPUT_ACTION_TYPE_POWER_BUTTON) {
                    const om::ui::ScreenAct act = g_screen_power.onEvent(
                        ev.args_action.state ? om::ui::ScreenEv::PowerPress : om::ui::ScreenEv::PowerRelease);
                    if (act == om::ui::ScreenAct::Off) {
                        screenLightsOff();
                        ESP_LOGI(TAG, "[screen] off");
                    } else if (act == om::ui::ScreenAct::OnByPower) {
                        screenLightsOn();
                        ESP_LOGI(TAG, "[screen] on (by power)");
                        dirty = true;   // one repaint of the page the user left
                    }
                }
                continue;   // no other action event is a key (audio jack, PMIC faults)
            }
            if (g_screen_power.off &&
                (ev.type == INPUT_EVENT_TYPE_NAVIGATION || ev.type == INPUT_EVENT_TYPE_KEYBOARD ||
                 ev.type == INPUT_EVENT_TYPE_SCANCODE)) {
                (void)g_screen_power.onEvent(om::ui::ScreenEv::Key);   // Swallow: locked, stays dark
                continue;   // a key while locked does NOTHING
            }
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
                            // S513/EST-441: a DM to a thread restored from the card has no KEY yet. That is a
                            // different refusal from "radio busy", so it is asked first and named.
                            bool ready = true;
                            if (chat_dm_open) {
                                char peer_name[24] = {0};
                                ready = om::mc::dmPeerReady(chat_dm_pub, peer_name, sizeof(peer_name));
                                if (!ready) {
                                    snprintf(chat_notice_buf, sizeof(chat_notice_buf),
                                             "No key for %s yet: waiting for its advert",
                                             peer_name[0] != '\0' ? peer_name : "this contact");
                                }
                            }
                            const bool queued =
                                !ready ? false
                                       : (chat_dm_open ? om::mc::queueDm(chat_dm_pub, composer)
                                                       : om::mc::queueText((size_t)chat_focus, composer));
                            if (queued) {
                                ESP_LOGI(TAG, "chat: queued %s (%u chars)", chat_dm_open ? "a DM" : "a channel text",
                                         (unsigned)composer_len);
                                // S514/EST-450: the L2 plays Sent when the send path ACCEPTS the message.
                                // Here that acceptance is the radio queue taking it (a real send failure
                                // is reported by the radio task, which plays Error instead).
                                om::audioout::playEvent(tm::soundcfg::Event::Sent);
                                composer[0]           = '\0';
                                composer_len          = 0;
                                chat_notice           = nullptr;   // a send that worked clears the last refusal
                                chat_notice_until_us  = 0;
                            } else {
                                // S513: the queue is FULL (four requests waiting), or the DM peer has no key yet
                                // (EST-441). Nothing is replaced, so the text stays in the composer and the page
                                // says why for three seconds.
                                // S514/EST-450: a send that failed is the error sound's first case.
                                om::audioout::playEvent(tm::soundcfg::Event::Error);
                                chat_notice          = ready ? "radio busy, not sent" : chat_notice_buf;
                                chat_notice_until_us = esp_timer_get_time() + 3 * 1000 * 1000;
                                ESP_LOGW(TAG, "chat: not sent (%s); the text is kept in the composer",
                                         ready ? "queue full" : "no key for the peer yet");
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
                } else if (page_now == tm::Page::Rptr) {
                    // S514/EST-453: RPTR part 1. The page has three levels (list -> login -> status); the link owns
                    // the radio and NVS, this branch only feeds it keys and moves the cursor.
                    if (tm::is_symbol(k)) {
                        handled = false;   // the core's page switch wins
                    } else {
                        handled = true;
                        const bool is_char = (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD);
                        const char ch      = is_char ? (char)ev.args_keyboard.ascii : '\0';
                        if (g_rptr_mode == 0) {
                            int n = 0;
                            {
                                om::rptrlink::lock();
                                n = om::rptrlink::found().count;
                                om::rptrlink::unlock();
                            }
                            if (k == tm::Key::Up) {
                                if (g_rptr_focus > 0) --g_rptr_focus;
                                dirty = true;
                            } else if (k == tm::Key::Down) {
                                if (n > 0 && g_rptr_focus + 1 < n) ++g_rptr_focus;
                                dirty = true;
                            } else if (is_char && ch == 's') {
                                if (om::rptrlink::scanStart()) {
                                    g_rptr_focus = 0;
                                    om::ui::noticeSet("Scanning for repeaters", 2000);
                                } else {
                                    om::ui::noticeSet("Scan not sent (radio busy)", 3000);
                                }
                                dirty = true;
                            } else if (k == tm::Key::Enter && n > 0) {
                                char name[40] = "";
                                {
                                    om::rptrlink::lock();
                                    if (g_rptr_focus >= 0 && g_rptr_focus < om::rptrlink::found().count) {
                                        const om::rptr::Found& f = om::rptrlink::found().rows[g_rptr_focus];
                                        if (f.name[0] != '\0')
                                            snprintf(name, sizeof(name), "%s", f.name);
                                        else
                                            snprintf(name, sizeof(name), "%02x%02x%02x", f.prefix[0],
                                                     f.prefix[1], f.prefix[2]);
                                    }
                                    om::rptrlink::unlock();
                                }
                                // The L2 remembers the password per repeater: a known one is pre-filled and the
                                // page only shows its dots. passwordFor() is the one reader and it never logs.
                                g_rptr_pw[0] = '\0';
                                g_rptr_pw_len = 0;
                                if (name[0] != '\0' && om::rptrlink::hasRemembered(name)) {
                                    char pw[om::rptr::kPasswordMax + 1];
                                    if (om::rptrlink::passwordFor(name, pw, sizeof(pw)))
                                        g_rptr_pw_len = (size_t)om::rptr::passwordFilter(pw, g_rptr_pw, sizeof(g_rptr_pw));
                                }
                                g_rptr_mode = 1;
                                g_rptr_note[0] = '\0';
                                dirty = true;
                            } else if (k == tm::Key::Esc) {
                                handled = false;   // the normal OM Lite back path
                            }
                        } else if (g_rptr_mode == 1) {
                            if (k == tm::Key::Esc) {
                                g_rptr_mode = 0;
                                g_rptr_pw[0] = '\0';
                                g_rptr_pw_len = 0;
                                om::rptrlink::logout();
                                dirty = true;
                            } else if (k == tm::Key::Backspace) {
                                if (g_rptr_pw_len > 0) g_rptr_pw[--g_rptr_pw_len] = '\0';
                                dirty = true;
                            } else if (k == tm::Key::Enter) {
                                if (g_rptr_pw_len == 0) {
                                    snprintf(g_rptr_note, sizeof(g_rptr_note), "type the password first");
                                } else if (om::rptrlink::loginStart(g_rptr_focus, g_rptr_pw)) {
                                    snprintf(g_rptr_note, sizeof(g_rptr_note), "sending…");
                                } else {
                                    snprintf(g_rptr_note, sizeof(g_rptr_note), "not sent (no key or radio busy)");
                                }
                                dirty = true;
                            } else if (is_char && om::rptr::passwordChar(ch) &&
                                       g_rptr_pw_len < (size_t)om::rptr::kPasswordMax) {
                                g_rptr_pw[g_rptr_pw_len++] = ch;
                                g_rptr_pw[g_rptr_pw_len]   = '\0';
                                dirty = true;
                            }
                        } else {
                            if (k == tm::Key::Esc) {
                                g_rptr_mode = 0;
                                g_rptr_pw[0] = '\0';
                                g_rptr_pw_len = 0;
                                om::rptrlink::logout();
                                dirty = true;
                            } else if (is_char && ch == 'r') {
                                if (om::rptrlink::statusRequest()) {
                                    g_rptr_status_ms = (uint32_t)(esp_timer_get_time() / 1000);
                                } else {
                                    om::ui::noticeSet("Status request not sent", 3000);
                                }
                                dirty = true;
                            }
                        }
                    }
                } else if (page_now == tm::Page::Settings) {
                    // EST-445: the frame + General. core/settings.h decides; this branch only feeds it the
                    // keys and performs the row actions against the real stores.
                    if (tm::is_symbol(k)) {
                        handled = false;   // the core's page route still wins on a symbol key
                    } else {
                        handled = true;
                        const bool is_char = (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD);
                        const char ch      = is_char ? (char)ev.args_keyboard.ascii : '\0';

                        // S514/EST-451: '+' (and '=' where '+' needs Shift) raises the Volume row by ten,
                        // '-' lowers it. The store and the audio are the shell's, so the key is decided here,
                        // before the frame; it is swallowed even at a clamp. Enter keeps the frame's cycle.
                        bool volume_key = false;
                        if (is_char && settings_frame.tab == tm::kSettingsGeneralTab &&
                            !settings_frame.confirmOpen() && !settings_frame.subPage() &&
                            (ch == '+' || ch == '=' || ch == '-')) {
                            int vn = 0;
                            const tm::SettingsSection* vs = settingsSectionsForTab(settings_frame.tab, &vn);
                            const tm::SettingRow* vr = tm::settingsRowAt(vs, vn, settings_frame.focus);
                            if (vr != nullptr && vr->id == kSetRowSoundVolume) {
                                adjustSoundVolume(ch == '-' ? -10 : 10);
                                volume_key = true;
                                dirty      = true;
                            }
                        }

                        if (volume_key) {
                            // Handled above: the focused row did not move.
                        } else if (settings_frame.confirmOpen()) {
                            // The shared sheet owns every key: Enter = Apply, Esc = Cancel, everything else is
                            // swallowed so a held arrow cannot move the page behind the question.
                            const tm::SetKey sk = (k == tm::Key::Enter) ? tm::SetKey::Enter
                                                   : (k == tm::Key::Esc) ? tm::SetKey::Esc
                                                                         : tm::SetKey::None;
                            const tm::SettingsKeyResult r =
                                tm::settingsKey(settings_frame, sk, nullptr, 0);
                            if (r.event == tm::SettingsEvent::ConfirmApplied) {
                                // S514/EST-446: the ONE sheet serves the name, the channel removal and the
                                // key display. The target was set where the sheet was raised; Cancel never
                                // reaches this branch, so it changes nothing on any of the three paths.
                                if (g_confirm_target == ConfirmTarget::RemoveChannel) {
                                    if (g_confirm_channel >= 0 &&
                                        om::mc::channelsRemove((size_t)g_confirm_channel)) {
                                        om::ui::noticeSet("Channel removed; its history stays", 3000);
                                    } else {
                                        om::ui::noticeSet("Channel not removed", 3000);
                                    }
                                    closeChatPage();
                                } else if (g_confirm_target == ConfirmTarget::ShowKey) {
                                    uint8_t key[meshcore::kKeyBytes];
                                    if (g_confirm_channel >= 0 &&
                                        om::mc::channelsKeyCopy((size_t)g_confirm_channel, key) &&
                                        meshcore::channelKeyToBase64(key, g_chat_show_key,
                                                                     sizeof(g_chat_show_key))) {
                                        g_chat_sub   = ChatSub::KeyShown;
                                        g_chat_field = ChatField::None;
                                        openChatList(ChatSub::KeyShown);
                                        ESP_LOGI(TAG, "settings: channel key shown on screen");
                                    } else {
                                        om::ui::noticeSet("Key not available", 3000);
                                    }
                                } else if (g_confirm_target == ConfirmTarget::RemoveContact) {
                                    // S514/EST-447: remove ONE contact from RAM; contactsPersistTick() below
                                    // rewrites the mcct blob on this pass. Its DM logs are not touched.
                                    if (g_confirm_contact >= 0 &&
                                        om::mc::contactsRemove((size_t)g_confirm_contact)) {
                                        om::ui::noticeSet("Contact removed; its DM history stays", 3000);
                                    } else {
                                        om::ui::noticeSet("Contact not removed", 3000);
                                    }
                                    closeNodesPage();
                                } else if (g_confirm_target == ConfirmTarget::ForgetRptr) {
                                    om::rptrlink::forgetAllPasswords();
                                    om::ui::noticeSet("Remembered passwords forgotten", 3000);
                                } else if (g_confirm_target == ConfirmTarget::ClearContacts) {
                                    const size_t removed = om::mc::contactsClear();
                                    if (removed > 0)
                                        om::ui::noticeSet("All contacts removed; their DM history stays", 3000);
                                    else
                                        om::ui::noticeSet("No contacts to clear", 3000);
                                    // This sheet was raised at the ROOT (the Clear row), so there is no
                                    // sub-page to close; closing one would restore a stale root focus.
                                    if (settings_frame.subPage()) closeNodesPage();
                                } else if (om::mc::setNodeName(settings_pending_name)) {
                                    om::ui::noticeSet("Node name saved", 3000);
                                    ESP_LOGI(TAG, "settings: node name saved");
                                } else {
                                    om::ui::noticeSet("Name not saved", 3000);
                                }
                            } else if (r.event == tm::SettingsEvent::ConfirmCancelled) {
                                ESP_LOGI(TAG, "settings: change cancelled; the old value stays");
                            }
                            dirty = true;
                        } else if (settings_frame.subPage() && g_chat_field != ChatField::None) {
                            // A chat TEXT page: the shared field, with THAT page's character rules. Esc goes
                            // back to the list it was opened from, or closes the page when there is none.
                            if (k == tm::Key::Esc) {
                                if (g_chat_sub != ChatSub::None)
                                    g_chat_field = ChatField::None;
                                else
                                    closeChatPage();
                                dirty = true;
                            } else if (k == tm::Key::Backspace) {
                                while (settings_draft_len > 0) {
                                    const unsigned char c =
                                        (unsigned char)settings_draft[--settings_draft_len];
                                    if ((c & 0xC0) != 0x80) break;
                                }
                                settings_draft[settings_draft_len] = '\0';
                                dirty = true;
                            } else if (k == tm::Key::Enter) {
                                applyChatField();
                                dirty = true;
                            } else if (is_char && fieldAllows(g_chat_field, ch)) {
                                if (settings_draft_len + 1 < sizeof(settings_draft) &&
                                    settings_draft_len < fieldMax(g_chat_field)) {
                                    settings_draft[settings_draft_len++] = ch;
                                    settings_draft[settings_draft_len]   = '\0';
                                }
                                dirty = true;
                            }
                        } else if (settings_frame.subPage() && g_sound_sub) {
                            // S514/EST-450: the event's picker. Up/Down/Enter/Esc use the SAME list
                            // arithmetic as every other sub-page; the choice is the part only this page
                            // has: the first Enter PLAYS the focused sound, the second Enter on that same
                            // row chooses it. Off is chosen on the first Enter (nothing to preview).
                            tm::SetKey sk = tm::SetKey::None;
                            if (k == tm::Key::Up) sk = tm::SetKey::Up;
                            else if (k == tm::Key::Down) sk = tm::SetKey::Down;
                            else if (k == tm::Key::Enter) sk = tm::SetKey::Enter;
                            else if (k == tm::Key::Esc) sk = tm::SetKey::Esc;
                            else if (is_char && ch == ',') sk = tm::SetKey::Settings;

                            int sn = 0;
                            const tm::SettingsSection* ss = soundSubSections(&sn);
                            const tm::SettingsKeyResult r =
                                tm::settingsSubPageKey(settings_frame, sk, ss, sn, &g_sound_sub_focus,
                                                       &g_sound_sub_scroll,
                                                       om::ui::kSetSubBodyH);
                            const tm::soundcfg::Event ev =
                                (tm::soundcfg::Event)g_sound_event;
                            if (r.event == tm::SettingsEvent::CloseSubPage) {
                                g_sound_sub        = false;
                                g_sound_preview_id = kNoSoundPreview;
                                g_sound_preview_row = -1;
                            } else if (r.event == tm::SettingsEvent::FocusMoved) {
                                // A preview belongs to the row it was played on. A clamped Up/Down at
                                // the end reports the same row, so that case keeps the pending choice.
                                if (r.row != g_sound_preview_row) {
                                    g_sound_preview_id  = kNoSoundPreview;
                                    g_sound_preview_row = -1;
                                }
                            } else if (r.event == tm::SettingsEvent::ActionRow) {
                                const tm::SettingRow* pick = tm::settingsRowAt(ss, sn, r.row);
                                if (pick != nullptr) {
                                    const uint8_t id =
                                        pick->id == kSoundPickOffId
                                            ? om::sound::kSoundOff
                                            : (uint8_t)pick->id;
                                    const bool second = (id != om::sound::kSoundOff &&
                                                         id == g_sound_preview_id &&
                                                         r.row == g_sound_preview_row);
                                    if (id == om::sound::kSoundOff || second) {
                                        if (!om::soundstore::setChoice(ev, id)) {
                                            // Keep the picker open so the choice can be tried again.
                                            om::ui::noticeSet("Sound not saved", 3000);
                                        } else {
                                            ESP_LOGI(TAG, "settings: %s sound -> %s",
                                                     tm::soundcfg::eventKey(ev),
                                                     tm::soundcfg::choiceName(id));
                                            g_sound_sub         = false;
                                            g_sound_preview_id  = kNoSoundPreview;
                                            g_sound_preview_row = -1;
                                            // The frame is still at depth 1; close it so the draw returns
                                            // to the SOUND section at the same focus/scroll it was opened
                                            // from.
                                            settings_frame.closeSubPage();
                                        }
                                    } else {
                                        g_sound_preview_id  = id;
                                        g_sound_preview_row = r.row;
                                        if (!om::audioout::preview(id)) {
                                            om::ui::noticeSet(
                                                om::soundstore::settings().master
                                                    ? "Sound not available"
                                                    : "Sound is Off",
                                                3000);
                                        }
                                    }
                                }
                            }
                            dirty = true;
                        } else if (settings_frame.subPage() && g_nodes_list) {
                            // S514/EST-447: the Remove-a-contact list. Same row arithmetic as every other list
                            // sub-page; Enter on a contact raises the shared sheet (nodesSubAction), Esc closes.
                            tm::SetKey sk = tm::SetKey::None;
                            if (k == tm::Key::Up) sk = tm::SetKey::Up;
                            else if (k == tm::Key::Down) sk = tm::SetKey::Down;
                            else if (k == tm::Key::Enter) sk = tm::SetKey::Enter;
                            else if (k == tm::Key::Esc) sk = tm::SetKey::Esc;
                            else if (is_char && ch == ',') sk = tm::SetKey::Settings;

                            int sn = 0;
                            const tm::SettingsSection* ss = nodesSubSections(&sn);
                            const tm::SettingsKeyResult r =
                                tm::settingsSubPageKey(settings_frame, sk, ss, sn, &g_nodes_sub_focus,
                                                       &g_nodes_sub_scroll,
                                                       om::ui::kSetSubBodyH);
                            if (r.event == tm::SettingsEvent::CloseSubPage) {
                                closeNodesPage();
                            } else if (r.event == tm::SettingsEvent::ActionRow) {
                                const tm::SettingRow* row = tm::settingsRowAt(ss, sn, r.row);
                                if (row != nullptr) nodesSubAction(row->id);
                            }
                            dirty = true;
                        } else if (settings_frame.subPage() && g_chat_sub != ChatSub::None) {
                            // A chat LIST page (channel / scope / add-by-key): the frame's own row arithmetic
                            // through settingsSubPageKey, and the same event vocabulary as the root.
                            tm::SetKey sk = tm::SetKey::None;
                            if (k == tm::Key::Up) sk = tm::SetKey::Up;
                            else if (k == tm::Key::Down) sk = tm::SetKey::Down;
                            else if (k == tm::Key::Enter) sk = tm::SetKey::Enter;
                            else if (k == tm::Key::Esc) sk = tm::SetKey::Esc;
                            else if (is_char && ch == ',') sk = tm::SetKey::Settings;

                            int sn = 0;
                            const tm::SettingsSection* ss = chatSubSections(&sn);
                            const tm::SettingsKeyResult r =
                                tm::settingsSubPageKey(settings_frame, sk, ss, sn, &g_chat_sub_focus,
                                                       &g_chat_sub_scroll,
                                                       om::ui::kSetSubBodyH);
                            if (r.event == tm::SettingsEvent::CloseSubPage) {
                                closeChatPage();
                            } else if (r.event == tm::SettingsEvent::OpenSubPage ||
                                       r.event == tm::SettingsEvent::ActionRow) {
                                const tm::SettingRow* row = tm::settingsRowAt(ss, sn, r.row);
                                if (row != nullptr) chatSubAction(row->id);
                            }
                            dirty = true;
                        } else if (settings_frame.subPage()) {                        } else {
                            tm::SetKey sk = tm::SetKey::None;
                            if (k == tm::Key::Up) sk = tm::SetKey::Up;
                            else if (k == tm::Key::Down) sk = tm::SetKey::Down;
                            else if (k == tm::Key::Left) sk = tm::SetKey::Left;
                            else if (k == tm::Key::Right) sk = tm::SetKey::Right;
                            else if (k == tm::Key::Enter) sk = tm::SetKey::Enter;
                            else if (k == tm::Key::Esc) sk = tm::SetKey::Esc;
                            else if (is_char && ch == ',') sk = tm::SetKey::Settings;

                            int ns = 0;
                            const tm::SettingsSection* secs = settingsSectionsForTab(settings_frame.tab, &ns);
                            settings_frame.setTabRunning(settingsTabRunning(settings_frame.tab));
                            const tm::SettingsKeyResult r = tm::settingsKey(settings_frame, sk, secs, ns);
                            const tm::SettingRow* row = tm::settingsRowAt(secs, ns, r.row);
                            switch (r.event) {
                                case tm::SettingsEvent::TabChanged: {
                                    // The NEW tab's sections, then its first row and the running state of
                                    // its owner (a LATER app is stopped, so its actions say so).
                                    refreshSettingsTabs();
                                    secs = settingsSectionsForTab(settings_frame.tab, &ns);
                                    settings_frame.setTabRunning(settingsTabRunning(settings_frame.tab));
                                    settings_frame.focusFirst(secs, ns);
                                    break;
                                }
                                case tm::SettingsEvent::OpenSubPage:
                                    if (row != nullptr && row->id == kNodesRowRemove) {
                                        // S514/EST-447: the Remove-a-contact list sub-page.
                                        openNodesPage();
                                    } else if (row != nullptr && row->id >= kChatRowChannelBase) {
                                        g_chat_channel = row->id - kChatRowChannelBase;
                                        fillStoredScope(g_chat_channel);
                                        openChatList(ChatSub::Channel);
                                    } else if (row != nullptr && row->id == kChatRowAddTag) {
                                        g_chat_add_name[0] = '\0';
                                        g_chat_add_key[0]  = '\0';
                                        g_chat_sub         = ChatSub::None;
                                        openChatFieldPage(ChatField::Tag, "", "Add hashtag");
                                    } else if (row != nullptr && row->id == kChatRowAddKey) {
                                        g_chat_add_name[0] = '\0';
                                        g_chat_add_key[0]  = '\0';
                                        openChatList(ChatSub::AddKey);
                                    } else if (row != nullptr && row->id == kSetRowName) {
                                        g_confirm_target  = ConfirmTarget::Name;
                                        g_confirm_channel = -1;
                                        snprintf(g_confirm_title, sizeof(g_confirm_title),
                                                 "Change the node name?");
                                        snprintf(g_confirm_text, sizeof(g_confirm_text),
                                                 "Other nodes will see the new name after the next advert");
                                        snprintf(g_confirm_apply, sizeof(g_confirm_apply), "Apply");
                                        g_confirm_danger  = false;
                                        snprintf(settings_draft, sizeof(settings_draft), "%s",
                                                 om::mc::nodeName());
                                        settings_draft_len = strlen(settings_draft);
                                    } else if (row != nullptr &&
                                               row->id >= kSetRowSoundEventBase &&
                                               row->id <
                                                   kSetRowSoundEventBase + tm::soundcfg::kEventCount) {
                                        // S514/EST-450: open this event's picker. The frame already
                                        // opened the sub-page; this fills its own focus/scroll and clears
                                        // any left-over preview from the previous event.
                                        g_sound_event      = (int)row->id - (int)kSetRowSoundEventBase;
                                        g_sound_sub        = true;
                                        g_nodes_list       = false;
                                        g_chat_sub         = ChatSub::None;
                                        g_chat_field       = ChatField::None;
                                        g_sound_preview_id = kNoSoundPreview;
                                        g_sound_preview_row = -1;
                                        int sn = 0;
                                        const tm::SettingsSection* ss = soundSubSections(&sn);
                                        g_sound_sub_focus  = tm::settingsFirstFocusable(ss, sn);
                                        g_sound_sub_scroll = 0;
                                    }
                                    break;
                                case tm::SettingsEvent::ToggleValue:
                                case tm::SettingsEvent::CycleValue:
                                case tm::SettingsEvent::ActionRow:
                                    // The row carries its own handler for the plain changes (a toggle, a cycle).
                                    // Two rows need the SHEET or the map's own state, which live in this scope:
                                    // Clear all contacts raises the shared confirm, and a MAP start row
                                    // re-places the map so the next Map draw uses the new choice.
                                    if (row != nullptr && row->id == kRptrRowForget) {
                                        // S514/EST-453: the shared confirm sheet; Cancel changes nothing.
                                        g_confirm_target = ConfirmTarget::ForgetRptr;
                                        snprintf(g_confirm_title, sizeof(g_confirm_title),
                                                 "Forget all passwords");
                                        snprintf(g_confirm_text, sizeof(g_confirm_text),
                                                 "Forget every remembered repeater password? The repeaters "
                                                 "themselves are not changed.");
                                        snprintf(g_confirm_apply, sizeof(g_confirm_apply), "Forget all");
                                        g_confirm_danger = true;
                                        settings_frame.beginConfirm(r.row);
                                    } else if (row != nullptr && row->id == kNodesRowClear) {
                                        raiseClearContacts(r.row);
                                    } else {
                                        if (row != nullptr && row->on_enter != nullptr)
                                            row->on_enter(core, *row);
                                        if (row != nullptr &&
                                            (row->id == kMapRowStartPos || row->id == kMapRowStartZoom)) {
                                            map_z            = (int)om::maplink::mapStartZoom();
                                            map_wx           = -1;
                                            map_wy           = -1;
                                            map_view_changed = false;
                                        }
                                    }
                                    break;
                                case tm::SettingsEvent::NeedsRunning: {
                                    char msg[48];
                                    snprintf(msg, sizeof(msg), "%s is not running",
                                             tm::settingsTabLabel(settings_tabs, settings_tab_count,
                                                                  settings_frame.tab));
                                    om::ui::noticeSet(msg, 3000);
                                    break;
                                }
                                case tm::SettingsEvent::LeaveSettings: {
                                    // Esc at the root leaves Settings for the page it was opened from (the
                                    // Launcher if it was opened from the menu).
                                    const tm::Page back = settings_return_page;
                                    if (back == tm::Page::Settings || back == tm::Page::Count)
                                        core.goPage(tm::Page::Launcher);
                                    else
                                        core.goPage(back);
                                    break;
                                }
                                default:
                                    break;
                            }
                            dirty = true;
                        }
                    }
                } else if (page_now == tm::Page::Map) {
                    // Pan by a quarter of a tile per press: fine enough to place a town, coarse enough to cross the
                    // country without holding the key. Zoom keeps the centre (core/map_view::zoomTo).
                    const int step = om::map::kTilePx / 4;
                    if (k == tm::Key::Up || k == tm::Key::Down || k == tm::Key::Left || k == tm::Key::Right) {
                        const int dx = (k == tm::Key::Left) ? -step : (k == tm::Key::Right) ? step : 0;
                        const int dy = (k == tm::Key::Up) ? -step : (k == tm::Key::Down) ? step : 0;
                        om::map::panBy(map_z, &map_wx, &map_wy, dx, dy, om::ui::kMapAreaW, om::ui::kMapAreaH);
                        map_view_changed = true;   // the once-a-minute / on-leaving rule decides the write
                        handled = true;
                        dirty   = true;
                    } else if (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD &&
                               (ev.args_keyboard.ascii == '+' || ev.args_keyboard.ascii == '=' ||
                                ev.args_keyboard.ascii == '-')) {
                        const int want = (ev.args_keyboard.ascii == '-') ? map_z - 1 : map_z + 1;
                        map_z = om::map::zoomTo(map_z, want, &map_wx, &map_wy, om::ui::kMapAreaW,
                                                om::ui::kMapAreaH);
                        map_view_changed = true;
                        handled = true;
                        dirty   = true;
                    }
                } else if (page_now == tm::Page::Nodes) {
                    // S514/EST-449: the search bar is the list's top row and owns the keys while it has the
                    // focus. A symbol key is a NAVIGATION event, never a Char, so the page switch still works
                    // from either half.
                    const bool is_char = (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD);
                    const char ch      = is_char ? (char)ev.args_keyboard.ascii : '\0';

                    if (tm::is_symbol(k)) {
                        handled = false;   // the core's page switch wins
                    } else if (node_trace_shown && k == tm::Key::Esc) {
                        // S514/EST-456: the trace result is a sheet of its own; Esc closes it before anything
                        // else on the page sees the key.
                        node_trace_shown = false;
                        handled          = true;
                        dirty            = true;
                    } else if (settings_frame.confirmOpen() &&
                               g_confirm_target == ConfirmTarget::RemoveContact) {
                        // S514/EST-455: Remove's EST-447 question, raised on THIS page. The shared sheet owns
                        // every key: Enter = Apply, Esc = Cancel, everything else is swallowed so a held arrow
                        // cannot move the list behind the question.
                        handled = true;
                        dirty   = true;
                        const tm::SetKey sk = (k == tm::Key::Enter) ? tm::SetKey::Enter
                                               : (k == tm::Key::Esc) ? tm::SetKey::Esc
                                                                     : tm::SetKey::None;
                        const tm::SettingsKeyResult r = tm::settingsKey(settings_frame, sk, nullptr, 0);
                        if (r.event == tm::SettingsEvent::ConfirmApplied) {
                            if (g_confirm_target == ConfirmTarget::RemoveContact) {
                                if (g_confirm_contact >= 0 &&
                                    om::mc::contactsRemove((size_t)g_confirm_contact)) {
                                    om::ui::noticeSet("Contact removed; its DM history stays", 3000);
                                } else {
                                    om::ui::noticeSet("Contact not removed", 3000);
                                }
                                g_confirm_contact = -1;
                                closeNodesPage();   // also closes the menu; a cancel below leaves it open
                            }
                        } else if (r.event == tm::SettingsEvent::ConfirmCancelled) {
                            ESP_LOGI(TAG, "nodes: remove cancelled; nothing changed");
                        }
                    } else if (node_menu_open) {
                        // S514/EST-455: the node menu owns the keys while it is up. The rows come from
                        // core/node_menu.h; every action here works on the CAPTURED store row, never on
                        // whatever happens to be focused after a re-sort.
                        handled = true;
                        dirty   = true;
                        om::ui::NodeMenuRow rows[om::ui::kMaxNodeMenuRows];
                        const int             row_count =
                            nodeMenuRowCount(node_menu_index, rows, om::ui::kMaxNodeMenuRows);
                        if (k == tm::Key::Esc) {
                            // S514/EST-456: Esc stops WAITING first; the packet already queued is not recalled.
                            uint8_t pub[32];
                            if (nodeMenuPub(pub)) {
                                const bool waiting = om::mc::nodePingPendingFor(pub) ||
                                                     om::mc::nodeTracePendingFor(pub);
                                om::mc::nodeOpsCancel();
                                if (waiting) om::ui::noticeSet("Cancelled; the packet stays sent", 2500);
                            }
                            node_menu_open  = false;
                            node_menu_index = -1;
                        } else if (k == tm::Key::Up) {
                            node_menu_focus = om::ui::nodeMenuFocusMove(node_menu_focus, -1, row_count);
                        } else if (k == tm::Key::Down) {
                            node_menu_focus = om::ui::nodeMenuFocusMove(node_menu_focus, 1, row_count);
                        } else if (k == tm::Key::Enter) {
                            switch (om::ui::nodeMenuActionAt(rows, row_count, node_menu_focus)) {
                                case om::ui::NodeAction::DirectMessage:
                                    openDmThread(node_menu_index);
                                    node_menu_open  = false;
                                    node_menu_index = -1;
                                    break;
                                case om::ui::NodeAction::Info:
                                    // The detail pane behind the sheet IS the info view; choosing it closes
                                    // the sheet and leaves the focus on this node.
                                    node_menu_open  = false;
                                    node_menu_index = -1;
                                    break;
                                case om::ui::NodeAction::Ping: {
                                    // S514/EST-456: the sheet STAYS UP so the row can say "waiting..."; the
                                    // notice carries the L2's words and the packet goes through the radio task.
                                    uint8_t pub[32];
                                    if (nodeMenuPub(pub)) {
                                        const bool ok = om::mc::nodePing(pub);
                                        om::ops::PingJob pj;
                                        om::mc::nodePingJob(&pj);
                                        char text[64];
                                        om::ops::pingText(text, sizeof(text), pj);
                                        om::ui::noticeSet(ok ? text : "Ping: send failed", 3000);
                                    }
                                    break;
                                }
                                case om::ui::NodeAction::Trace: {
                                    uint8_t pub[32];
                                    if (nodeMenuPub(pub)) {
                                        const bool ok = om::mc::nodeTrace(pub);
                                        om::ui::noticeSet(ok ? "Trace: waiting..." : "Trace: not sent", 3000);
                                    }
                                    break;
                                }
                                case om::ui::NodeAction::Favourite:
                                    // The list re-sorts at once; the focus follows. The sheet stays up so a
                                    // second press can undo it.
                                    toggleFavourite(node_menu_index);
                                    break;
                                case om::ui::NodeAction::RemoveContact:
                                    // The EST-447 question; a Cancel brings this sheet back unchanged.
                                    raiseRemoveContactConfirm(node_menu_index);
                                    break;
                                case om::ui::NodeAction::None:
                                    break;
                            }
                        }
                    } else if (nodes_bar) {
                        handled = true;
                        dirty   = true;
                        if (k == tm::Key::Esc) {
                            // Esc clears a non-empty query first; a second Esc (empty) returns to the list.
                            om::ui::nodesFocusEsc(&nodes_bar, nodes_query, &nodes_query_len, &nodes_focus);
                        } else if (k == tm::Key::Backspace) {
                            om::ui::nodeSearchBackspace(nodes_query, &nodes_query_len);
                            nodes_focus = 0;   // the list just changed under the bar
                        } else if (k == tm::Key::Down) {
                            // Out of the bar onto the first match: that is a move onto a row (S515/EST-461).
                            if (om::ui::nodesFocusDown(&nodes_bar, &nodes_focus, nodesShownCount()) && !nodes_bar)
                                nodes_key_moved = true;
                        } else if (k == tm::Key::Up) {
                            // The bar IS the top row: Up there does nothing (and must never type).
                        } else if (is_char) {
                            // The BSP hands the whole UTF-8 sequence in utf8[] (Alt_R+e -> "é"); `ascii`
                            // is only the base key, so appending it would lose a non-ASCII letter. The
                            // buffer and Backspace stay the composer's rules (bytes in, whole sequence out).
                            const char* ins   = ev.args_keyboard.utf8;
                            bool        added = false;
                            if (ins[0] != '\0') {
                                for (int i = 0; ins[i] != '\0'; ++i) {
                                    if (om::ui::nodeSearchAppend(nodes_query, sizeof(nodes_query),
                                                                 &nodes_query_len, ins[i]))
                                        added = true;
                                }
                            } else {
                                added = om::ui::nodeSearchAppend(nodes_query, sizeof(nodes_query),
                                                                 &nodes_query_len, ch);
                            }
                            if (added) nodes_focus = 0;   // a new query starts at its first match
                        }
                        ESP_LOGI(TAG, "nodes search: %u byte(s), %d shown", (unsigned)nodes_query_len,
                                 nodesShownCount());
                    } else {
                        // The LIST has the focus. Up/Down move; Up from the FIRST row goes to the bar; `/`
                        // opens it from anywhere; Enter opens this node's menu; `f` toggles favourite without
                        // the menu. Every other key keeps its old meaning (the `!handled` block below:
                        // ',' = Settings, 'a' = advert, Esc = back).
                        if (k == tm::Key::Up) {
                            // S515/EST-461: a move that LANDS on a row acks its NEW dot at once.
                            if (om::ui::nodesFocusUp(&nodes_bar, &nodes_focus) && !nodes_bar) nodes_key_moved = true;
                            handled = true;
                            dirty   = true;
                        } else if (k == tm::Key::Down) {
                            if (om::ui::nodesFocusDown(&nodes_bar, &nodes_focus, nodesShownCount()) && !nodes_bar)
                                nodes_key_moved = true;
                            handled = true;
                            dirty   = true;
                        } else if (k == tm::Key::Enter) {
                            // S514/EST-455: the node menu belongs to the FOCUSED contact, captured by its
                            // STORE index so the sheet cannot act on a row that moved under it.
                            const int ci = nodesFocusedStoreIndex();
                            if (ci >= 0) {
                                node_menu_index = ci;
                                node_menu_focus = 0;
                                node_menu_open  = true;
                            }
                            handled = true;
                            dirty   = true;
                        } else if (is_char && ch == 'f') {
                            // The ticket's shortcut: the same toggle the menu row runs, no sheet.
                            toggleFavourite(nodesFocusedStoreIndex());
                            handled = true;
                            dirty   = true;
                        } else if (is_char && om::ui::nodesSlashOpensSearch(ch)) {
                            nodes_bar = true;
                            handled   = true;
                            dirty     = true;
                            ESP_LOGI(TAG, "nodes search: opened with '/'");
                        }
                    }
                }

                if (!handled) {
                    if (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD &&
                        ev.args_keyboard.ascii == ',') {
                        // EST-445: `,` opens Settings from any OM Lite page, on the tab matching that page.
                        // (An open Chat composer consumed the comma above, so this cannot steal a typed ','.)
                        openSettings(page_now);
                    } else if (tm::is_symbol(k)) {
                        // S514/EST-455: a symbol key navigates away, so a node menu left open on this page
                        // closes with it instead of reappearing on the next visit.
                        if (page_now == tm::Page::Nodes) {
                            node_menu_open  = false;
                            node_menu_index = -1;
                        }
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
                            // Left/Right move between the three cards (clamped, no wrap); Down lands on the
                            // full-width Settings bar under them, Up from it returns to the first card. All of
                            // that is core/menu_cards, host-tested.
                            const tm::MenuMove move = (k == tm::Key::Left)     ? tm::MenuMove::Left
                                                      : (k == tm::Key::Right) ? tm::MenuMove::Right
                                                      : (k == tm::Key::Up)    ? tm::MenuMove::Up
                                                                              : tm::MenuMove::Down;
                            menu_focus = tm::menuFocusMove(menu_focus, move, view.card_count);
                            dirty      = true;
                        } else if (k == tm::Key::Enter) {
                            if (menu_focus == tm::kMenuSettingsFocus) {
                                // EST-445: Enter on the full-width Settings bar opens Settings on General.
                                openSettings(tm::Page::Launcher);
                            } else {
                                const tm::MenuAction act =
                                    tm::menuEnterActionAt(menu_focus, cards[menu_focus].available, view.card_count);
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
                            }
                        } else if (k == tm::Key::Char && ev.type == INPUT_EVENT_TYPE_KEYBOARD &&
                                   ev.args_keyboard.ascii == 's' && menu_focus < view.card_count) {
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
                               ev.type == INPUT_EVENT_TYPE_KEYBOARD && ev.args_keyboard.ascii == 'a' &&
                               om::ui::nodesAdvertAllowed(nodes_bar)) {
                        // S513/EST-432: the advert key is on the Nodes page too (its empty-list text points at
                        // it), so the page that shows who was heard can ask for a fresh advert without leaving.
                        // S514/EST-449: ONLY while the list has focus; in the search bar 'a' is a typed letter
                        // (that branch consumes it, so this is belt-and-braces with the rule in one place).
                        pressAdvert();
                    } else if (k == tm::Key::Esc) {
                        core.handleKey(k);   // back one page through the router's history
                        dirty = true;
                    }
                }
            }

            // S514/EST-447: leaving the Map page is the one write that ignores the once-a-minute floor (the
            // ticket: "written at most once a minute and on leaving"). It is still only written when the view
            // moved, so opening and closing the page without touching it writes nothing.
            if (page_now == tm::Page::Map && core.router().current() != tm::Page::Map) saveMapLastView(true);
        }

        // A tile the worker finished: repaint, so the map fills in from the centre out while the keys stay live.
        if (om::maplink::takeLanded() && core.router().current() == tm::Page::Map) dirty = true;
        if (om::ui::noticeNeedsRepaint()) dirty = true;   // S513 (Claude): a notice set by the radio task, or one that ran out

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

        // S514/EST-447: while the Map page is on screen, a moved view is written at most once a minute. The
        // RULE is core/map_settings.h (time passed in); this only supplies the monotonic clock and the view.
        if (core.router().current() == tm::Page::Map) saveMapLastView(false);

        // S513/EST-437: the contact table's one NVS write, on the MAIN task. The radio task only raised a flag;
        // a structural change goes on this pass, last-heard/SNR waits for the blob's 10-minute floor. Cheap when
        // nothing is dirty, which is almost always.
        om::mc::contactsPersistTick();

        // S513/EST-439: the DM retry rule, on the MAIN task — it composes the next attempt and QUEUES it, and
        // marks "no confirmation" when the attempts are spent. True = a message's state moved, so repaint.
        if (om::mc::dmTick()) dirty = true;
        // S514/EST-456: a ping's reply/timeout and a trace's result arrive from the radio task. The trace result
        // closes the menu (its own sheet takes over); a ping only raises its notice.
        if (om::mc::nodeOpsPoll()) {
            om::ops::TraceView tv;
            if (om::mc::nodeTraceResult(&tv)) {
                node_trace_shown = true;
                node_menu_open   = false;
                node_menu_index  = -1;
            } else {
                node_trace_shown = false;
                om::ui::noticeSet("Trace: no data back", 3000);
            }
            dirty = true;
        }
        // S514/EST-453: the scan window and the login/status deadlines. Also a repaint when a login answer
        // moved the state, so the page cannot sit on "sending" after it was answered.
        {
            const om::rptr::LoginState before = om::rptrlink::login().state;
            om::rptrlink::tick();
            if (om::rptrlink::login().state != before) {
                dirty = true;
                // S514 (Claude; Filip: "stuck on sending"): the "sending…" note set when the login went out was
                // only cleared on success, and the note wins over the state text, so "no answer" and "refused"
                // could never show. Any state change ends the note.
                g_rptr_note[0] = '\0';
            }
            if (g_rptr_mode == 1 && om::rptrlink::login().loggedIn()) {
                // Logged in: the STATUS view opens and asks for the repeater's own counters once.
                g_rptr_mode = 2;
                g_rptr_note[0] = '\0';
                om::rptrlink::statusRequest();
                g_rptr_status_ms = (uint32_t)(esp_timer_get_time() / 1000);
                dirty = true;
            }
            if (g_rptr_mode == 2) {
                static uint32_t last_status_serial = 0;
                const uint32_t ss = om::rptrlink::statusSerial();
                if (ss != last_status_serial) {
                    last_status_serial = ss;
                    g_rptr_status_ms = (uint32_t)(esp_timer_get_time() / 1000);
                    dirty = true;
                }
            }
            static uint32_t last_scan_serial = 0;
            if (om::rptrlink::found().serial != last_scan_serial) {
                last_scan_serial = om::rptrlink::found().serial;
                dirty = true;
            }
        }

        // ---- S515/EST-461: the hint bar's dots, then the Nodes page's frozen order and the NEW-contact ack.
        // The counts come from the SAME functions the chat list's badges use (unreadTotals sums
        // unreadForChannel + dmUnreadForPeer), so a dot and its badge cannot disagree.
        {
            uint8_t mask = 0;
            {
                om::mc::StoreLock l;
                uint32_t          unread_ch = 0, unread_dm = 0;
                om::mc::unreadTotals(om::mc::messages(), om::mc::readState(), om::mc::channelCount(),
                                     om::mc::dmThreads(), om::mc::contacts(), &unread_ch, &unread_dm);
                mask = om::ui::hintDotMask(unread_ch, unread_dm, om::mc::contactsFreshCount(om::mc::contacts()));
            }
            om::ui::hintDotsSet(mask);
            // A message arriving while the Map (or any page) is on screen raises this with no key press behind
            // it, so the dot appears at the next 250 ms pass.
            if (om::ui::hintDotsNeedsRepaint()) dirty = true;
        }
        {
            const tm::Page pnow   = core.router().current();
            const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (pnow != nodes_page_prev) {
                // Leaving: the one ack write that does not wait for the 5 s floor.
                if (nodes_page_prev == tm::Page::Nodes) om::mc::contactsAckLeave();
                if (pnow == tm::Page::Nodes) {
                    // Entering: RE-SORT (a fresh snapshot of the live order) and start the dwell. Entry alone
                    // acks nothing — the top row is acked after 1.5 s of rest, or by the first key press.
                    int live[om::mc::kMaxContacts];
                    int n;
                    {
                        om::mc::StoreLock l;
                        n = (int)om::mc::contactsOrder(om::mc::contacts(), live, om::mc::kMaxContacts);
                    }
                    om::ui::nodesFrozenBegin(nodes_frozen, live, n);
                    om::ui::nodesAckEnter(nodes_ack, nodes_focus, now_ms);
                }
                nodes_page_prev = pnow;
            }
            if (pnow == tm::Page::Nodes) {
                if (nodes_key_moved) {
                    nodes_key_moved = false;
                    if (om::ui::nodesAckKeyMoved(nodes_ack, nodes_focus, now_ms) >= 0) {
                        const int si = nodesFocusedStoreIndex();
                        if (si >= 0 && om::mc::contactsAckFresh((size_t)si)) dirty = true;   // the dot goes at once
                    }
                } else {
                    const int row = om::ui::nodesAckTick(nodes_ack, now_ms);
                    if (row >= 0) {
                        int       shown[om::mc::kMaxContacts];
                        const int n  = nodesShownOrder(shown);
                        const int si = (row >= 0 && row < n) ? shown[row] : -1;
                        if (si >= 0 && om::mc::contactsAckFresh((size_t)si)) dirty = true;
                    }
                }
            }
        }

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
                case tm::Page::Rptr:
                    drawRptrPage();
                    break;
                case tm::Page::Settings:
                    drawSettingsPage();
                    break;
                default:
                    drawPlaceholder(core.router().current());
                    break;
            }
            // S515/EST-461: while the screen is OFF the frame is still COMPOSED (the state stays current), but
            // it is not pushed to the panel. The backlights are at 0, so a blit would be invisible work.
            if (!g_screen_power.off) {
                const int64_t present_t0 = esp_timer_get_time();
                g_screen.present();
                const long present_us = (long)(esp_timer_get_time() - present_t0);
                // On the Map page the panel blit is a big share of a frame (it transposes 384k pixels on the CPU), so
                // it is measured, not assumed: a fill-in only looks smooth if compose + present stays short.
                if (core.router().current() == tm::Page::Map) {
                    ESP_LOGI(TAG, "map frame: present %ld ms", present_us / 1000);
                }
            }
            dirty = false;
        }
    }
}
