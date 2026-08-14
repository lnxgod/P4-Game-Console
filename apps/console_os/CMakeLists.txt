# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
set(P4_MONOREPO_GAMES_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../games")
file(GLOB P4_MONOREPO_GAME_CMAKES CONFIGURE_DEPENDS
    "${P4_MONOREPO_GAMES_ROOT}/*/CMakeLists.txt"
)
set(P4_MONOREPO_GAME_DIRS)
foreach(P4_GAME_CMAKE IN LISTS P4_MONOREPO_GAME_CMAKES)
    get_filename_component(P4_GAME_DIR "${P4_GAME_CMAKE}" DIRECTORY)
    list(APPEND P4_MONOREPO_GAME_DIRS "${P4_GAME_DIR}")
endforeach()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../doom_embedded_touch_audio/components/platform_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/console_shell"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_touch_input"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_factory"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_display"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_storage"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_i2c_shared"
    "${CMAKE_CURRENT_LIST_DIR}/components/platform_readonly_blob"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_touch"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_platform"
    "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
    ${P4_MONOREPO_GAME_DIRS}
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(p4_console_os)

# A reviewed full-project install seeds a valid wear-levelled FAT volume.
# Subsequent app-only updates leave this persistent partition untouched.
set(P4_GAME_STORAGE_SEED_DIR "${CMAKE_BINARY_DIR}/game-storage-seed")
set(P4_LOCAL_DOOM_WAD
    "${CMAKE_CURRENT_LIST_DIR}/../../local-data/doom/doom1.wad")
file(MAKE_DIRECTORY "${P4_GAME_STORAGE_SEED_DIR}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/game-storage/README.TXT"
    "${P4_GAME_STORAGE_SEED_DIR}/README.TXT"
    COPYONLY
)
configure_file(
    "${P4_LOCAL_DOOM_WAD}"
    "${P4_GAME_STORAGE_SEED_DIR}/DOOM1.WAD"
    COPYONLY
)
fatfs_create_spiflash_image(
    game_data "${P4_GAME_STORAGE_SEED_DIR}" FLASH_IN_PROJECT)
idf_build_get_property(P4_IDF_PYTHON PYTHON)
add_custom_command(TARGET fatfs_game_data_bin POST_BUILD
    COMMAND
        "${P4_IDF_PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/normalize-game-storage-image.py"
        "${CMAKE_BINARY_DIR}/game_data.bin"
    VERBATIM
)
