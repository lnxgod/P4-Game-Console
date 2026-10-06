add_executable(p4_yahtzee_tests test_p4_yahtzee.c)
target_include_directories(p4_yahtzee_tests PRIVATE ../src)
target_link_libraries(p4_yahtzee_tests PRIVATE p4_yahtzee p4_game_api)
target_compile_features(p4_yahtzee_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_yahtzee_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_yahtzee_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_yahtzee_tests COMMAND p4_yahtzee_tests)

add_executable(p4_yahtzee_presentation_tests test_presentation.c)
target_include_directories(p4_yahtzee_presentation_tests PRIVATE ../src)
target_link_libraries(p4_yahtzee_presentation_tests PRIVATE p4_yahtzee p4_game_api)
target_compile_features(p4_yahtzee_presentation_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(p4_yahtzee_presentation_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(p4_yahtzee_presentation_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_yahtzee_presentation_tests COMMAND p4_yahtzee_presentation_tests)
