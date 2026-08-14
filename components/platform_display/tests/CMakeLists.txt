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
