cmake_minimum_required(VERSION 3.16)
project(platform_i2c_shared_tests C)

enable_testing()

add_executable(platform_i2c_shared_tests
    test_platform_i2c_shared.c
    ../src/bus.c
)
target_include_directories(platform_i2c_shared_tests PRIVATE
    mocks
    ../include
)
target_compile_features(platform_i2c_shared_tests PRIVATE c_std_11)
target_compile_options(platform_i2c_shared_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer
)
target_link_options(platform_i2c_shared_tests PRIVATE
    -fsanitize=address,undefined
)

add_test(NAME platform_i2c_shared COMMAND platform_i2c_shared_tests)
