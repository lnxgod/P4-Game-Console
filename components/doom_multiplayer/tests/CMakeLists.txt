# SPDX-License-Identifier: MIT

add_executable(doom_multiplayer_tests test_doom_multiplayer.c)
target_link_libraries(doom_multiplayer_tests PRIVATE doom_multiplayer)
target_compile_features(doom_multiplayer_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(doom_multiplayer_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(doom_multiplayer_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME doom_multiplayer_tests COMMAND doom_multiplayer_tests)
