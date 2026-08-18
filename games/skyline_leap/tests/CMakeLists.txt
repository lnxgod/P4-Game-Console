add_executable(skyline_leap_tests test_skyline_leap.c)
target_link_libraries(skyline_leap_tests PRIVATE skyline_leap p4_game_api)
target_compile_features(skyline_leap_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(skyline_leap_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(skyline_leap_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME skyline_leap_tests COMMAND skyline_leap_tests)

add_executable(skyline_leap_reachability_tests test_reachability.c)
target_link_libraries(skyline_leap_reachability_tests PRIVATE p4_game_api)
target_compile_features(skyline_leap_reachability_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(skyline_leap_reachability_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(skyline_leap_reachability_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME skyline_leap_reachability_tests
    COMMAND skyline_leap_reachability_tests)
