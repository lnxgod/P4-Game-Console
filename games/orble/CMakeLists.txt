# SPDX-License-Identifier: MIT

set(P4_ORBLE_SOURCES "src/orble.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_ORBLE_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_orble VERSION 0.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(orble STATIC ${P4_ORBLE_SOURCES})
    target_link_libraries(orble PUBLIC p4_game_api)
    target_compile_features(orble PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(orble PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    if(BUILD_TESTING)
        add_executable(orble_tests tests/test_orble.c)
        target_link_libraries(orble_tests PRIVATE p4_game_api)
        target_compile_features(orble_tests PRIVATE c_std_11)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(orble_tests PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(orble_tests PRIVATE
                -fsanitize=address,undefined
            )
        endif()
        add_test(NAME orble_tests COMMAND orble_tests)
    endif()
endif()
