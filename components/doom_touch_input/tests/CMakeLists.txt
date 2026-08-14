cmake_minimum_required(VERSION 3.16)
project(doom_touch_input_tests C)

enable_testing()

add_executable(test_doom_touch_input
    test_doom_touch_input.c
    ../src/input.c
    ../src/overlay.c
)
target_include_directories(test_doom_touch_input PRIVATE ../include)
target_compile_features(test_doom_touch_input PRIVATE c_std_11)
target_compile_options(test_doom_touch_input PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer
)
target_link_options(test_doom_touch_input PRIVATE
    -fsanitize=address,undefined
)

add_test(NAME doom_touch_input COMMAND test_doom_touch_input)
