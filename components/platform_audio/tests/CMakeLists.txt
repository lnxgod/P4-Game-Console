add_executable(platform_audio_policy_tests test_platform_audio.c)
target_link_libraries(platform_audio_policy_tests PRIVATE platform_audio_policy)
target_include_directories(platform_audio_policy_tests PRIVATE ../src)
target_compile_features(platform_audio_policy_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_audio_policy_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )

    option(PLATFORM_AUDIO_ENABLE_SANITIZERS
           "Enable ASan and UBSan for native audio policy tests" ON)
    if(PLATFORM_AUDIO_ENABLE_SANITIZERS)
        target_compile_options(platform_audio_policy PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_audio_policy PRIVATE
            -fsanitize=address,undefined)
        target_compile_options(platform_audio_policy_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_audio_policy_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME platform_audio_policy_tests COMMAND platform_audio_policy_tests)

add_executable(platform_audio_runtime_tests
    test_platform_audio_runtime.c
    mocks/mock_esp32_runtime.c
    ../src/platform_audio.c
    ../src/platform_audio_policy.c
)
target_include_directories(platform_audio_runtime_tests PRIVATE
    mocks
    ../include
    ../src
)
target_compile_features(platform_audio_runtime_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_audio_runtime_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )
    if(PLATFORM_AUDIO_ENABLE_SANITIZERS)
        target_compile_options(platform_audio_runtime_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_audio_runtime_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME platform_audio_runtime_tests COMMAND platform_audio_runtime_tests)
