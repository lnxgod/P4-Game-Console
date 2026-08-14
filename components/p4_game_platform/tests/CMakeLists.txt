add_executable(p4_game_platform_tests test_audio_session.c)
target_include_directories(p4_game_platform_tests PRIVATE mocks)
target_link_libraries(p4_game_platform_tests PRIVATE p4_game_platform)
target_compile_features(p4_game_platform_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_game_platform_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_game_platform_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_game_platform_tests COMMAND p4_game_platform_tests)
