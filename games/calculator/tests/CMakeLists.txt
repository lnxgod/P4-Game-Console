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

# All three utility views use the same native geometry contract. Link their
# actual renderers so low-resolution regressions and touch-layout drift fail.
add_executable(test_utility_native test_utility_native.c
    "../../av_test/src/av_test.c"
    "../../input_test/src/input_test.c")
target_include_directories(test_utility_native PRIVATE "../src")
target_link_libraries(test_utility_native PRIVATE calculator p4_game_api)
target_compile_features(test_utility_native PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_utility_native PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_utility_native PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME utility_native_tests COMMAND test_utility_native)
