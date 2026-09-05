add_executable(console_shell_preview render_preview.c)
target_link_libraries(console_shell_preview PRIVATE console_shell)
target_compile_features(console_shell_preview PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_shell_preview PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(console_shell_preview PRIVATE
        -fsanitize=address,undefined
    )
endif()

add_executable(console_shell_bbs_preview
    render_preview.c
    ../src/console_shell.c
)
target_include_directories(console_shell_bbs_preview PRIVATE ../include)
target_link_libraries(console_shell_bbs_preview PRIVATE
    p4_bbs
    p4_desktop
    p4_game_api
)
target_compile_definitions(console_shell_bbs_preview PRIVATE
    CONSOLE_SHELL_TARGET_WIDTH=800U
    CONSOLE_SHELL_TARGET_HEIGHT=480U
)
target_compile_features(console_shell_bbs_preview PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_shell_bbs_preview PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(console_shell_bbs_preview PRIVATE
        -fsanitize=address,undefined
    )
endif()

add_executable(console_shell_waveshare_preview
    render_preview.c
    ../src/console_shell.c
)
target_include_directories(console_shell_waveshare_preview PRIVATE ../include)
target_link_libraries(console_shell_waveshare_preview PRIVATE
    p4_bbs
    p4_desktop
    p4_game_api
)
target_compile_definitions(console_shell_waveshare_preview PRIVATE
    CONSOLE_SHELL_TARGET_WIDTH=800U
    CONSOLE_SHELL_TARGET_HEIGHT=480U
    CONSOLE_SHELL_FORCE_WINDOWS=1
)
target_compile_features(console_shell_waveshare_preview PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(console_shell_waveshare_preview PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(console_shell_waveshare_preview PRIVATE
        -fsanitize=address,undefined
    )
endif()
