# SPDX-License-Identifier: MIT

add_executable(test_calculator test_calculator.c)
target_include_directories(test_calculator PRIVATE "../src")
target_link_libraries(test_calculator PRIVATE calculator p4_game_api)
target_compile_features(test_calculator PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_calculator PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_calculator PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME calculator_tests COMMAND test_calculator)
