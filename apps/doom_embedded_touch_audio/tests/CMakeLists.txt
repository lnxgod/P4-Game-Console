cmake_minimum_required(VERSION 3.16)
project(doom_touch_audio_gate_tests VERSION 0.1.0 LANGUAGES C)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(test_doom_touch_audio_gate
    test_runtime_gate.c
    ../main/runtime_gate.c
)
target_include_directories(test_doom_touch_audio_gate PRIVATE ../main)
target_compile_features(test_doom_touch_audio_gate PRIVATE c_std_11)
target_compile_options(test_doom_touch_audio_gate PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

enable_testing()
add_test(NAME doom_touch_audio_gate COMMAND test_doom_touch_audio_gate)

add_executable(test_doom_touch_audio_degraded_policy
    test_degraded_policy.c
    ../main/degraded_policy.c
)
target_include_directories(test_doom_touch_audio_degraded_policy PRIVATE ../main)
target_compile_features(test_doom_touch_audio_degraded_policy PRIVATE c_std_11)
target_compile_options(test_doom_touch_audio_degraded_policy PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME doom_touch_audio_degraded_policy
    COMMAND test_doom_touch_audio_degraded_policy)

add_executable(test_doom_e6_audio_lifecycle
    test_audio_lifecycle.c
    ../main/audio_lifecycle.c
)
target_include_directories(test_doom_e6_audio_lifecycle PRIVATE
    ../main
    ../components/platform_audio/include
    ../../doom/components/doom_audio/include
    ../../../components/platform_audio_factory/include
    ../../../components/platform_audio_factory/tests/mocks
)
target_compile_features(test_doom_e6_audio_lifecycle PRIVATE c_std_11)
target_compile_options(test_doom_e6_audio_lifecycle PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME doom_e6_audio_lifecycle COMMAND test_doom_e6_audio_lifecycle)

add_executable(test_doom_touch_controls
    test_touch_controls.c
    ../main/touch_controls.c
    ../../../components/doom_touch_input/src/input.c
    ../../../components/doom_touch_input/src/overlay.c
)
target_include_directories(test_doom_touch_controls PRIVATE
    ../main
    ../../../components/doom_touch_input/include
    ../../../components/platform_touch/include
    ../../../components/platform_touch/host/include
    ../../../third_party/doomgeneric/doomgeneric
)
target_compile_features(test_doom_touch_controls PRIVATE c_std_11)
target_compile_options(test_doom_touch_controls PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer
)
target_link_options(test_doom_touch_controls PRIVATE
    -fsanitize=address,undefined
)
add_test(NAME doom_touch_controls COMMAND test_doom_touch_controls)
add_test(
    NAME doom_touch_audio_source_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/test_source_contract.py
)
