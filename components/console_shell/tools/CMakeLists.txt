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
