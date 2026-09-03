# SPDX-License-Identifier: MIT

set(P4_BLE_RADIO_HANDOFF_SOURCES
    "src/ble_radio_handoff.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_BLE_RADIO_HANDOFF_SOURCES}
        INCLUDE_DIRS "include"
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_ble_radio_handoff VERSION 0.1.0 LANGUAGES C)

    add_library(p4_ble_radio_handoff STATIC
        ${P4_BLE_RADIO_HANDOFF_SOURCES}
    )
    target_include_directories(p4_ble_radio_handoff PUBLIC include)
    target_compile_features(p4_ble_radio_handoff PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_ble_radio_handoff PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_ble_radio_handoff PRIVATE
            -fsanitize=address,undefined
        )
    endif()

    include(CTest)
    add_subdirectory(tests)
endif()
