add_executable(checkers_tests test_checkers.c)
target_include_directories(checkers_tests PRIVATE ../src)
target_link_libraries(checkers_tests PRIVATE checkers p4_game_api)
target_compile_features(checkers_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(checkers_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(checkers_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME checkers_tests COMMAND checkers_tests)
