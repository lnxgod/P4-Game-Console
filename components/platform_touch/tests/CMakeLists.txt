add_executable(platform_touch_frame_tests test_platform_touch_frame.c)
target_link_libraries(platform_touch_frame_tests PRIVATE platform_touch_frame)
target_include_directories(platform_touch_frame_tests PRIVATE ../src)
target_compile_features(platform_touch_frame_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_touch_frame_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_touch_frame_tests PRIVATE
        -fsanitize=address,undefined)
endif()
add_test(NAME platform_touch_frame_tests COMMAND platform_touch_frame_tests)

add_executable(platform_touch_runtime_tests
    test_platform_touch_runtime.c
    ../src/platform_touch.c
)
target_link_libraries(platform_touch_runtime_tests PRIVATE platform_touch_frame)
target_include_directories(platform_touch_runtime_tests PRIVATE
    ../host/include
    ../include
    ../src
)
target_compile_features(platform_touch_runtime_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_touch_runtime_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_touch_runtime_tests PRIVATE
        -fsanitize=address,undefined)
endif()
add_test(NAME platform_touch_runtime_tests COMMAND platform_touch_runtime_tests)
