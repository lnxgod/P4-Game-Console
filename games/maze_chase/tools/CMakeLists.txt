add_executable(maze_chase_preview render_preview.c)
target_link_libraries(maze_chase_preview PRIVATE maze_chase p4_game_api)
target_compile_features(maze_chase_preview PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(maze_chase_preview PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(maze_chase_preview PRIVATE
        -fsanitize=address,undefined
    )
endif()
