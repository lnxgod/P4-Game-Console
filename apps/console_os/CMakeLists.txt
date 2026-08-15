# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.2.0")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../doom_embedded_touch_audio/components/platform_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/console_shell"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_touch_input"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_factory"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_display"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_catalog"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_loader"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_storage"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_i2c_shared"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_os_update"
    "${CMAKE_CURRENT_LIST_DIR}/components/platform_readonly_blob"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_touch"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_package"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_os_update_package"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_platform"
    "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(p4_console_os)
idf_build_get_property(P4_IDF_PYTHON PYTHON)
get_filename_component(P4_REPOSITORY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# A reviewed full-project install seeds a valid wear-levelled FAT volume.
# Subsequent app-only updates leave this persistent partition untouched.
set(P4_GAME_STORAGE_SEED_DIR "${CMAKE_BINARY_DIR}/game-storage-seed")
set(P4_LOCAL_DOOM_WAD
    "${CMAKE_CURRENT_LIST_DIR}/../../local-data/doom/doom1.wad")
if(NOT EXISTS "${P4_LOCAL_DOOM_WAD}")
    message(FATAL_ERROR
        "console_os requires the ignored Doom 1.9 shareware WAD")
endif()
file(SIZE "${P4_LOCAL_DOOM_WAD}" P4_LOCAL_DOOM_WAD_BYTES)
file(SHA256 "${P4_LOCAL_DOOM_WAD}" P4_LOCAL_DOOM_WAD_SHA256)
if(NOT P4_LOCAL_DOOM_WAD_BYTES EQUAL 4196020 OR
   NOT P4_LOCAL_DOOM_WAD_SHA256 STREQUAL
       "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771")
    message(FATAL_ERROR "local Doom shareware WAD identity does not match")
endif()
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

string(REPLACE "-gcc" "-strip" P4_RISCV_STRIP "${CMAKE_C_COMPILER}")
set(P4_PACKAGE_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/../../scripts/build-game-package.py")
set(P4_CARTRIDGE_COMMON_SOURCES
    "${P4_PACKAGE_SCRIPT}"
    "${CMAKE_CURRENT_LIST_DIR}/../../game-platform/runtime/cartridge_main.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/draw.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/game_runtime.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/input.c"
)

function(p4_add_seed_game PACKAGE_NAME GAME_NAME)
    set(P4_GAME_DIR
        "${P4_REPOSITORY_ROOT}/games/${GAME_NAME}")
    set(P4_PACKAGE_OUTPUT
        "${P4_GAME_STORAGE_SEED_DIR}/${PACKAGE_NAME}")
    file(GLOB_RECURSE P4_GAME_SOURCES CONFIGURE_DEPENDS
        "${P4_GAME_DIR}/*.c" "${P4_GAME_DIR}/*.h")
    add_custom_command(
        OUTPUT "${P4_PACKAGE_OUTPUT}"
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_PACKAGE_SCRIPT}"
            --manifest "${P4_GAME_DIR}/game.json"
            --compiler "${CMAKE_C_COMPILER}"
            --strip "${P4_RISCV_STRIP}"
            --output "${P4_PACKAGE_OUTPUT}"
        DEPENDS
            ${P4_CARTRIDGE_COMMON_SOURCES}
            ${P4_GAME_SOURCES}
            "${P4_GAME_DIR}/game.json"
        VERBATIM
    )
    set(P4_SEEDED_GAME_PACKAGES
        ${P4_SEEDED_GAME_PACKAGES} "${P4_PACKAGE_OUTPUT}"
        PARENT_SCOPE)
endfunction()

file(GLOB P4_GAME_MANIFESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../../games/*/game.json")
set(P4_GENERATED_GAMES_CMAKE "${CMAKE_BINARY_DIR}/p4-games.cmake")
execute_process(
    COMMAND
        "${P4_IDF_PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/generate-game-registry.py"
        --games-root "${CMAKE_CURRENT_LIST_DIR}/../../games"
        --output-cmake "${P4_GENERATED_GAMES_CMAKE}"
    RESULT_VARIABLE P4_GAME_MANIFEST_RESULT
    OUTPUT_VARIABLE P4_GAME_MANIFEST_REPORT
    ERROR_VARIABLE P4_GAME_MANIFEST_ERROR
)
if(NOT P4_GAME_MANIFEST_RESULT EQUAL 0)
    message(FATAL_ERROR
        "game manifest validation failed: ${P4_GAME_MANIFEST_ERROR}")
endif()
message(STATUS "${P4_GAME_MANIFEST_REPORT}")
include("${P4_GENERATED_GAMES_CMAKE}")
add_custom_target(p4_game_packages ALL DEPENDS ${P4_SEEDED_GAME_PACKAGES})

fatfs_create_spiflash_image(
    game_data "${P4_GAME_STORAGE_SEED_DIR}" FLASH_IN_PROJECT)
add_dependencies(fatfs_game_data_bin p4_game_packages)
add_custom_command(TARGET fatfs_game_data_bin POST_BUILD
    COMMAND
        "${P4_IDF_PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/normalize-game-storage-image.py"
        "${CMAKE_BINARY_DIR}/game_data.bin"
    VERBATIM
)

set(P4_OS_UPDATE_OUTPUT "${CMAKE_BINARY_DIR}/P4UPDATE.P4U")
add_custom_target(p4_os_update_package ALL
    COMMAND
        "${P4_IDF_PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/build-os-update.py"
        --image "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
        --version "${PROJECT_VER}"
        --source-root "${CMAKE_CURRENT_LIST_DIR}/../.."
        --output "${P4_OS_UPDATE_OUTPUT}"
    DEPENDS
        gen_project_binary
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/build-os-update.py"
    BYPRODUCTS "${P4_OS_UPDATE_OUTPUT}"
    VERBATIM
)
