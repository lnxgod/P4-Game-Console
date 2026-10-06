# SPDX-License-Identifier: MIT

set(P4_SPACE_INVADERS_SOURCES "src/space_invaders.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_SPACE_INVADERS_SOURCES}
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
    project(p4_space_invaders VERSION 1.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(space_invaders STATIC ${P4_SPACE_INVADERS_SOURCES})
    target_include_directories(space_invaders PUBLIC include PRIVATE src)
    target_link_libraries(space_invaders PUBLIC p4_game_api)
    target_compile_features(space_invaders PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(space_invaders PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(space_invaders PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
    add_subdirectory(tools)
    add_executable(space_invaders_hires_preview tools/render_hires.c)
    target_link_libraries(space_invaders_hires_preview PRIVATE space_invaders p4_game_api)
    target_compile_features(space_invaders_hires_preview PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(space_invaders_hires_preview PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
        target_link_options(space_invaders_hires_preview PRIVATE -fsanitize=address,undefined)
    endif()
endif()
