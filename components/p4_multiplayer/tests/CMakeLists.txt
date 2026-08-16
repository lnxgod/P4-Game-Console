# SPDX-License-Identifier: MIT

add_executable(p4_multiplayer_tests test_p4_multiplayer.c)
target_link_libraries(p4_multiplayer_tests PRIVATE p4_multiplayer)
target_compile_features(p4_multiplayer_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_multiplayer_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_multiplayer_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_multiplayer_tests COMMAND p4_multiplayer_tests)
