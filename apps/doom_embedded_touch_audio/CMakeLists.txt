# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/platform_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_touch_input"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_factory"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_display"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_i2c_shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_readonly_blob"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_touch"
    "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(p4_doom_embedded_touch_audio)
