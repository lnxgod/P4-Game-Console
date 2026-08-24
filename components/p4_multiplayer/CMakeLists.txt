# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            "src/packet.c"
            "src/lobby.c"
            "src/start.c"
            "src/session.c"
            "src/stream.c"
            "src/ble_frame.c"
            "src/uart_endpoint_dual.c"
        INCLUDE_DIRS "include"
        REQUIRES
            esp_driver_uart
            freertos
            log
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_multiplayer VERSION 0.1.0 LANGUAGES C)

    add_library(p4_multiplayer STATIC
        src/packet.c
        src/lobby.c
        src/start.c
        src/session.c
        src/stream.c
        src/ble_frame.c
    )
    target_include_directories(p4_multiplayer PUBLIC include)
    target_compile_features(p4_multiplayer PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_multiplayer PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_multiplayer PRIVATE
            -fsanitize=address,undefined
        )
    endif()

    include(CTest)
    add_subdirectory(tests)
endif()
