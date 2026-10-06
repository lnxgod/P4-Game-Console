cmake_minimum_required(VERSION 3.16)
project(tab5_usb_power_tests LANGUAGES C)
add_executable(tab5_usb_power_tests ../test_usb_power.c ../../src/usb_power_control.c)
target_compile_features(tab5_usb_power_tests PRIVATE c_std_11)
target_compile_options(tab5_usb_power_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(tab5_usb_power_tests PRIVATE -fsanitize=address,undefined)
enable_testing()
add_test(NAME tab5_usb_power_tests COMMAND tab5_usb_power_tests)
