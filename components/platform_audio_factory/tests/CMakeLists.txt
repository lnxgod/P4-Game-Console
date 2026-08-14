add_executable(platform_audio_factory_policy_tests
    test_platform_audio_factory_policy.c
)
target_link_libraries(platform_audio_factory_policy_tests
    PRIVATE platform_audio_factory_policy
)
target_include_directories(platform_audio_factory_policy_tests PRIVATE
    ../src
    mocks
)
target_compile_features(platform_audio_factory_policy_tests PRIVATE c_std_11)

add_executable(platform_audio_factory_runtime_tests
    test_platform_audio_factory_runtime.c
    mocks/mock_factory_runtime.c
    ../src/platform_audio_factory.c
    ../src/platform_audio_factory_policy.c
)
target_include_directories(platform_audio_factory_runtime_tests PRIVATE
    mocks
    ../include
    ../src
)
target_compile_definitions(platform_audio_factory_runtime_tests PRIVATE
    PLATFORM_AUDIO_FACTORY_HOST_TEST=1
)
target_compile_features(platform_audio_factory_runtime_tests PRIVATE c_std_11)
find_package(Threads REQUIRED)
target_link_libraries(platform_audio_factory_runtime_tests PRIVATE Threads::Threads)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(test_target
            platform_audio_factory_policy_tests
            platform_audio_factory_runtime_tests)
        target_compile_options(${test_target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Werror
        )
    endforeach()

    option(PLATFORM_AUDIO_FACTORY_ENABLE_SANITIZERS
           "Enable ASan and UBSan for native factory-audio tests" ON)
    if(PLATFORM_AUDIO_FACTORY_ENABLE_SANITIZERS)
        target_compile_options(platform_audio_factory_policy PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(platform_audio_factory_policy PRIVATE
            -fsanitize=address,undefined
        )
        foreach(test_target
                platform_audio_factory_policy_tests
                platform_audio_factory_runtime_tests)
            target_compile_options(${test_target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(${test_target} PRIVATE
                -fsanitize=address,undefined
            )
        endforeach()
    endif()
endif()

add_test(
    NAME platform_audio_factory_policy_tests
    COMMAND platform_audio_factory_policy_tests
)
add_test(
    NAME platform_audio_factory_runtime_tests
    COMMAND platform_audio_factory_runtime_tests
)
