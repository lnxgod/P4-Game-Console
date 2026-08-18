# SPDX-License-Identifier: LicenseRef-LORD-Permission

add_executable(lord_tests test_lord.c)
target_link_libraries(lord_tests PRIVATE lord p4_game_api)
target_compile_features(lord_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lord_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(lord_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME lord_tests COMMAND lord_tests)
set_tests_properties(lord_tests PROPERTIES
    ENVIRONMENT
        "ASAN_OPTIONS=halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1"
)
