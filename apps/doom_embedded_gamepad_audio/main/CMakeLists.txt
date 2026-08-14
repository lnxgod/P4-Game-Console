# SPDX-License-Identifier: GPL-2.0-or-later

get_filename_component(
    P4_LOCAL_DOOM_WAD
    "${CMAKE_CURRENT_LIST_DIR}/../../../local-data/doom/doom1.wad"
    ABSOLUTE
)
set(P4_EXPECTED_DOOM_WAD_BYTES 4196020)
set(P4_EXPECTED_DOOM_WAD_SHA256
    "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771")

if(NOT EXISTS "${P4_LOCAL_DOOM_WAD}")
    message(FATAL_ERROR
        "doom_embedded_gamepad_audio requires the ignored local Doom 1.9 shareware WAD at local-data/doom/doom1.wad")
endif()
file(SIZE "${P4_LOCAL_DOOM_WAD}" P4_LOCAL_DOOM_WAD_BYTES)
file(SHA256 "${P4_LOCAL_DOOM_WAD}" P4_LOCAL_DOOM_WAD_SHA256)
if(NOT P4_LOCAL_DOOM_WAD_BYTES EQUAL P4_EXPECTED_DOOM_WAD_BYTES)
    message(FATAL_ERROR "local Doom shareware WAD has the wrong byte count")
endif()
if(NOT P4_LOCAL_DOOM_WAD_SHA256 STREQUAL P4_EXPECTED_DOOM_WAD_SHA256)
    message(FATAL_ERROR "local Doom shareware WAD has the wrong SHA-256")
endif()

idf_component_register(
    SRCS
        "doom_embedded_gamepad_audio_main.c"
        "runtime_gate.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        doom_audio
        doom_engine_audio
        doom_gamepad_input
        doom_video
        esp_timer
        freertos
        gamepad_core
        heap
        log
        mbedtls
        platform_audio
        platform_display
        platform_gamepad_usb
        platform_readonly_blob
        platform_usb_host
        vfs
)

target_add_binary_data(
    ${COMPONENT_LIB}
    "${P4_LOCAL_DOOM_WAD}"
    BINARY
    RENAME_TO doom_shareware_wad
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
)
