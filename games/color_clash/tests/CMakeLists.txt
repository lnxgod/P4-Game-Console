add_executable(color_clash_tests test_color_clash.c)
target_include_directories(color_clash_tests PRIVATE ../src)
target_link_libraries(color_clash_tests PRIVATE color_clash p4_game_api)
target_compile_features(color_clash_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(color_clash_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(color_clash_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME color_clash_tests COMMAND color_clash_tests)

add_executable(color_clash_presentation_tests test_presentation.c)
target_link_libraries(color_clash_presentation_tests PRIVATE color_clash p4_game_api)
target_compile_features(color_clash_presentation_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(color_clash_presentation_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(color_clash_presentation_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME color_clash_presentation_tests COMMAND color_clash_presentation_tests)

add_executable(color_clash_touch_drag_tests test_touch_drag.c)
target_include_directories(color_clash_touch_drag_tests PRIVATE ../src)
target_link_libraries(color_clash_touch_drag_tests PRIVATE color_clash p4_game_api)
target_compile_features(color_clash_touch_drag_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(color_clash_touch_drag_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(color_clash_touch_drag_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME color_clash_touch_drag_tests COMMAND color_clash_touch_drag_tests)
