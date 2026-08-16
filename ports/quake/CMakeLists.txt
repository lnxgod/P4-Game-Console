# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.19)
project(p4_quake_host LANGUAGES C)

get_filename_component(P4_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(P4_QUAKE_SOURCE "${P4_REPO_ROOT}/third_party/quakegeneric/source")
set(P4_QUAKE_DATA_DIR "" CACHE PATH "Directory containing id1/pak0.pak")

include("${CMAKE_CURRENT_LIST_DIR}/engine-sources.cmake")
set(P4_QUAKE_ENGINE_SOURCES ${P4_QUAKE_ENGINE_SOURCE_NAMES})
list(TRANSFORM P4_QUAKE_ENGINE_SOURCES PREPEND "${P4_QUAKE_SOURCE}/")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)

add_library(p4_quake_engine STATIC ${P4_QUAKE_ENGINE_SOURCES})
target_include_directories(p4_quake_engine PUBLIC
    "${P4_QUAKE_SOURCE}"
    "${CMAKE_CURRENT_LIST_DIR}/host/include"
)
target_compile_features(p4_quake_engine PUBLIC c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    # This is a verbatim 1996 engine plus Espressif's embedded annotations.
    # Keep warnings visible in our adapter without rewriting the vendor tree.
    target_compile_options(p4_quake_engine PRIVATE -include assert.h -w)
endif()

add_executable(p4_quake_host host/quake_host_sdl3.c)
target_link_libraries(p4_quake_host PRIVATE p4_quake_engine PkgConfig::SDL3)
if(UNIX)
    target_link_libraries(p4_quake_host PRIVATE m)
endif()
target_compile_features(p4_quake_host PRIVATE c_std_11)
if(P4_QUAKE_DATA_DIR)
    file(REAL_PATH "${P4_QUAKE_DATA_DIR}" P4_QUAKE_DATA_DIR_ABSOLUTE)
    target_compile_definitions(p4_quake_host PRIVATE
        P4_QUAKE_DEFAULT_BASEDIR="${P4_QUAKE_DATA_DIR_ABSOLUTE}"
    )
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_quake_host PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -Wno-strict-prototypes
    )
endif()

include(CTest)
add_test(NAME p4_quake_host_usage COMMAND p4_quake_host --help)
if(P4_QUAKE_DATA_DIR)
    add_test(NAME p4_quake_host_smoke
        COMMAND p4_quake_host --headless --no-audio --frames 8)
    set_tests_properties(p4_quake_host_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        TIMEOUT 30
    )
endif()
