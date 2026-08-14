add_executable(gamepad_core_tests test_gamepad_core.c)
target_link_libraries(gamepad_core_tests PRIVATE gamepad_core)
target_compile_features(gamepad_core_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(gamepad_core_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )

    option(GAMEPAD_CORE_ENABLE_SANITIZERS "Enable ASan and UBSan for native tests" ON)
    if(GAMEPAD_CORE_ENABLE_SANITIZERS)
        target_compile_options(gamepad_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(gamepad_core PRIVATE -fsanitize=address,undefined)
        target_compile_options(gamepad_core_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(gamepad_core_tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME gamepad_core_tests COMMAND gamepad_core_tests)
