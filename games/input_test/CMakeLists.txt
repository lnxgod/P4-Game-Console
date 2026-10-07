# SPDX-License-Identifier: MIT

set(P4_INPUT_TEST_SOURCES "src/input_test.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_INPUT_TEST_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_input_test VERSION 1.0.3 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(input_test STATIC ${P4_INPUT_TEST_SOURCES})
    target_link_libraries(input_test PUBLIC p4_game_api)
    target_compile_features(input_test PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(input_test PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(input_test PRIVATE
            -fsanitize=address,undefined
        )
    endif()
endif()
