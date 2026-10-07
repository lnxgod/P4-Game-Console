# SPDX-License-Identifier: MIT

set(P4_COLOR_CLASH_SOURCES
    "src/color_clash.c"
    "src/color_clash_logic.c"
    "src/color_clash_network.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_COLOR_CLASH_SOURCES}
        PRIV_INCLUDE_DIRS "src"
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_color_clash VERSION 1.8.2 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(color_clash STATIC ${P4_COLOR_CLASH_SOURCES})
    target_include_directories(color_clash PRIVATE src)
    target_link_libraries(color_clash PUBLIC p4_game_api)
    target_compile_features(color_clash PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(color_clash PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
