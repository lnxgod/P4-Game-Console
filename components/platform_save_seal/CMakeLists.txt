# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS src/platform_save_seal.c
        INCLUDE_DIRS include
        REQUIRES esp_hw_support mbedtls nvs_flash p4_game_save
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_save_seal VERSION 0.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../p4_game_save"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_save"
    )
    add_library(platform_save_seal STATIC src/platform_save_seal.c)
    target_include_directories(platform_save_seal
        PUBLIC include
        PRIVATE tests/stubs
    )
    target_link_libraries(platform_save_seal PUBLIC p4_game_save)
    target_compile_features(platform_save_seal PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_save_seal PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(platform_save_seal PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
