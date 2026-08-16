# SPDX-License-Identifier: MIT

set(P4_SKYLINE_LEAP_SOURCES "src/skyline_leap.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_SKYLINE_LEAP_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_skyline_leap VERSION 0.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(skyline_leap STATIC ${P4_SKYLINE_LEAP_SOURCES})
    target_link_libraries(skyline_leap PUBLIC p4_game_api)
    target_compile_features(skyline_leap PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(skyline_leap PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
    add_executable(skyline_leap_preview tools/render_preview.c)
    target_link_libraries(skyline_leap_preview PRIVATE skyline_leap p4_game_api)
    target_compile_features(skyline_leap_preview PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(skyline_leap_preview PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        )
        target_link_options(skyline_leap_preview PRIVATE
            -fsanitize=address,undefined
        )
    endif()
endif()
