add_executable(p4_game_platform_tests test_audio_session.c)
target_include_directories(p4_game_platform_tests PRIVATE mocks)
target_link_libraries(p4_game_platform_tests PRIVATE p4_game_platform)
target_compile_features(p4_game_platform_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_game_platform_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_game_platform_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_game_platform_tests COMMAND p4_game_platform_tests)

find_package(Threads REQUIRED)
add_executable(p4_game_audio_worker_tests test_audio_worker.c mock_worker_runtime.c
    ../src/audio_worker.c ../../p4_game_api/src/audio_mixer.c)
target_include_directories(p4_game_audio_worker_tests PRIVATE mocks ../include ../../p4_game_api/include)
target_link_libraries(p4_game_audio_worker_tests PRIVATE Threads::Threads)
target_compile_features(p4_game_audio_worker_tests PRIVATE c_std_11)
target_compile_options(p4_game_audio_worker_tests PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(p4_game_audio_worker_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME p4_game_audio_worker_tests COMMAND p4_game_audio_worker_tests)
