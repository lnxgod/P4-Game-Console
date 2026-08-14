# SPDX-License-Identifier: MIT

set(P4_MAZE_CHASE_SOURCES "src/maze_chase.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_MAZE_CHASE_SOURCES}
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src"
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_maze_chase VERSION 0.1.0 LANGUAGES C)

    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(maze_chase STATIC ${P4_MAZE_CHASE_SOURCES})
    target_include_directories(maze_chase PUBLIC include PRIVATE src)
    target_link_libraries(maze_chase PUBLIC p4_game_api)
    target_compile_features(maze_chase PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(maze_chase PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(maze_chase PRIVATE
            -fsanitize=address,undefined
        )
    endif()

    include(CTest)
    add_subdirectory(tests)
    add_subdirectory(tools)
endif()
