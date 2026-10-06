# SPDX-License-Identifier: MIT

set(P4_TIDE_MAZE_SOURCES "src/tide_maze.c" "src/tide_maze_physics.c" "src/tide_maze_network.c" "src/tide_maze_render.c" "src/tide_maze_water.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_TIDE_MAZE_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_tide_maze VERSION 0.3.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    include(CTest)
    add_subdirectory(tests)
    add_library(tide_maze STATIC ${P4_TIDE_MAZE_SOURCES})
    target_link_libraries(tide_maze PUBLIC p4_game_api)
    target_compile_features(tide_maze PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(tide_maze PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
endif()
