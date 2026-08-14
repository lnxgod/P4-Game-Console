# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../doom/components"
    "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(p4_doom_embedded_gamepad_audio)

add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}.elf
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DP4_LINK_NM=${CMAKE_NM}"
        "-DP4_LINK_ELF=$<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>"
        -P "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_usb_host/cmake/verify_native_gamepad_link.cmake"
    VERBATIM
)
