# SPDX-License-Identifier: MIT

set(P4_CHECKERS_SOURCES
    "src/checkers.c"
    "src/checkers_logic.c"
    "src/checkers_network.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_CHECKERS_SOURCES}
        PRIV_INCLUDE_DIRS "src"
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_checkers VERSION 0.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(checkers STATIC ${P4_CHECKERS_SOURCES})
    target_include_directories(checkers PRIVATE src)
    target_link_libraries(checkers PUBLIC p4_game_api)
    target_compile_features(checkers PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(checkers PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
