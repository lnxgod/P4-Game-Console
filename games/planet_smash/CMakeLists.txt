# SPDX-License-Identifier: MIT

set(P4_PLANET_SMASH_SOURCES "src/planet_smash.c")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_PLANET_SMASH_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_planet_smash VERSION 0.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(planet_smash STATIC ${P4_PLANET_SMASH_SOURCES})
    target_link_libraries(planet_smash PUBLIC p4_game_api)
    target_compile_features(planet_smash PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(planet_smash PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    if(BUILD_TESTING)
        add_executable(planet_smash_tests tests/test_planet_smash.c)
        target_link_libraries(planet_smash_tests PRIVATE p4_game_api)
        target_compile_features(planet_smash_tests PRIVATE c_std_11)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(planet_smash_tests PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(planet_smash_tests PRIVATE
                -fsanitize=address,undefined
            )
        endif()
        add_test(NAME planet_smash_tests COMMAND planet_smash_tests)
    endif()
endif()
