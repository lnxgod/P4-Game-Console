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

add_executable(lord_presentation_tests test_presentation.c)
target_link_libraries(lord_presentation_tests PRIVATE lord p4_game_api)
target_compile_features(lord_presentation_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lord_presentation_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lord_presentation_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME lord_presentation_tests COMMAND lord_presentation_tests)

add_executable(lord_trace_tests test_trace.c)
target_link_libraries(lord_trace_tests PRIVATE lord p4_game_api)
target_compile_features(lord_trace_tests PRIVATE c_std_11)
target_compile_definitions(lord_trace_tests PRIVATE
    LORD_TRACE_PATH="${CMAKE_CURRENT_LIST_DIR}/performance-input.txt")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lord_trace_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lord_trace_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME lord_trace_tests COMMAND lord_trace_tests)
set_tests_properties(lord_presentation_tests lord_trace_tests PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1")
