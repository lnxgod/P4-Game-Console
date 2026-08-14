cmake_minimum_required(VERSION 3.16)
project(doom_touch_audio_platform_audio_adapter_tests VERSION 0.1.0 LANGUAGES C)

add_executable(test_platform_audio_adapter
    test_platform_audio_adapter.c
    ../src/platform_audio_adapter.c
)
target_include_directories(test_platform_audio_adapter PRIVATE
    ../include
    ../../../../../components/platform_audio_factory/include
    ../../../../../components/platform_audio_factory/tests/mocks
)
target_compile_features(test_platform_audio_adapter PRIVATE c_std_11)
target_compile_options(test_platform_audio_adapter PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

enable_testing()
add_test(NAME platform_audio_adapter COMMAND test_platform_audio_adapter)
