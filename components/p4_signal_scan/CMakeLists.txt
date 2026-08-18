# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/signal_scan.c"
        INCLUDE_DIRS "include"
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        cmake_minimum_required(VERSION 3.16)
        project(p4_signal_scan VERSION 0.1.0 LANGUAGES C)
    endif()
    if(NOT TARGET p4_game_api)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../p4_game_api"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
        )
    endif()
    add_library(p4_signal_scan STATIC "src/signal_scan.c")
    target_include_directories(p4_signal_scan PUBLIC "include")
    target_link_libraries(p4_signal_scan PUBLIC p4_game_api)
    target_compile_features(p4_signal_scan PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_signal_scan PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_signal_scan PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        include(CTest)
        add_subdirectory(tests)
    endif()
endif()
