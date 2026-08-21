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
    console_shell
    doom_multiplayer
    esp_timer
    esp_system
    freertos
    heap
    log
    lwip
    mbedtls
    p4_content_catalog
    p4_bbs
    p4_desktop
    p4_game_api
    p4_game_save
    p4_multiplayer
    p4_usb_content_transfer
    p4_lua_runtime
    p4_script_audio
    p4_script_renderer
    platform_board
    platform_console_settings
    platform_display
    platform_game_catalog
    platform_game_loader
    platform_game_storage
    platform_os_update
    vfs
)

idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
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
            platform_gamepad_usb
            platform_usb_host
        )
        # The top-level build removes ESP-Hosted's constructor so both H2 role
        # images can start the passive scanner explicitly after launcher READY.
        list(APPEND P4_CONSOLE_MAIN_REQUIREMENTS
            platform_signal_scan
        )
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
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
)
