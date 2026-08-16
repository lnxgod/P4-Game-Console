# SPDX-License-Identifier: MIT

add_executable(byte_buddy_tests test_byte_buddy.c)
target_link_libraries(byte_buddy_tests PRIVATE byte_buddy p4_game_api)
target_compile_features(byte_buddy_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(byte_buddy_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(byte_buddy_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME byte_buddy_tests COMMAND byte_buddy_tests)
