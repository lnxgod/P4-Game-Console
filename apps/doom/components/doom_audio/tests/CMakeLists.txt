add_executable(doom_audio_core_tests test_doom_audio.c)
find_package(Threads REQUIRED)
target_link_libraries(doom_audio_core_tests PRIVATE
    doom_audio_core
    Threads::Threads
)
target_compile_features(doom_audio_core_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(doom_audio_core_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )

    option(DOOM_AUDIO_ENABLE_SANITIZERS
           "Enable ASan and UBSan for native Doom audio tests" ON)
    if(DOOM_AUDIO_ENABLE_SANITIZERS)
        target_compile_options(doom_audio_core PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(doom_audio_core PRIVATE
            -fsanitize=address,undefined)
        target_compile_options(doom_audio_core_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(doom_audio_core_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME doom_audio_core_tests COMMAND doom_audio_core_tests)
set_tests_properties(doom_audio_core_tests PROPERTIES TIMEOUT 10)

add_executable(doom_audio_runtime_tests
    test_doom_audio_runtime.c
    ../src/doom_audio_runtime.c
)
target_link_libraries(doom_audio_runtime_tests PRIVATE doom_audio_core)
target_include_directories(doom_audio_runtime_tests BEFORE PRIVATE mocks)
target_compile_features(doom_audio_runtime_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(doom_audio_runtime_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )
    if(DOOM_AUDIO_ENABLE_SANITIZERS)
        target_compile_options(doom_audio_runtime_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(doom_audio_runtime_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME doom_audio_runtime_tests COMMAND doom_audio_runtime_tests)
set_tests_properties(doom_audio_runtime_tests PROPERTIES TIMEOUT 10)

get_filename_component(P4_LOCAL_DOOM_WAD
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../local-data/doom/doom1.wad"
    ABSOLUTE)
if(EXISTS "${P4_LOCAL_DOOM_WAD}")
    add_executable(doom_audio_wad_tests test_doom_audio_wad.c)
    target_link_libraries(doom_audio_wad_tests PRIVATE doom_audio_core)
    target_compile_features(doom_audio_wad_tests PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(doom_audio_wad_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Werror
        )
        if(DOOM_AUDIO_ENABLE_SANITIZERS)
            target_compile_options(doom_audio_wad_tests PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(doom_audio_wad_tests PRIVATE
                -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME doom_audio_wad_tests
             COMMAND doom_audio_wad_tests "${P4_LOCAL_DOOM_WAD}")
    set_tests_properties(doom_audio_wad_tests PROPERTIES TIMEOUT 10)
else()
    message(STATUS
        "Local Doom shareware WAD absent; exact SFX-lump compatibility test skipped")
endif()
