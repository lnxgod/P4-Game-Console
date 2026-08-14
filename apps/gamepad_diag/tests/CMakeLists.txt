cmake_minimum_required(VERSION 3.16)
project(gamepad_diag_arm_model_tests VERSION 0.1.0 LANGUAGES C)

add_library(gamepad_diag_arm_model STATIC
    ../main/gamepad_diag_arm_model.c
)
target_include_directories(gamepad_diag_arm_model PUBLIC ../main)
target_compile_features(gamepad_diag_arm_model PUBLIC c_std_11)

add_executable(gamepad_diag_arm_model_tests
    test_gamepad_diag_arm_model.c
)
target_link_libraries(gamepad_diag_arm_model_tests PRIVATE
    gamepad_diag_arm_model
)
target_compile_features(gamepad_diag_arm_model_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(target gamepad_diag_arm_model gamepad_diag_arm_model_tests)
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
add_test(NAME gamepad_diag_arm_model_tests
         COMMAND gamepad_diag_arm_model_tests)
