if(P4_CONSOLE_HOST_TAB5)
    add_executable(console_shell_tests test_nextgen.c)
else()
    add_executable(console_shell_tests test_console_shell.c)
endif()
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
    p4_bbs
    p4_desktop
    p4_game_api
)
target_compile_definitions(console_shell_waveshare_tests PRIVATE
    # The panel is 800x480; its centered Console OS viewport is 768x480.
    CONSOLE_SHELL_TARGET_WIDTH=800U
    CONSOLE_SHELL_TARGET_HEIGHT=480U
    # Keep the interaction suite on the default Windows renderer.
    CONSOLE_SHELL_FORCE_WINDOWS=1
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

add_executable(console_shell_bbs_tests
    test_console_shell_bbs.c
    ../src/console_shell.c
)
target_include_directories(console_shell_bbs_tests PRIVATE ../include)
target_link_libraries(console_shell_bbs_tests PRIVATE
    p4_bbs
    p4_desktop
    p4_game_api
)
target_compile_definitions(console_shell_bbs_tests PRIVATE
    CONSOLE_SHELL_TARGET_WIDTH=800U
    CONSOLE_SHELL_TARGET_HEIGHT=480U
)
target_compile_features(console_shell_bbs_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_shell_bbs_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(console_shell_bbs_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME console_shell_bbs_tests COMMAND console_shell_bbs_tests)

add_executable(console_shell_tab5_tests
    test_nextgen.c
    ../src/console_shell.c
)
target_include_directories(console_shell_tab5_tests PRIVATE ../include)
target_link_libraries(console_shell_tab5_tests PRIVATE
    p4_bbs
    p4_desktop
    p4_game_api
)
target_compile_definitions(console_shell_tab5_tests PRIVATE
    # Tab5 OS uses the full 1280x720 landscape panel.
    CONSOLE_SHELL_TARGET_WIDTH=1280U
    CONFIG_P4_BOARD_M5STACK_TAB5=1
    CONSOLE_SHELL_TARGET_HEIGHT=720U
)
target_compile_features(console_shell_tab5_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_shell_tab5_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(console_shell_tab5_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME console_shell_tab5_tests
         COMMAND console_shell_tab5_tests)
