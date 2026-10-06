# SPDX-License-Identifier: MIT

set(P4_BYTE_BUDDY_SOURCES
    "src/byte_buddy.c"
    "src/byte_buddy_save.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_BYTE_BUDDY_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_byte_buddy VERSION 5.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(byte_buddy STATIC ${P4_BYTE_BUDDY_SOURCES})
    target_link_libraries(byte_buddy PUBLIC p4_game_api)
    target_compile_features(byte_buddy PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(byte_buddy PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
