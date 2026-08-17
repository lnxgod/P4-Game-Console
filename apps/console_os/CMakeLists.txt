# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.3.0")

if(NOT DEFINED P4_BOARD_PROFILE)
    set(P4_BOARD_PROFILE "elecrow-crowpanel-advanced-10")
endif()
if(NOT P4_BOARD_PROFILE STREQUAL "elecrow-crowpanel-advanced-10" AND
   NOT P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" AND
   NOT P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    message(FATAL_ERROR "unsupported P4_BOARD_PROFILE=${P4_BOARD_PROFILE}")
endif()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components/console_shell"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_board"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_display"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_catalog"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_loader"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_storage"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_os_update"
    "${CMAKE_CURRENT_LIST_DIR}/components/platform_readonly_blob"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_content_catalog"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_desktop"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_package"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_multiplayer"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_os_update_package"
)

if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../components/board_deps_olimex"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/olimex/platform_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_gamepad_input"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/gamepad_core"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_platform"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_gamepad_usb"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_usb_host"
        "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
    )
elseif(P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../components/board_deps_waveshare"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_embedded_touch_audio/components/platform_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_touch_input"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_es8311"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_factory"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_i2c_shared"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_touch"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_platform"
        "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
    )
else()
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../components/board_deps_elecrow"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_embedded_touch_audio/components/platform_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_touch_input"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_factory"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_i2c_shared"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_touch"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_platform"
        "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
    # The two hardware profiles intentionally resolve different managed
    # component graphs. Keep each resolution independently authoritative.
    idf_build_set_property(DEPENDENCIES_LOCK dependencies-olimex.lock)
elseif(P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    idf_build_set_property(DEPENDENCIES_LOCK dependencies-waveshare.lock)
endif()
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(P4_BOARD_PROFILE "${P4_BOARD_PROFILE}")
project(p4_console_os)
idf_build_get_property(P4_IDF_PYTHON PYTHON)
get_filename_component(P4_REPOSITORY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# A reviewed full-project install seeds a valid wear-levelled FAT volume.
# Subsequent app-only updates leave this persistent partition untouched.
if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" OR
   P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    set(P4_GAME_STORAGE_SEED_DIR "${CMAKE_BINARY_DIR}/sd-card")
else()
    set(P4_GAME_STORAGE_SEED_DIR "${CMAKE_BINARY_DIR}/game-storage-seed")
endif()
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
file(MAKE_DIRECTORY "${P4_GAME_STORAGE_SEED_DIR}/UPDATE")
file(MAKE_DIRECTORY "${P4_GAME_STORAGE_SEED_DIR}/P4/GAMES")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/game-storage/README.TXT"
    "${P4_GAME_STORAGE_SEED_DIR}/README.TXT"
    COPYONLY
)
if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/game-storage/README-OLIMEX.TXT"
        "${P4_GAME_STORAGE_SEED_DIR}/README.TXT"
        COPYONLY
    )
endif()
configure_file(
    "${P4_LOCAL_DOOM_WAD}"
    "${P4_GAME_STORAGE_SEED_DIR}/DOOM1.WAD"
    COPYONLY
)

# Preserve the original open, source-included P4 Cart format alongside the
# native P4G runtime. The Lua executor remains a separately reviewed sandbox
# milestone, but the deterministic cart is validated and visible on-device.
set(P4_LEGACY_CART_OUTPUT
    "${P4_GAME_STORAGE_SEED_DIR}/P4/GAMES/BOUNCE-LAB.P4CART")
file(GLOB_RECURSE P4_LEGACY_CART_SOURCES CONFIGURE_DEPENDS
    "${P4_REPOSITORY_ROOT}/game-platform/templates/bounce-lab/*")
add_custom_command(
    OUTPUT "${P4_LEGACY_CART_OUTPUT}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${P4_LEGACY_CART_OUTPUT}"
    COMMAND
        "${P4_IDF_PYTHON}"
        "${P4_REPOSITORY_ROOT}/game-platform/scripts/p4cart.py"
        pack
        "${P4_REPOSITORY_ROOT}/game-platform/templates/bounce-lab"
        "${P4_LEGACY_CART_OUTPUT}"
    DEPENDS
        "${P4_REPOSITORY_ROOT}/game-platform/scripts/p4cart.py"
        ${P4_LEGACY_CART_SOURCES}
    VERBATIM
)
add_custom_target(p4_legacy_cart_seed ALL
    DEPENDS "${P4_LEGACY_CART_OUTPUT}")

string(REPLACE "-gcc" "-strip" P4_RISCV_STRIP "${CMAKE_C_COMPILER}")
set(P4_PACKAGE_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/../../scripts/build-game-package.py")
set(P4_CARTRIDGE_COMMON_SOURCES
    "${P4_PACKAGE_SCRIPT}"
    "${CMAKE_CURRENT_LIST_DIR}/../../game-platform/runtime/cartridge_main.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/include/p4/audio_pack.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/include/p4/feedback.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/audio_pack.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/draw.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/feedback.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/generated/audio_pack.inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/generated/feedback_atlas.inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/game_runtime.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/input.c"
)

function(p4_add_seed_game PACKAGE_NAME GAME_NAME)
    set(P4_GAME_DIR
        "${P4_REPOSITORY_ROOT}/games/${GAME_NAME}")
    set(P4_PACKAGE_OUTPUT
        "${P4_GAME_STORAGE_SEED_DIR}/GAMES/${PACKAGE_NAME}")
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

# Keep one complete Game API cartridge in firmware so a new or temporarily
# missing removable card still has a real game to launch. A valid BYTEBUD.P4G
# on storage takes precedence at runtime, making the fallback replaceable
# without changing the firmware image.
set(P4_DEFAULT_GAME_PACKAGE
    "${P4_GAME_STORAGE_SEED_DIR}/GAMES/BYTEBUD.P4G")
target_add_binary_data(
    ${CMAKE_PROJECT_NAME}.elf
    "${P4_DEFAULT_GAME_PACKAGE}"
    BINARY
    DEPENDS p4_game_packages
    RENAME_TO bytebud_p4g
)

if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" OR
   P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    add_custom_target(p4_sd_card_bundle ALL
        DEPENDS p4_game_packages p4_legacy_cart_seed
        COMMENT "Preparing removable microSD game/update bundle"
    )
else()
    fatfs_create_spiflash_image(
        game_data "${P4_GAME_STORAGE_SEED_DIR}" FLASH_IN_PROJECT)
    add_dependencies(
        fatfs_game_data_bin p4_game_packages p4_legacy_cart_seed)
    add_custom_command(TARGET fatfs_game_data_bin POST_BUILD
        COMMAND
            "${P4_IDF_PYTHON}"
            "${CMAKE_CURRENT_LIST_DIR}/../../scripts/normalize-game-storage-image.py"
            "${CMAKE_BINARY_DIR}/game_data.bin"
        VERBATIM
    )
endif()

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
if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" OR
   P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    add_custom_command(TARGET p4_os_update_package POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${P4_OS_UPDATE_OUTPUT}"
            "${P4_GAME_STORAGE_SEED_DIR}/UPDATE/P4UPDATE.P4U"
        VERBATIM
    )
endif()
