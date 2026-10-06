# SPDX-License-Identifier: MIT

set(P4_TEXAS_HOLDEM_SOURCES
    "src/texas_holdem.c"
    "src/texas_holdem_logic.c"
    "src/texas_holdem_network.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_TEXAS_HOLDEM_SOURCES}
        PRIV_INCLUDE_DIRS "src"
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_texas_holdem VERSION 1.2.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(texas_holdem STATIC ${P4_TEXAS_HOLDEM_SOURCES})
    target_include_directories(texas_holdem PRIVATE src)
    target_link_libraries(texas_holdem PUBLIC p4_game_api)
    target_compile_features(texas_holdem PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(texas_holdem PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
