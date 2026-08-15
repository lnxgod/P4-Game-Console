# SPDX-License-Identifier: GPL-2.0-or-later

set(P4_DOOM_TOUCH_AUDIO_MAIN
    "${CMAKE_CURRENT_LIST_DIR}/../../doom_embedded_touch_audio/main")

idf_component_register(
    SRCS
        "console_os_main.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/doom_embedded_touch_audio_main.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/audio_lifecycle.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/degraded_policy.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/runtime_gate.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/touch_controls.c"
    INCLUDE_DIRS
        "."
        "${P4_DOOM_TOUCH_AUDIO_MAIN}"
    PRIV_REQUIRES
        console_shell
        doom_audio
        doom_engine_audio
        doom_touch_input
        doom_video
        esp_timer
        freertos
        heap
        log
        mbedtls
        p4_game_api
        p4_game_platform
        platform_audio
        platform_display
        platform_game_catalog
        platform_game_loader
        platform_game_storage
        platform_i2c_shared
        platform_os_update
        platform_readonly_blob
        platform_touch
        vfs
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
