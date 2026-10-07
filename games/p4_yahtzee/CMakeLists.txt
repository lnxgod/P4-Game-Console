# SPDX-License-Identifier: MIT

set(P4_P4_YAHTZEE_SOURCES
    "src/p4_yahtzee.c"
    "src/p4_yahtzee_logic.c"
    "src/p4_yahtzee_network.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_P4_YAHTZEE_SOURCES}
        PRIV_INCLUDE_DIRS "src"
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_p4_yahtzee VERSION 1.4.3 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(p4_yahtzee STATIC ${P4_P4_YAHTZEE_SOURCES})
    target_include_directories(p4_yahtzee PRIVATE src)
    target_link_libraries(p4_yahtzee PUBLIC p4_game_api)
    target_compile_features(p4_yahtzee PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_yahtzee PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
    add_subdirectory(tools)
endif()
