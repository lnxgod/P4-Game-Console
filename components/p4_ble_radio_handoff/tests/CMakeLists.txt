# SPDX-License-Identifier: MIT

add_executable(p4_ble_radio_handoff_tests
    test_ble_radio_handoff.c
)
target_link_libraries(p4_ble_radio_handoff_tests
    PRIVATE p4_ble_radio_handoff
)
target_compile_features(p4_ble_radio_handoff_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_ble_radio_handoff_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_ble_radio_handoff_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_ble_radio_handoff_tests
         COMMAND p4_ble_radio_handoff_tests)
