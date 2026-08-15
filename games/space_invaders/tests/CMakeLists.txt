add_executable(space_invaders_tests test_space_invaders.c)
target_include_directories(space_invaders_tests PRIVATE ../src)
target_link_libraries(space_invaders_tests PRIVATE space_invaders p4_game_api)
target_compile_features(space_invaders_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(space_invaders_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(space_invaders_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME space_invaders_tests COMMAND space_invaders_tests)
