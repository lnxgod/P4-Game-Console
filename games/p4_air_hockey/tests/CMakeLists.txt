add_executable(p4_air_hockey_tests test_p4_air_hockey.c)
target_include_directories(p4_air_hockey_tests PRIVATE ../src)
target_link_libraries(p4_air_hockey_tests PRIVATE p4_air_hockey p4_game_api)
target_compile_features(p4_air_hockey_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_air_hockey_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_air_hockey_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_air_hockey_tests COMMAND p4_air_hockey_tests)
