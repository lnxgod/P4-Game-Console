if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/input.c"
        INCLUDE_DIRS "include"
        REQUIRES gamepad_core
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
    project(doom_gamepad_input VERSION 0.1.0 LANGUAGES C)

    add_library(doom_gamepad_input STATIC
        src/input.c
        ../gamepad_core/src/gamepad.c
    )
    target_include_directories(doom_gamepad_input PUBLIC
        include
        ../gamepad_core/include
    )
    target_compile_features(doom_gamepad_input PUBLIC c_std_11)

    add_executable(doom_gamepad_input_tests
        tests/test_doom_gamepad_input.c
    )
    target_link_libraries(doom_gamepad_input_tests PRIVATE
        doom_gamepad_input
    )
    target_compile_features(doom_gamepad_input_tests PRIVATE c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        foreach(target doom_gamepad_input doom_gamepad_input_tests)
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
    add_test(NAME doom_gamepad_input_tests
             COMMAND doom_gamepad_input_tests)
endif()
