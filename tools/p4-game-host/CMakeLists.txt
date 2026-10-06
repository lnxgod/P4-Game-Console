# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.19)
project(p4_game_host VERSION 0.1.0 LANGUAGES C)

option(P4_ALLOW_DRAFT_GAME "Allow local play of an unpublished draft" OFF)
set(P4_GAME "" CACHE STRING "Native game directory name under games/")
if(NOT P4_GAME MATCHES "^[a-z0-9][a-z0-9_]*$")
    message(FATAL_ERROR "Set -DP4_GAME to a game slug such as space_invaders")
endif()

get_filename_component(P4_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(P4_GAME_DIR "${P4_REPO_ROOT}/games/${P4_GAME}")
set(P4_GAME_MANIFEST "${P4_GAME_DIR}/game.json")
if(NOT EXISTS "${P4_GAME_MANIFEST}")
    message(FATAL_ERROR "Unknown native game: ${P4_GAME}")
endif()

file(READ "${P4_GAME_MANIFEST}" P4_GAME_JSON)
string(JSON P4_GAME_FORMAT GET "${P4_GAME_JSON}" format)
string(JSON P4_GAME_API_VERSION GET "${P4_GAME_JSON}" api_version)
string(JSON P4_GAME_ENTRY GET "${P4_GAME_JSON}" entry_symbol)
string(JSON P4_GAME_ENABLED GET "${P4_GAME_JSON}" enabled)
string(JSON P4_GAME_RESOURCE_PAYLOAD ERROR_VARIABLE P4_GAME_RESOURCE_ERROR
    GET "${P4_GAME_JSON}" resource_payload)
if(P4_GAME_RESOURCE_ERROR)
    set(P4_GAME_RESOURCE_PATH "")
else()
    set(P4_GAME_RESOURCE_PATH
        "${P4_GAME_DIR}/${P4_GAME_RESOURCE_PAYLOAD}")
    if(NOT EXISTS "${P4_GAME_RESOURCE_PATH}")
        message(FATAL_ERROR
            "${P4_GAME} resource payload is missing: ${P4_GAME_RESOURCE_PATH}")
    endif()
endif()
if(NOT P4_GAME_FORMAT STREQUAL "p4-native-elf-v1" OR
   NOT P4_GAME_API_VERSION EQUAL 1 OR (NOT P4_GAME_ENABLED AND NOT P4_ALLOW_DRAFT_GAME))
    message(FATAL_ERROR "${P4_GAME} is not an enabled P4 Game API v1 game")
endif()
if(NOT P4_GAME_ENTRY MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "${P4_GAME} has an invalid entry symbol")
endif()

file(GLOB P4_GAME_SOURCES CONFIGURE_DEPENDS "${P4_GAME_DIR}/src/*.c")
if(NOT P4_GAME_SOURCES)
    message(FATAL_ERROR "${P4_GAME} has no C sources")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
add_subdirectory(
    "${P4_REPO_ROOT}/components/p4_game_api"
    "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
)
add_subdirectory(
    "${P4_REPO_ROOT}/components/p4_signal_scan"
    "${CMAKE_CURRENT_BINARY_DIR}/p4_signal_scan"
)
add_subdirectory(
    "${P4_REPO_ROOT}/components/p4_game_save"
    "${CMAKE_CURRENT_BINARY_DIR}/p4_game_save"
)

add_executable(p4_game_host play_game.c host_service.c ${P4_GAME_SOURCES})
set_target_properties(p4_game_host PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.p4console.game-host"
    MACOSX_BUNDLE_BUNDLE_NAME "P4 Game Host"
)
target_include_directories(p4_game_host PRIVATE
    "${P4_GAME_DIR}/include"
    "${P4_GAME_DIR}/src"
)
target_compile_definitions(p4_game_host PRIVATE
    "P4_HOST_GAME_DESCRIPTOR=${P4_GAME_ENTRY}"
    "P4_HOST_RESOURCE_PATH=\"${P4_GAME_RESOURCE_PATH}\""
)
target_compile_features(p4_game_host PRIVATE c_std_11)
target_link_libraries(
    p4_game_host PRIVATE p4_game_save p4_signal_scan PkgConfig::SDL3
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_game_host PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_game_host PRIVATE
        -fsanitize=address,undefined
    )
endif()

include(CTest)
add_test(NAME p4_game_host_smoke COMMAND p4_game_host --frames 8)
set_tests_properties(p4_game_host_smoke PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
)

add_executable(p4_host_audio_timing_tests test_audio_timing.c host_service.c)
target_link_libraries(p4_host_audio_timing_tests PRIVATE p4_game_api)
target_compile_features(p4_host_audio_timing_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_host_audio_timing_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(p4_host_audio_timing_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_host_audio_timing COMMAND p4_host_audio_timing_tests)

# Separate optimized CPU benchmark: simulator/sanitizer acceptance stays unchanged.
add_executable(p4_game_benchmark benchmark_game.c ${P4_GAME_SOURCES}
    "${P4_REPO_ROOT}/components/p4_game_api/src/achievements.c"
    "${P4_REPO_ROOT}/components/p4_game_api/src/audio_mixer.c"
    "${P4_REPO_ROOT}/components/p4_game_api/src/audio_pack.c"
    "${P4_REPO_ROOT}/components/p4_game_api/src/draw.c"
    "${P4_REPO_ROOT}/components/p4_game_api/src/feedback.c"
    "${P4_REPO_ROOT}/components/p4_game_api/src/game_runtime.c"
    "${P4_REPO_ROOT}/components/p4_game_api/src/input.c"
    "${P4_REPO_ROOT}/components/p4_game_api/src/presentation_font.c"
    "${P4_REPO_ROOT}/components/p4_game_api/src/visual.c"
    "${P4_REPO_ROOT}/components/p4_cp437/src/cp437.c")
target_include_directories(p4_game_benchmark PRIVATE
    "${P4_GAME_DIR}/include" "${P4_GAME_DIR}/src"
    "${P4_REPO_ROOT}/components/p4_game_api/include"
    "${P4_REPO_ROOT}/components/p4_cp437/include")
target_compile_definitions(p4_game_benchmark PRIVATE "P4_HOST_GAME_DESCRIPTOR=${P4_GAME_ENTRY}")
target_compile_features(p4_game_benchmark PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_game_benchmark PRIVATE -O2
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

# Black-box process status: update/render failure must fail the smoke gate;
# a game's ordinary Back/launcher exit remains successful.
add_executable(p4_host_exit_status_tests test_exit_status.c host_service.c)
target_link_libraries(p4_host_exit_status_tests PRIVATE
    p4_game_save p4_signal_scan PkgConfig::SDL3)
target_compile_features(p4_host_exit_status_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_host_exit_status_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(p4_host_exit_status_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_host_exit_status COMMAND p4_host_exit_status_tests)
set_tests_properties(p4_host_exit_status PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

# A complete fast desktop drag must not collapse into only its initial click.
add_executable(p4_host_mouse_events_tests test_mouse_events.c host_service.c)
target_link_libraries(p4_host_mouse_events_tests PRIVATE
    p4_game_save p4_signal_scan PkgConfig::SDL3)
target_compile_features(p4_host_mouse_events_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_host_mouse_events_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(p4_host_mouse_events_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_host_mouse_events COMMAND p4_host_mouse_events_tests)
set_tests_properties(p4_host_mouse_events PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
