idf_build_get_property(idf_target IDF_TARGET)
if("${idf_target}" STREQUAL "esp32p4")
list(APPEND extra_requires esp-hosted-tanmatsu)
endif()

# Our app. Project shape and the `esp-hosted-tanmatsu` requirement are the vendor template's
# (tanmatsu-template/main/CMakeLists.txt); `core/` is our host-tested core as its own component.
idf_component_register(
    SRCS
        "main.cpp"
        "font.cpp"
        "screen.cpp"
        "launcher_page.cpp"
        "radio_link.cpp"
        # S502 step 4: the shared chrome (status bar / page head / hint bar) and the two new pages.
        "chrome.cpp"
        "nodes_page.cpp"
        "chat_page.cpp"
        # S502 step 5: the chat history on the SD card (device glue for core/mc_history.*).
        "sd_history.cpp"
        # S502: the app's own clock (RTC / stored floor / a peer's authenticated timestamp).
        "time_source.cpp"
        "mc_link.cpp"
        # S503 step 6 slice 1: the map pack on the card (index search + tile read + JPEG decode).
        "map_link.cpp"
        "map_page.cpp"
        # EST-445: Settings, part 1 (frame + General). The rules are core/settings.h (host-tested).
        "settings_page.cpp"
        # S514/EST-450: the one owner of the I2S channel and the NVS sound settings. The synth itself
        # is compiled from the L2's own firmware/src/sound_synth.cpp in the core component.
        "audio_out.cpp"
        "sound_store.cpp"
    INCLUDE_DIRS
        "."
    # The six Tanmatsu faces (tools/mkfonts.py -> main/fonts/*.bin, decoded by font.cpp).
    EMBED_FILES
        "fonts/sans_title22.bin"
        "fonts/sans_row17.bin"
        "fonts/sans_body13.bin"
        "fonts/sans_meta12.bin"
        "fonts/sans_hint11.bin"
        "fonts/mono_kv12.bin"
    PRIV_REQUIRES
        esp_lcd
        fatfs
        esp_jpeg
        esp_driver_jpeg   # S506: the P4's hardware JPEG codec, with esp_jpeg as the fallback
        nvs_flash
        bt
        badge-bsp
        tanmatsu-wifi
        tanmatsu-lora
        esp_driver_sdmmc
        sdmmc
        vfs
        core
        ${extra_requires}
)

# TEMP (S499): panel calibration build — flip to 0 to get the normal Launcher back.
set(TM_CALIBRATION 0)
if(TM_CALIBRATION)
    idf_build_set_property(COMPILE_OPTIONS "-DTM_CALIBRATION" APPEND)
endif()

idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-variable" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-const-variable" APPEND)

# The palette is the L2's, parsed nowhere else: `launcher_page.cpp` includes
# ../../firmware/src/ui/theme.h directly (one source of truth; the L2 build is not touched by reading it).
