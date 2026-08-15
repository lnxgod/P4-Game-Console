add_executable(test_p4_game_package test_game_package.c)
target_link_libraries(test_p4_game_package PRIVATE p4_game_package)
target_compile_features(test_p4_game_package PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_p4_game_package PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_p4_game_package PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_game_package COMMAND test_p4_game_package)
