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

add_executable(platform_touch_frame_waveshare_tests
    test_platform_touch_frame.c
    ../src/platform_touch_frame.c
)
target_include_directories(platform_touch_frame_waveshare_tests PRIVATE
    ../host/include
    ../include
    ../src
    ../../platform_board/include
)
target_compile_definitions(platform_touch_frame_waveshare_tests PRIVATE
    CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3=1
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3=1
)
target_compile_features(platform_touch_frame_waveshare_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_touch_frame_waveshare_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_touch_frame_waveshare_tests PRIVATE
        -fsanitize=address,undefined)
endif()
add_test(NAME platform_touch_frame_waveshare_tests
         COMMAND platform_touch_frame_waveshare_tests)

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

add_executable(platform_touch_frame_tab5_tests
    test_platform_touch_frame.c
    ../src/platform_touch_frame.c
)
target_include_directories(platform_touch_frame_tab5_tests PRIVATE
    ../host/include
    ../include
    ../src
    ../../platform_board/include
)
target_compile_definitions(platform_touch_frame_tab5_tests PRIVATE
    CONFIG_P4_BOARD_M5STACK_TAB5=1
)
target_compile_features(platform_touch_frame_tab5_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_touch_frame_tab5_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_touch_frame_tab5_tests PRIVATE
        -fsanitize=address,undefined)
endif()
add_test(NAME platform_touch_frame_tab5_tests
         COMMAND platform_touch_frame_tab5_tests)

add_executable(platform_touch_tab5_runtime_tests
    test_platform_touch_tab5.c ../src/platform_touch_tab5.c ../src/platform_touch_frame.c)
target_include_directories(platform_touch_tab5_runtime_tests PRIVATE
    tab5_mocks ../host/include ../include ../src ../../platform_board/include)
target_compile_definitions(platform_touch_tab5_runtime_tests PRIVATE CONFIG_P4_BOARD_M5STACK_TAB5=1)
target_compile_features(platform_touch_tab5_runtime_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_touch_tab5_runtime_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_touch_tab5_runtime_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME platform_touch_tab5_runtime_tests COMMAND platform_touch_tab5_runtime_tests)
