add_executable(texas_holdem_tests test_texas_holdem.c)
target_include_directories(texas_holdem_tests PRIVATE ../src)
target_link_libraries(texas_holdem_tests PRIVATE texas_holdem p4_game_api)
target_compile_features(texas_holdem_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(texas_holdem_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(texas_holdem_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME texas_holdem_tests COMMAND texas_holdem_tests)
set_tests_properties(texas_holdem_tests PROPERTIES
    ENVIRONMENT
        "ASAN_OPTIONS=halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1"
)

add_executable(texas_holdem_presentation_tests test_presentation.c)
target_link_libraries(texas_holdem_presentation_tests PRIVATE texas_holdem p4_game_api)
target_compile_features(texas_holdem_presentation_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(texas_holdem_presentation_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(texas_holdem_presentation_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME texas_holdem_presentation_tests COMMAND texas_holdem_presentation_tests)
