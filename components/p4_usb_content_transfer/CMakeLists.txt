# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
idf_component_register(
    SRCS
        "src/content_transfer.c"
        "src/file_transfer.c"
        "src/h1_usb_drive_control.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_timer
        esp_system
        freertos
        heap
        log
        mbedtls
        p4_content_catalog
        p4_game_package
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_usb_content_transfer LANGUAGES C)
    add_library(p4_h1_usb_drive_control STATIC
        src/h1_usb_drive_control.c
    )
    target_include_directories(p4_h1_usb_drive_control PUBLIC include)
    target_compile_features(p4_h1_usb_drive_control PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_h1_usb_drive_control PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_h1_usb_drive_control PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
