# SPDX-License-Identifier: GPL-2.0-or-later

set(P4_DOOM_TOUCH_AUDIO_MAIN
    "${CMAKE_CURRENT_LIST_DIR}/../../doom_embedded_touch_audio/main")
set(P4_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(P4_GAME_REGISTRY_DIR "${CMAKE_CURRENT_BINARY_DIR}/p4_game_registry")
set(P4_GAME_REGISTRY_C "${P4_GAME_REGISTRY_DIR}/p4_game_registry.c")
set(P4_GAME_REGISTRY_H "${P4_GAME_REGISTRY_DIR}/p4_game_registry.h")
set(P4_GAME_REGISTRY_CMAKE "${P4_GAME_REGISTRY_DIR}/games.cmake")
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB P4_GAME_MANIFESTS
        "${P4_REPOSITORY_ROOT}/games/*/game.json"
    )
else()
    file(GLOB P4_GAME_MANIFESTS CONFIGURE_DEPENDS
        "${P4_REPOSITORY_ROOT}/games/*/game.json"
    )
endif()
find_program(P4_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
file(MAKE_DIRECTORY "${P4_GAME_REGISTRY_DIR}")
execute_process(
    COMMAND
        "${P4_PYTHON3_EXECUTABLE}"
        "${P4_REPOSITORY_ROOT}/scripts/generate-game-registry.py"
        --games-root "${P4_REPOSITORY_ROOT}/games"
        --output-c "${P4_GAME_REGISTRY_C}"
        --output-h "${P4_GAME_REGISTRY_H}"
        --output-cmake "${P4_GAME_REGISTRY_CMAKE}"
    RESULT_VARIABLE P4_GAME_REGISTRY_RESULT
    OUTPUT_VARIABLE P4_GAME_REGISTRY_OUTPUT
    ERROR_VARIABLE P4_GAME_REGISTRY_ERROR
)
if(NOT P4_GAME_REGISTRY_RESULT EQUAL 0)
    message(FATAL_ERROR
        "P4 game registry generation failed: ${P4_GAME_REGISTRY_ERROR}")
endif()
include("${P4_GAME_REGISTRY_CMAKE}")

idf_component_register(
    SRCS
        "console_os_main.c"
        "${P4_GAME_REGISTRY_C}"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/doom_embedded_touch_audio_main.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/audio_lifecycle.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/degraded_policy.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/runtime_gate.c"
        "${P4_DOOM_TOUCH_AUDIO_MAIN}/touch_controls.c"
    INCLUDE_DIRS
        "."
        "${P4_DOOM_TOUCH_AUDIO_MAIN}"
        "${P4_GAME_REGISTRY_DIR}"
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
        platform_game_storage
        platform_i2c_shared
        platform_readonly_blob
        platform_touch
        vfs
        ${P4_GENERATED_GAME_COMPONENTS}
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/doomgeneric/doomgeneric"
)

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
        "console_os requires the ignored Doom 1.9 shareware WAD at local-data/doom/doom1.wad")
endif()
file(SIZE "${P4_LOCAL_DOOM_WAD}" P4_LOCAL_DOOM_WAD_BYTES)
file(SHA256 "${P4_LOCAL_DOOM_WAD}" P4_LOCAL_DOOM_WAD_SHA256)
if(NOT P4_LOCAL_DOOM_WAD_BYTES EQUAL P4_EXPECTED_DOOM_WAD_BYTES)
    message(FATAL_ERROR "local Doom shareware WAD has the wrong byte count")
endif()
if(NOT P4_LOCAL_DOOM_WAD_SHA256 STREQUAL P4_EXPECTED_DOOM_WAD_SHA256)
    message(FATAL_ERROR "local Doom shareware WAD has the wrong SHA-256")
endif()
target_add_binary_data(
    ${COMPONENT_LIB}
    "${P4_LOCAL_DOOM_WAD}"
    BINARY
    RENAME_TO doom_shareware_wad
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
