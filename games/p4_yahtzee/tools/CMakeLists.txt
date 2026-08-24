add_executable(p4_yahtzee_preview render_preview.c)
target_include_directories(p4_yahtzee_preview PRIVATE ../src)
target_link_libraries(p4_yahtzee_preview PRIVATE p4_yahtzee p4_game_api)
target_compile_features(p4_yahtzee_preview PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_yahtzee_preview PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_yahtzee_preview PRIVATE
        -fsanitize=address,undefined
    )
endif()
