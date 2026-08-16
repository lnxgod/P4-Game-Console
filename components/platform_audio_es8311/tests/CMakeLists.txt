add_executable(platform_audio_es8311_policy_tests
    test_platform_audio_es8311_policy.c)
target_link_libraries(platform_audio_es8311_policy_tests
    PRIVATE platform_audio_es8311_policy)
target_include_directories(platform_audio_es8311_policy_tests PRIVATE
    mocks
    ../src
    ../../platform_board/include
)
target_compile_features(platform_audio_es8311_policy_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_audio_es8311_policy_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )
    option(PLATFORM_AUDIO_ES8311_ENABLE_SANITIZERS
           "Enable ASan and UBSan for native ES8311 policy tests" ON)
    if(PLATFORM_AUDIO_ES8311_ENABLE_SANITIZERS)
        target_compile_options(platform_audio_es8311_policy PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_audio_es8311_policy PRIVATE
            -fsanitize=address,undefined)
        target_compile_options(platform_audio_es8311_policy_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_audio_es8311_policy_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME platform_audio_es8311_policy_tests
         COMMAND platform_audio_es8311_policy_tests)

add_library(platform_audio_es8311_policy_waveshare STATIC
    ../src/platform_audio_es8311_policy.c)
target_include_directories(platform_audio_es8311_policy_waveshare
    PUBLIC ../include
    PRIVATE ../src ../../platform_board/include)
target_compile_definitions(platform_audio_es8311_policy_waveshare
    PUBLIC CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3=1)
target_compile_features(platform_audio_es8311_policy_waveshare PUBLIC c_std_11)

add_executable(platform_audio_es8311_waveshare_policy_tests
    test_platform_audio_es8311_policy.c)
target_link_libraries(platform_audio_es8311_waveshare_policy_tests
    PRIVATE platform_audio_es8311_policy_waveshare)
target_include_directories(platform_audio_es8311_waveshare_policy_tests PRIVATE
    mocks
    ../src
    ../../platform_board/include
)
target_compile_features(platform_audio_es8311_waveshare_policy_tests
    PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_audio_es8311_policy_waveshare PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_audio_es8311_policy_waveshare PRIVATE
        -fsanitize=address,undefined)
    target_compile_options(platform_audio_es8311_waveshare_policy_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(platform_audio_es8311_waveshare_policy_tests PRIVATE
        -fsanitize=address,undefined)
endif()

add_test(NAME platform_audio_es8311_waveshare_policy_tests
         COMMAND platform_audio_es8311_waveshare_policy_tests)
