# SPDX-License-Identifier: GPL-2.0-or-later

get_filename_component(P4_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(P4_QUAKE_PORT "${P4_REPOSITORY_ROOT}/ports/quake")
set(P4_QUAKE_EMBEDDED "${P4_QUAKE_PORT}/embedded")
set(P4_QUAKE_SOURCE "${P4_REPOSITORY_ROOT}/third_party/quakegeneric/source")
include("${P4_QUAKE_PORT}/engine-sources.cmake")
# Start the embedded port with Quake's loopback driver only. Console OS does
# not own a qualified Wi-Fi transport yet, so the BSD socket adapter would
# waste internal RAM and bypass the OS-owned lobby boundary. The SDL3 host
# build keeps the original UDP path.
list(REMOVE_ITEM P4_QUAKE_ENGINE_SOURCE_NAMES
    common.c
    r_main.c
    sys_null.c
    net_bsd.c
    net_udp.c
    net_dgrm.c
)
list(APPEND P4_QUAKE_ENGINE_SOURCE_NAMES net_none.c)
set(P4_QUAKE_VENDOR_SOURCES ${P4_QUAKE_ENGINE_SOURCE_NAMES})
list(TRANSFORM P4_QUAKE_VENDOR_SOURCES PREPEND "${P4_QUAKE_SOURCE}/")

# Keep the pinned vendor tree byte-for-byte reproducible. The embedded build
# generates reviewed common.c and r_main.c port deltas outside that tree.
# Upstream keeps a 128 KiB maximum PAK directory and about 128 KiB of renderer
# edge/surface scratch on automatic stacks; both exceed the deliberately
# bounded Console OS and Quake task stacks.
set(P4_QUAKE_COMMON_PATCHER
    "${P4_QUAKE_EMBEDDED}/prepare_embedded_common.py")
set(P4_QUAKE_COMMON_VENDOR "${P4_QUAKE_SOURCE}/common.c")
set(P4_QUAKE_COMMON_EMBEDDED
    "${CMAKE_CURRENT_BINARY_DIR}/generated/common_embedded.c")
find_program(P4_QUAKE_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(
    COMMAND
        "${P4_QUAKE_PYTHON3_EXECUTABLE}"
        "${P4_QUAKE_COMMON_PATCHER}"
        --input "${P4_QUAKE_COMMON_VENDOR}"
        --output "${P4_QUAKE_COMMON_EMBEDDED}"
    RESULT_VARIABLE P4_QUAKE_COMMON_PATCH_RESULT
    ERROR_VARIABLE P4_QUAKE_COMMON_PATCH_ERROR
)
if(NOT P4_QUAKE_COMMON_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR
        "embedded Quake common.c preparation failed: "
        "${P4_QUAKE_COMMON_PATCH_ERROR}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${P4_QUAKE_COMMON_PATCHER}"
    "${P4_QUAKE_COMMON_VENDOR}"
)
list(APPEND P4_QUAKE_VENDOR_SOURCES "${P4_QUAKE_COMMON_EMBEDDED}")

set(P4_QUAKE_R_MAIN_PATCHER
    "${P4_QUAKE_EMBEDDED}/prepare_embedded_r_main.py")
set(P4_QUAKE_R_MAIN_VENDOR "${P4_QUAKE_SOURCE}/r_main.c")
set(P4_QUAKE_R_MAIN_EMBEDDED
    "${CMAKE_CURRENT_BINARY_DIR}/generated/r_main_embedded.c")
execute_process(
    COMMAND
        "${P4_QUAKE_PYTHON3_EXECUTABLE}"
        "${P4_QUAKE_R_MAIN_PATCHER}"
        --input "${P4_QUAKE_R_MAIN_VENDOR}"
        --output "${P4_QUAKE_R_MAIN_EMBEDDED}"
    RESULT_VARIABLE P4_QUAKE_R_MAIN_PATCH_RESULT
    ERROR_VARIABLE P4_QUAKE_R_MAIN_PATCH_ERROR
)
if(NOT P4_QUAKE_R_MAIN_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR
        "embedded Quake r_main.c preparation failed: "
        "${P4_QUAKE_R_MAIN_PATCH_ERROR}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${P4_QUAKE_R_MAIN_PATCHER}"
    "${P4_QUAKE_R_MAIN_VENDOR}"
)
list(APPEND P4_QUAKE_VENDOR_SOURCES "${P4_QUAKE_R_MAIN_EMBEDDED}")

idf_component_register(
    SRCS
        "${P4_QUAKE_EMBEDDED}/quake_embedded.c"
        "${P4_QUAKE_EMBEDDED}/quake_sys_embedded.c"
        ${P4_QUAKE_VENDOR_SOURCES}
    INCLUDE_DIRS
        "${P4_QUAKE_EMBEDDED}/include"
        "${P4_QUAKE_SOURCE}"
    PRIV_INCLUDE_DIRS "${P4_QUAKE_EMBEDDED}"
    LDFRAGMENTS "linker.lf"
    REQUIRES
        esp_timer
        freertos
        heap
        log
        p4_game_platform
        platform_board
        platform_display
        platform_touch
)

if(NOT CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
    message(FATAL_ERROR
        "p4_quake requires CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y")
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
    # ESP-IDF 5.5.3 public RISC-V inline headers contain two signedness
    # conversions; retain the remaining conversion diagnostics.
    -Wno-sign-conversion
)
set_source_files_properties(${P4_QUAKE_VENDOR_SOURCES} PROPERTIES
    COMPILE_OPTIONS
        "-w;-include;${P4_QUAKE_EMBEDDED}/quake_embedded_compat.h"
)
