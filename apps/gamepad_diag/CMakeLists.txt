cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(p4_gamepad_diag)

get_property(P4_GAMEPAD_DIAG_LINK_ARM_TOKEN_SHA256 GLOBAL PROPERTY
    P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX)
if(P4_GAMEPAD_DIAG_LINK_ARM_TOKEN_SHA256 STREQUAL "")
    message(FATAL_ERROR
        "gamepad_diag component did not publish its resolved arm digest")
endif()

add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}.elf
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DP4_LINK_NM=${CMAKE_NM}"
        "-DP4_LINK_ELF=$<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>"
        -P "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_usb_host/cmake/verify_native_gamepad_link.cmake"
    COMMAND "${CMAKE_COMMAND}"
        "-DP4_LINK_NM=${CMAKE_NM}"
        "-DP4_LINK_ELF=$<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>"
        "-DP4_LINK_ARM_TOKEN_SHA256=${P4_GAMEPAD_DIAG_LINK_ARM_TOKEN_SHA256}"
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/verify_arm_link.cmake"
    VERBATIM
)
