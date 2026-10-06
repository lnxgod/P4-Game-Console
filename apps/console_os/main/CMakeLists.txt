# SPDX-License-Identifier: GPL-2.0-or-later

set(P4_DOOM_TOUCH_AUDIO_MAIN
    "${CMAKE_CURRENT_LIST_DIR}/../../doom_embedded_touch_audio/main")
set(P4_DOOM_GAMEPAD_AUDIO_MAIN
    "${CMAKE_CURRENT_LIST_DIR}/../../doom_embedded_gamepad_audio/main")

set(P4_CONSOLE_MAIN_SOURCES
    "console_os_main.c"
    "doom_p4mp_adapter.c"
)
set(P4_CONSOLE_MAIN_INCLUDE_DIRS ".")
set(P4_CONSOLE_MAIN_REQUIREMENTS
    console_startup
    console_shell
    doom_multiplayer
    esp_timer
    esp_system
    freertos
    heap
    log
    lwip
    mbedtls
    p4_bbs
    p4_ble_radio_handoff
    p4_desktop
    p4_game_api
    p4_game_save
    p4_multiplayer
    p4_multiplayer_registry
    p4_usb_content_transfer
    p4_frame_scheduler
    platform_board
    platform_console_settings
    platform_display
    platform_game_catalog
    platform_game_loader
    platform_game_storage
    platform_os_update
    platform_save_seal
    vfs
)

idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
idf_build_get_property(P4_TAB5_BLE_MULTIPLAYER_BUILD P4_TAB5_BLE_MULTIPLAYER_BUILD)
idf_build_get_property(P4_TAB5_USB_HOST_BUILD P4_TAB5_USB_HOST_BUILD)
idf_build_get_property(P4_WAVESHARE_SIGNAL_SCAN_BUILD
    P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD)
idf_build_get_property(P4_WAVESHARE_BLE_MULTIPLAYER_BUILD
    P4_WAVESHARE_BLE_MULTIPLAYER_BUILD)
idf_build_get_property(P4_WAVESHARE_BLE_GAMEPAD_BUILD
    P4_WAVESHARE_BLE_GAMEPAD_BUILD)
idf_build_get_property(P4_CONSOLE_RUNTIME_STATS_BUILD
    P4_CONSOLE_RUNTIME_STATS_BUILD)
set(P4_CONSOLE_WIFI_MULTIPLAYER_ENABLED 0)
set(P4_CONSOLE_SIGNAL_SCAN_ENABLED 0)
set(P4_CONSOLE_BLE_MULTIPLAYER_ENABLED 0)
set(P4_CONSOLE_BLE_GAMEPAD_ENABLED 0)
set(P4_CONSOLE_BLE_DIAGNOSTIC_AUTOSTART_ENABLED 0)
set(P4_CONSOLE_RUNTIME_STATS_ENABLED 0)
if(P4_CONSOLE_RUNTIME_STATS_BUILD)
    set(P4_CONSOLE_RUNTIME_STATS_ENABLED 1)
endif()
if(P4_ACTIVE_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
    list(APPEND P4_CONSOLE_MAIN_SOURCES
        "${P4_DOOM_GAMEPAD_AUDIO_MAIN}/doom_embedded_gamepad_audio_main.c"
    )
    list(APPEND P4_CONSOLE_MAIN_INCLUDE_DIRS
        "${P4_DOOM_GAMEPAD_AUDIO_MAIN}"
    )
    list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS
        board_deps_olimex
        doom_audio
        doom_engine_audio
        doom_gamepad_input
        doom_video
        gamepad_core
        p4_game_platform
        platform_audio
        platform_gamepad
        platform_gamepad_usb
        platform_readonly_blob
        platform_usb_host
    )
else()
    list(APPEND P4_CONSOLE_MAIN_SOURCES
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/doom_embedded_touch_audio_main.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/audio_lifecycle.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/degraded_policy.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/runtime_gate.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/touch_controls.c"
    )
    list(APPEND P4_CONSOLE_MAIN_INCLUDE_DIRS
        "${P4_DOOM_TOUCH_AUDIO_MAIN}"
    )
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
        set(P4_TOUCH_BOARD_DEPS board_deps_waveshare)
        # Keep the host/HID platform link available in both mutually exclusive
        # H2 role images. sdkconfig selects whether Console OS starts it; the
        # ordinary device/MSC image remains electrically inert as a host.
        list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS
            doom_gamepad_input
            gamepad_core
            platform_battery
            platform_gamepad
            platform_gamepad_usb
            platform_usb_host
        )
        if(P4_WAVESHARE_SIGNAL_SCAN_BUILD)
            # This remains opt-in until its exact SDIO transport passes the
            # top-level overlap gate and live-radio acceptance.
            list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS
                platform_signal_scan
            )
            set(P4_CONSOLE_SIGNAL_SCAN_ENABLED 1)
        endif()
        if(P4_WAVESHARE_BLE_MULTIPLAYER_BUILD)
            list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS
                platform_multiplayer_ble
                platform_dice_ble
            )
            set(P4_CONSOLE_BLE_MULTIPLAYER_ENABLED 1)
            if(P4_BLE_DIAGNOSTIC_AUTOSTART)
                set(P4_CONSOLE_BLE_DIAGNOSTIC_AUTOSTART_ENABLED 1)
            endif()
        endif()
        if(P4_WAVESHARE_BLE_GAMEPAD_BUILD)
            list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS
                platform_gamepad_ble
            )
            set(P4_CONSOLE_BLE_GAMEPAD_ENABLED 1)
        endif()
    elseif(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5")
        set(P4_TOUCH_BOARD_DEPS board_deps_tab5 platform_tab5)
        if(P4_TAB5_BLE_MULTIPLAYER_BUILD)
            list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS platform_multiplayer_ble platform_dice_ble platform_multiplayer_wifi)
            set(P4_CONSOLE_WIFI_MULTIPLAYER_ENABLED 1)
            set(P4_CONSOLE_BLE_MULTIPLAYER_ENABLED 1)
        endif()
        if(P4_TAB5_USB_HOST_BUILD)
            list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS
                doom_gamepad_input gamepad_core platform_gamepad
                platform_gamepad_usb platform_gamepad_xusb platform_usb_host)
        endif()
    else()
        set(P4_TOUCH_BOARD_DEPS board_deps_elecrow)
    endif()
    list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS
        ${P4_TOUCH_BOARD_DEPS}
        doom_audio
        doom_engine_audio
        doom_touch_input
        doom_video
        p4_game_platform
        platform_audio
        platform_i2c_shared
        platform_readonly_blob
        platform_touch
    )
endif()

idf_component_register(
    SRCS ${P4_CONSOLE_MAIN_SOURCES}
    INCLUDE_DIRS ${P4_CONSOLE_MAIN_INCLUDE_DIRS}
    PRIV_REQUIRES ${P4_CONSOLE_MAIN_REQUIREMENTS}
)

set(P4_GAMECHANGERS_AI_LOGO
    "${CMAKE_CURRENT_LIST_DIR}/assets/gamechangers_ai_logo.rgb565")
if(NOT EXISTS "${P4_GAMECHANGERS_AI_LOGO}")
    message(FATAL_ERROR "Console OS requires the Game Changers AI boot logo")
endif()
target_add_binary_data(
    ${COMPONENT_LIB}
    "${P4_GAMECHANGERS_AI_LOGO}"
    BINARY
    RENAME_TO gamechangers_ai_logo_rgb565
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/doomgeneric/doomgeneric"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    P4_CONSOLE_OS_EMBEDDED=1
    P4_CONSOLE_SIGNAL_SCAN=${P4_CONSOLE_SIGNAL_SCAN_ENABLED}
    P4_CONSOLE_WIFI_MULTIPLAYER=${P4_CONSOLE_WIFI_MULTIPLAYER_ENABLED}
    P4_CONSOLE_BLE_MULTIPLAYER=${P4_CONSOLE_BLE_MULTIPLAYER_ENABLED}
    P4_CONSOLE_BLE_GAMEPAD=${P4_CONSOLE_BLE_GAMEPAD_ENABLED}
    P4_BLE_DIAGNOSTIC_AUTOSTART=${P4_CONSOLE_BLE_DIAGNOSTIC_AUTOSTART_ENABLED}
    CONSOLE_OS_ENABLE_RUNTIME_STATS=${P4_CONSOLE_RUNTIME_STATS_ENABLED}
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
)

# Tab5 uses the reviewed flight mark; compact/legacy boards retain their asset.
if(CONFIG_P4_BOARD_M5STACK_TAB5)
    set(P4_GAMECHANGERS_BOOT_MARK "${CMAKE_CURRENT_LIST_DIR}/assets/gamechangers_mark_flight.rgb565a8")
    set(P4_BOOT_MARK_EXPECTED "4cf8a16d89b645fb0f6d8b4d4c6080f73224100f8b42834d9672b1a2283bb11e")
else()
    set(P4_GAMECHANGERS_BOOT_MARK "${CMAKE_CURRENT_LIST_DIR}/assets/gamechangers_mark_v042.rgb565a8")
    set(P4_BOOT_MARK_EXPECTED "f0dea8368982c71c6d82ce3b6d711cf3d699e66e12c9ef8af93c2133a6bc9683")
endif()
file(SHA256 "${P4_GAMECHANGERS_BOOT_MARK}" P4_BOOT_MARK_SHA256)
file(SIZE "${P4_GAMECHANGERS_BOOT_MARK}" P4_BOOT_MARK_BYTES)
if(NOT P4_BOOT_MARK_BYTES EQUAL 442368 OR NOT P4_BOOT_MARK_SHA256 STREQUAL P4_BOOT_MARK_EXPECTED)
    message(FATAL_ERROR "GameChangersAI boot mark differs from reviewed asset")
endif()
target_add_binary_data(${COMPONENT_LIB} "${P4_GAMECHANGERS_BOOT_MARK}" BINARY
    RENAME_TO gamechangers_boot_mark_rgb565a8)
