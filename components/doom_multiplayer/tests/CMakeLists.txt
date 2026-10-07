# SPDX-License-Identifier: MIT

add_executable(doom_multiplayer_tests test_doom_multiplayer.c)
target_link_libraries(doom_multiplayer_tests PRIVATE doom_multiplayer)
target_compile_features(doom_multiplayer_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(doom_multiplayer_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(doom_multiplayer_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME doom_multiplayer_tests COMMAND doom_multiplayer_tests)

add_executable(doom_arena_tests test_doom_arena.c)
target_link_libraries(doom_arena_tests PRIVATE doom_multiplayer)
target_compile_options(doom_arena_tests PRIVATE -Wall -Wextra -Wconversion -Werror -fsanitize=address,undefined)
target_link_options(doom_arena_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME doom_arena_tests COMMAND doom_arena_tests)

add_executable(doom_lockstep_tests test_doom_lockstep.c)
target_link_libraries(doom_lockstep_tests PRIVATE doom_multiplayer)
target_compile_options(doom_lockstep_tests PRIVATE -Wall -Wextra -Wconversion -Werror -fsanitize=address,undefined)
target_link_options(doom_lockstep_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME doom_lockstep_tests COMMAND doom_lockstep_tests)

if(UNIX)
    add_executable(doom_gc_adapter_tests test_doom_gc_adapter.c
        ../../../apps/console_os/main/doom_gc_p4mp.c)
    target_include_directories(doom_gc_adapter_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_adapter_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_adapter_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_adapter_tests PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_adapter_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_adapter_tests COMMAND doom_gc_adapter_tests)
    add_test(NAME doom_gc_handoff_loss COMMAND doom_gc_adapter_tests handoff)
    set_tests_properties(doom_gc_adapter_tests doom_gc_handoff_loss PROPERTIES TIMEOUT 35)
endif()

if(UNIX)
    add_executable(doom_gc_stall_tests test_doom_gc_stall.c
        ../../../apps/console_os/main/doom_gc_p4mp.c)
    target_include_directories(doom_gc_stall_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_stall_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_stall_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_stall_tests PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_stall_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_stalled_guest COMMAND doom_gc_stall_tests host)
    add_test(NAME doom_gc_host_heartbeat COMMAND doom_gc_stall_tests client)
    add_test(NAME doom_gc_host_paused COMMAND doom_gc_stall_tests host-paused)
    add_test(NAME doom_gc_lobby_only_timeout COMMAND doom_gc_stall_tests lobby-only)
endif()
