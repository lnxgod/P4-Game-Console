if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/input.c" "src/overlay.c"
        INCLUDE_DIRS "include"
    )

    target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wshadow
        -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(doom_touch_input VERSION 0.1.0 LANGUAGES C)

    add_library(doom_touch_input STATIC src/input.c src/overlay.c)
    target_include_directories(doom_touch_input PUBLIC include)
    target_compile_features(doom_touch_input PUBLIC c_std_11)

    add_executable(doom_touch_input_tests tests/test_doom_touch_input.c)
    target_link_libraries(doom_touch_input_tests PRIVATE doom_touch_input)
    target_compile_features(doom_touch_input_tests PRIVATE c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        foreach(target doom_touch_input doom_touch_input_tests)
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE
                -fsanitize=address,undefined
            )
        endforeach()
    endif()

    include(CTest)
    add_test(NAME doom_touch_input_tests COMMAND doom_touch_input_tests)
endif()
