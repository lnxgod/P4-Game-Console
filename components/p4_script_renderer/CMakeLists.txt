# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS src/script_renderer.c
        INCLUDE_DIRS include
        REQUIRES p4_cp437 p4_game_runtime_core
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_script_renderer VERSION 0.1.0 LANGUAGES C)

    if(NOT TARGET p4_cp437)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../p4_cp437"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_cp437"
        )
    endif()
    if(NOT TARGET p4_game_runtime_core)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../../game-platform/firmware/components/p4_game_runtime_core"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_game_runtime_core"
        )
    endif()

    add_library(p4_script_renderer STATIC src/script_renderer.c)
    target_include_directories(p4_script_renderer PUBLIC include)
    target_link_libraries(
        p4_script_renderer PUBLIC p4_cp437 p4_game_runtime_core)
    target_compile_features(p4_script_renderer PUBLIC c_std_11)
    target_compile_options(p4_script_renderer PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Werror
    )
endif()
