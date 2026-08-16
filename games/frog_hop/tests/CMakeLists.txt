add_executable(frog_hop_tests test_frog_hop.c)
target_link_libraries(frog_hop_tests PRIVATE frog_hop p4_game_api)
target_compile_features(frog_hop_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(frog_hop_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(frog_hop_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME frog_hop_tests COMMAND frog_hop_tests)
