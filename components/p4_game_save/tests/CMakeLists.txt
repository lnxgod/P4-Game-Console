# SPDX-License-Identifier: MIT

add_executable(p4_game_save_tests test_game_save.c)
target_link_libraries(p4_game_save_tests PRIVATE p4_game_save)
target_compile_features(p4_game_save_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_game_save_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_game_save_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_game_save_tests COMMAND p4_game_save_tests)
