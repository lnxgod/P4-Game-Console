# SPDX-License-Identifier: MIT

set(P4_GAME_PACKAGE_SOURCES
    "src/game_package.c"
    "src/game_resource.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_GAME_PACKAGE_SOURCES}
        INCLUDE_DIRS "include"
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_game_package VERSION 1.0.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(p4_game_package STATIC ${P4_GAME_PACKAGE_SOURCES})
    target_include_directories(p4_game_package PUBLIC include)
    target_link_libraries(p4_game_package PUBLIC p4_game_api)
    target_compile_features(p4_game_package PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_game_package PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_game_package PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
