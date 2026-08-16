add_executable(space_invaders_preview render_preview.c)
target_link_libraries(space_invaders_preview PRIVATE
    space_invaders p4_game_api)
target_compile_features(space_invaders_preview PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(space_invaders_preview PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(space_invaders_preview PRIVATE
        -fsanitize=address,undefined
    )
endif()
