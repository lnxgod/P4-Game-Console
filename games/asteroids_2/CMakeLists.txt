# SPDX-License-Identifier: MIT

set(P4_ASTEROIDS_2_SOURCES "src/asteroids_2.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_ASTEROIDS_2_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_asteroids_2 VERSION 0.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(asteroids_2 STATIC ${P4_ASTEROIDS_2_SOURCES})
    target_link_libraries(asteroids_2 PUBLIC p4_game_api)
    target_compile_features(asteroids_2 PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(asteroids_2 PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
endif()
