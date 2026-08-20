# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS src/cp437.c
        INCLUDE_DIRS include
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        cmake_minimum_required(VERSION 3.16)
        project(p4_cp437 VERSION 0.1.0 LANGUAGES C)
    endif()
    add_library(p4_cp437 STATIC src/cp437.c)
    target_include_directories(p4_cp437 PUBLIC include)
    target_compile_features(p4_cp437 PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_cp437 PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_cp437 PRIVATE
            -fsanitize=address,undefined
        )
    endif()
endif()
