add_executable(platform_display_layout_tests test_platform_display_layout.c)
target_link_libraries(platform_display_layout_tests PRIVATE platform_display_layout)
target_compile_features(platform_display_layout_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_display_layout_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )

    option(PLATFORM_DISPLAY_ENABLE_SANITIZERS
           "Enable ASan and UBSan for native layout tests" ON)
    if(PLATFORM_DISPLAY_ENABLE_SANITIZERS)
        target_compile_options(platform_display_layout PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_display_layout PRIVATE
            -fsanitize=address,undefined)
        target_compile_options(platform_display_layout_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_display_layout_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME platform_display_layout_tests COMMAND platform_display_layout_tests)

add_executable(platform_display_layout_waveshare_tests
    test_platform_display_layout.c
    ../src/platform_display_layout.c
)
target_include_directories(platform_display_layout_waveshare_tests PRIVATE
    ../src
    ../include
    ../../platform_board/include
)
target_compile_definitions(platform_display_layout_waveshare_tests PRIVATE
    CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3=1
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3=1
)
target_compile_features(platform_display_layout_waveshare_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_display_layout_waveshare_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(platform_display_layout_waveshare_tests PRIVATE
        -fsanitize=address,undefined)
endif()

add_test(NAME platform_display_layout_waveshare_tests
         COMMAND platform_display_layout_waveshare_tests)

add_executable(platform_display_layout_tab5_tests
    test_platform_display_tab5.c
    ../src/platform_display_layout.c
)
target_include_directories(platform_display_layout_tab5_tests PRIVATE
    ../src
    ../include
    ../../platform_board/include
)
target_compile_definitions(platform_display_layout_tab5_tests PRIVATE
    CONFIG_P4_BOARD_M5STACK_TAB5=1
)
target_compile_features(platform_display_layout_tab5_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_display_layout_tab5_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(platform_display_layout_tab5_tests PRIVATE
        -fsanitize=address,undefined)
endif()

add_test(NAME platform_display_layout_tab5_tests
         COMMAND platform_display_layout_tab5_tests)

# The exact queue/damage helpers used by the Tab5 hardware submission path.
add_executable(platform_display_tab5_queue_tests
    test_tab5_frame_queue.c ../src/platform_display_layout.c)
target_include_directories(platform_display_tab5_queue_tests PRIVATE
    ../src ../include ../../platform_board/include)
target_compile_definitions(platform_display_tab5_queue_tests PRIVATE CONFIG_P4_BOARD_M5STACK_TAB5=1)
target_compile_features(platform_display_tab5_queue_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_display_tab5_queue_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_display_tab5_queue_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME platform_display_tab5_queue_tests COMMAND platform_display_tab5_queue_tests)

add_executable(platform_display_tab5_prescale_tests test_tab5_game_prescale.c)
target_include_directories(platform_display_tab5_prescale_tests PRIVATE ../src)
target_compile_features(platform_display_tab5_prescale_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_display_tab5_prescale_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_display_tab5_prescale_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME platform_display_tab5_prescale_tests COMMAND platform_display_tab5_prescale_tests)

# A prescaled game must retain the original 320x200 CPU fallback rounding.
add_executable(platform_display_tab5_prescaled_game_tests
    test_tab5_prescaled_game.c ../src/platform_display_layout.c)
target_include_directories(platform_display_tab5_prescaled_game_tests PRIVATE
    ../src ../include ../../platform_board/include)
target_compile_definitions(platform_display_tab5_prescaled_game_tests PRIVATE
    CONFIG_P4_BOARD_M5STACK_TAB5=1)
target_compile_features(platform_display_tab5_prescaled_game_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_display_tab5_prescaled_game_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_display_tab5_prescaled_game_tests PRIVATE
        -fsanitize=address,undefined)
endif()
add_test(NAME platform_display_tab5_prescaled_game_tests
         COMMAND platform_display_tab5_prescaled_game_tests)

add_executable(platform_display_tab5_prescaled_game_transform_tests
    test_tab5_prescaled_game_transform.c ../src/platform_display_layout.c)
target_include_directories(platform_display_tab5_prescaled_game_transform_tests PRIVATE
    ../src ../include ../../platform_board/include)
target_compile_definitions(platform_display_tab5_prescaled_game_transform_tests PRIVATE
    CONFIG_P4_BOARD_M5STACK_TAB5=1)
target_compile_features(platform_display_tab5_prescaled_game_transform_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_display_tab5_prescaled_game_transform_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_display_tab5_prescaled_game_transform_tests PRIVATE
        -fsanitize=address,undefined)
endif()
add_test(NAME platform_display_tab5_prescaled_game_transform_tests
         COMMAND platform_display_tab5_prescaled_game_transform_tests)
