add_executable(p4_rummy_tests test_p4_rummy.c)
target_include_directories(p4_rummy_tests PRIVATE ../src)
target_link_libraries(p4_rummy_tests PRIVATE p4_rummy p4_game_api)
target_compile_features(p4_rummy_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_rummy_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_rummy_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_rummy_tests COMMAND p4_rummy_tests)

add_executable(p4_rummy_presentation_tests test_presentation.c)
target_link_libraries(p4_rummy_presentation_tests PRIVATE p4_rummy p4_game_api)
target_compile_features(p4_rummy_presentation_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_rummy_presentation_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(p4_rummy_presentation_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_rummy_presentation_tests COMMAND p4_rummy_presentation_tests)

add_executable(p4_rummy_touch_drag_tests test_touch_drag.c)
target_include_directories(p4_rummy_touch_drag_tests PRIVATE ../src)
target_link_libraries(p4_rummy_touch_drag_tests PRIVATE p4_rummy p4_game_api)
target_compile_features(p4_rummy_touch_drag_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_rummy_touch_drag_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(p4_rummy_touch_drag_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_rummy_touch_drag_tests COMMAND p4_rummy_touch_drag_tests)
