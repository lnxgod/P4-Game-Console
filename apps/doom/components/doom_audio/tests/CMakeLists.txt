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
        target_compile_options(p4_midi PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
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

add_executable(doom_music_midi_tests test_doom_music_midi.c)
target_link_libraries(doom_music_midi_tests PRIVATE doom_audio_core)
target_compile_features(doom_music_midi_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(doom_music_midi_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(DOOM_AUDIO_ENABLE_SANITIZERS)
        target_compile_options(doom_music_midi_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(doom_music_midi_tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME doom_music_midi_tests COMMAND doom_music_midi_tests)
set_tests_properties(doom_music_midi_tests PROPERTIES TIMEOUT 10)

get_filename_component(P4_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
add_test(NAME doom_music_pure_hades_tests COMMAND doom_music_midi_tests
    "${P4_REPO_ROOT}/game-data/pure-hades/v0.6/PUREHADES.WAD")
set_tests_properties(doom_music_pure_hades_tests PROPERTIES TIMEOUT 30)
foreach(wad freedoom2.wad arena-inbox/dwango5/DWANGO5.WAD)
    if(EXISTS "${P4_REPO_ROOT}/local-data/doom/${wad}")
        get_filename_component(wad_name "${wad}" NAME_WE)
        add_test(NAME doom_music_${wad_name}_tests COMMAND doom_music_midi_tests
            "${P4_REPO_ROOT}/local-data/doom/${wad}")
        set_tests_properties(doom_music_${wad_name}_tests PROPERTIES TIMEOUT 60)
    endif()
endforeach()

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

# Exercise exact runtime transitions with deterministic worker/producer hooks.
# Only this separate core redirects synthesis allocation for ownership checks.
add_library(doom_audio_runtime_admission_core STATIC
    ../src/doom_audio_mixer.c
    ../src/doom_music_synth.c
    ../src/doom_audio_ring.c
)
target_include_directories(doom_audio_runtime_admission_core PUBLIC ../include)
target_link_libraries(doom_audio_runtime_admission_core PRIVATE p4_midi)
target_compile_definitions(doom_audio_runtime_admission_core PRIVATE
    malloc=test_music_malloc free=test_music_free)
target_compile_features(doom_audio_runtime_admission_core PUBLIC c_std_11)
add_executable(doom_audio_runtime_admission_tests
    test_doom_audio_runtime_admission.c)
target_include_directories(doom_audio_runtime_admission_tests BEFORE PRIVATE mocks)
target_link_libraries(doom_audio_runtime_admission_tests PRIVATE
    doom_audio_runtime_admission_core)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(target doom_audio_runtime_admission_core doom_audio_runtime_admission_tests)
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
        if(DOOM_AUDIO_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endforeach()
endif()
add_test(NAME doom_audio_runtime_admission_tests COMMAND doom_audio_runtime_admission_tests)
set_tests_properties(doom_audio_runtime_admission_tests PROPERTIES TIMEOUT 10)
