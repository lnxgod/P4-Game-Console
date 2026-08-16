add_executable(console_shell_tests test_console_shell.c)
target_link_libraries(console_shell_tests PRIVATE console_shell)
target_compile_features(console_shell_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_shell_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(console_shell_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()

add_test(NAME console_shell_tests COMMAND console_shell_tests)

add_executable(console_shell_waveshare_tests
    test_console_shell.c
    ../src/console_shell.c
)
target_include_directories(console_shell_waveshare_tests PRIVATE ../include)
target_link_libraries(console_shell_waveshare_tests PRIVATE
    p4_game_api
)
target_compile_definitions(console_shell_waveshare_tests PRIVATE
    CONSOLE_SHELL_TARGET_WIDTH=800U
    CONSOLE_SHELL_TARGET_HEIGHT=480U
)
target_compile_features(console_shell_waveshare_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_shell_waveshare_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(console_shell_waveshare_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME console_shell_waveshare_tests
         COMMAND console_shell_waveshare_tests)
