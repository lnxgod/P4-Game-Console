add_executable(solitaire_tests test_solitaire.c)
target_include_directories(solitaire_tests PRIVATE ../src)
target_link_libraries(solitaire_tests PRIVATE solitaire p4_game_api)
target_compile_features(solitaire_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(solitaire_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(solitaire_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME solitaire_tests COMMAND solitaire_tests)
set_tests_properties(solitaire_tests PROPERTIES
    ENVIRONMENT
        "ASAN_OPTIONS=halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1"
)

add_executable(solitaire_presentation_tests test_presentation.c)
target_link_libraries(solitaire_presentation_tests PRIVATE solitaire p4_game_api)
target_compile_features(solitaire_presentation_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(solitaire_presentation_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(solitaire_presentation_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME solitaire_presentation_tests COMMAND solitaire_presentation_tests)
