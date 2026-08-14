cmake_minimum_required(VERSION 3.16)
project(doom_gamepad_input_tests C)

enable_testing()

add_executable(test_doom_gamepad_input
    test_doom_gamepad_input.c
    ../src/input.c
    ../../gamepad_core/src/gamepad.c
)
target_include_directories(test_doom_gamepad_input PRIVATE
    ../include
    ../../gamepad_core/include
)
target_compile_features(test_doom_gamepad_input PRIVATE c_std_11)
target_compile_options(test_doom_gamepad_input PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
)
target_link_options(test_doom_gamepad_input PRIVATE
    -fsanitize=address,undefined
)

add_test(NAME doom_gamepad_input COMMAND test_doom_gamepad_input)
