# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/doom_multiplayer.c"
        INCLUDE_DIRS "include"
        REQUIRES p4_multiplayer
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_doom_multiplayer VERSION 0.1.0 LANGUAGES C)

    if(NOT TARGET p4_multiplayer)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../p4_multiplayer"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_multiplayer"
        )
    endif()
    add_library(doom_multiplayer STATIC src/doom_multiplayer.c)
    target_include_directories(doom_multiplayer PUBLIC include)
    target_link_libraries(doom_multiplayer PUBLIC p4_multiplayer)
    target_compile_features(doom_multiplayer PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(doom_multiplayer PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(doom_multiplayer PRIVATE
            -fsanitize=address,undefined
        )
    endif()

    include(CTest)
    add_subdirectory(tests)
endif()
