# SPDX-License-Identifier: LicenseRef-LORD-Permission

set(P4_LORD_SOURCES
    "src/lord.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_LORD_SOURCES}
        INCLUDE_DIRS "src"
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_lord VERSION 1.8.1 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(lord STATIC ${P4_LORD_SOURCES})
    target_include_directories(lord PUBLIC src)
    target_link_libraries(lord PUBLIC p4_game_api)
    target_compile_features(lord PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(lord PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
