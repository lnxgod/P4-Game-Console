# SPDX-License-Identifier: MIT

set(P4_GAME_API_SOURCES
    "src/achievements.c"
    "src/audio_mixer.c"
    "src/audio_pack.c"
    "src/draw.c"
    "src/feedback.c"
    "src/game_runtime.c"
    "src/input.c"
    "src/presentation_font.c"
    "src/visual.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_GAME_API_SOURCES}
        INCLUDE_DIRS "include"
        REQUIRES platform_board p4_cp437
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        cmake_minimum_required(VERSION 3.16)
        project(p4_game_api VERSION 1.0.0 LANGUAGES C)
    endif()

    if(NOT TARGET p4_cp437)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../p4_cp437"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_cp437"
        )
    endif()

    add_library(p4_game_api STATIC ${P4_GAME_API_SOURCES})
    target_include_directories(p4_game_api PUBLIC include)
    target_link_libraries(p4_game_api PUBLIC p4_cp437)
    target_compile_features(p4_game_api PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_game_api PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_game_api PRIVATE
            -fsanitize=address,undefined
        )
    endif()

    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        include(CTest)
        add_subdirectory(tests)
    endif()
endif()
