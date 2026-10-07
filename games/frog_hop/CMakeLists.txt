# SPDX-License-Identifier: MIT

set(P4_FROG_HOP_SOURCES "src/frog_hop.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_FROG_HOP_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_frog_hop VERSION 1.1.1 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(frog_hop STATIC ${P4_FROG_HOP_SOURCES})
    target_link_libraries(frog_hop PUBLIC p4_game_api)
    target_compile_features(frog_hop PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(frog_hop PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
    add_executable(frog_hop_hires_preview tools/render_hires.c)
    target_link_libraries(frog_hop_hires_preview PRIVATE frog_hop p4_game_api)
    target_compile_features(frog_hop_hires_preview PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(frog_hop_hires_preview PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
        target_link_options(frog_hop_hires_preview PRIVATE -fsanitize=address,undefined)
    endif()
endif()
