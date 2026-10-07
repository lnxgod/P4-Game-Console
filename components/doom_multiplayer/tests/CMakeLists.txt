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

add_executable(doom_arena_ui_tests test_doom_arena_ui.c)
target_include_directories(doom_arena_ui_tests PRIVATE
    ../../../apps/doom_audio_probe/components/doom_engine_audio)
target_include_directories(doom_arena_ui_tests SYSTEM PRIVATE
    ../../../third_party/doomgeneric/doomgeneric)
target_link_libraries(doom_arena_ui_tests PRIVATE doom_multiplayer)
target_compile_options(doom_arena_ui_tests PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
target_link_options(doom_arena_ui_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME doom_arena_ui_tests COMMAND doom_arena_ui_tests)

add_executable(doom_lockstep_tests test_doom_lockstep.c)
target_link_libraries(doom_lockstep_tests PRIVATE doom_multiplayer)
target_compile_options(doom_lockstep_tests PRIVATE -Wall -Wextra -Wconversion -Werror -fsanitize=address,undefined)
target_link_options(doom_lockstep_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME doom_lockstep_tests COMMAND doom_lockstep_tests)

if(UNIX)
    add_executable(doom_gc_adapter_tests test_doom_gc_adapter.c
        ../../../apps/console_os/main/doom_gc_p4mp.c)
    target_include_directories(doom_gc_adapter_tests PRIVATE mocks
        ../../../apps/console_os/components/platform_readonly_blob/include
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
        ../../../apps/console_os/components/platform_readonly_blob/include
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

if(UNIX)
    # Keep ESP-IDF adapter mocks separate from the Wi-Fi source's richer mocks.
    add_library(doom_gc_startup_vfs OBJECT test_doom_gc_startup_vfs.c
        ../../../apps/console_os/components/platform_readonly_blob/platform_game_storage_blob.c)
    target_include_directories(doom_gc_startup_vfs PRIVATE
        ../../platform_game_storage/tests/mocks ../../platform_game_storage/include
        ../../platform_readonly_blob/include
        ../../../apps/console_os/components/platform_readonly_blob/include)
    target_compile_definitions(doom_gc_startup_vfs PRIVATE CONFIG_VFS_SUPPORT_DIR=1)
    target_compile_options(doom_gc_startup_vfs PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    add_library(doom_gc_startup_adapter OBJECT
        ../../../apps/console_os/main/doom_gc_p4mp.c)
    target_include_directories(doom_gc_startup_adapter PRIVATE mocks
        ../../../apps/console_os/components/platform_readonly_blob/include
        ../../../apps/console_os/main
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_startup_adapter SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_startup_adapter PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_startup_adapter PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    add_executable(doom_gc_startup_lease_tests test_doom_gc_startup_lease.c
        ../../platform_multiplayer_wifi/src/discovery.c
        $<TARGET_OBJECTS:doom_gc_startup_vfs>
        $<TARGET_OBJECTS:doom_gc_startup_adapter>)
    target_include_directories(doom_gc_startup_lease_tests PRIVATE
        ../../platform_multiplayer_wifi/tests/mocks
        ../../platform_multiplayer_wifi/include
        ../../platform_multiplayer_wifi/src
        ../../platform_ble_host/include
        ../../../apps/console_os/main
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_startup_lease_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_startup_lease_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_startup_lease_tests PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_startup_lease_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_startup_immediate COMMAND doom_gc_startup_lease_tests immediate)
    add_test(NAME doom_gc_startup_slow_storage COMMAND doom_gc_startup_lease_tests pumped)
    add_test(NAME doom_gc_startup_expired_route COMMAND doom_gc_startup_lease_tests expired)
    add_test(NAME doom_gc_startup_wait_is_not_ready COMMAND doom_gc_startup_lease_tests waiting)
    add_test(NAME doom_gc_startup_slow_engine_reads COMMAND doom_gc_startup_lease_tests vfs)
    set_tests_properties(doom_gc_startup_immediate doom_gc_startup_slow_storage
        doom_gc_startup_expired_route doom_gc_startup_wait_is_not_ready doom_gc_startup_slow_engine_reads
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 10)

    add_executable(doom_gc_startup_deadline_tests test_doom_gc_startup_deadline.c
        $<TARGET_OBJECTS:doom_gc_startup_adapter>)
    target_include_directories(doom_gc_startup_deadline_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_startup_deadline_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_startup_deadline_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_startup_deadline_tests PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_startup_deadline_tests PRIVATE -fsanitize=address,undefined)
    foreach(startup_case host-delayed client-delayed waiting silence replay malformed lobby-only silent-peer)
        add_test(NAME doom_gc_startup_deadline_${startup_case}
            COMMAND doom_gc_startup_deadline_tests ${startup_case})
        set_tests_properties(doom_gc_startup_deadline_${startup_case} PROPERTIES TIMEOUT 10)
    endforeach()

    add_executable(doom_gc_postconfigure_loading_tests test_doom_gc_postconfigure_loading.c
        $<TARGET_OBJECTS:doom_gc_startup_adapter>)
    target_include_directories(doom_gc_postconfigure_loading_tests PRIVATE mocks
        ../../../apps/console_os/components/platform_readonly_blob/include
        ../../../apps/console_os/main
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_postconfigure_loading_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_postconfigure_loading_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_postconfigure_loading_tests PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_postconfigure_loading_tests PRIVATE -fsanitize=address,undefined)
    foreach(loading_role host client)
        foreach(loading_case local local-cap preconfigure-cap wait-cap silence replay malformed)
            add_test(NAME doom_gc_postconfigure_${loading_role}_${loading_case}
                COMMAND doom_gc_postconfigure_loading_tests ${loading_role} ${loading_case})
            set_tests_properties(doom_gc_postconfigure_${loading_role}_${loading_case}
                PROPERTIES TIMEOUT 10)
        endforeach()
    endforeach()
    add_test(NAME doom_gc_postconfigure_incidental_reads
        COMMAND doom_gc_postconfigure_loading_tests host incidental)
    set_tests_properties(doom_gc_postconfigure_incidental_reads PROPERTIES TIMEOUT 10)
endif()

# Exact replay and resume-control coverage remains independent of the adapter.
foreach(resume_suite doom_replay doom_resume)
    add_executable(${resume_suite}_tests test_${resume_suite}.c)
    target_link_libraries(${resume_suite}_tests PRIVATE doom_multiplayer)
    target_compile_options(${resume_suite}_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(${resume_suite}_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME ${resume_suite}_tests COMMAND ${resume_suite}_tests)
endforeach()

if(UNIX)
    # The older fixtures consume tics immediately and do not request replay.
    foreach(ordinary_suite doom_gc_adapter_tests doom_gc_stall_tests
        doom_gc_startup_lease_tests doom_gc_startup_deadline_tests
        doom_gc_postconfigure_loading_tests)
        target_sources(${ordinary_suite} PRIVATE test_doom_gc_engine_stub.c)
    endforeach()

    # Includes the production adapter in this TU for deterministic virtual-wire
    # control; the separate strong store fixture overrides its weak callback.
    add_executable(doom_gc_rejoin_tests test_doom_gc_rejoin.c test_doom_gc_resume_store.c)
    target_include_directories(doom_gc_rejoin_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/console_os/components/platform_readonly_blob/include
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_rejoin_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_rejoin_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_rejoin_tests PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_rejoin_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_rejoin_tests COMMAND doom_gc_rejoin_tests)
    set_tests_properties(doom_gc_rejoin_tests PROPERTIES TIMEOUT 20)

    add_executable(doom_gc_late_join_tests test_doom_gc_late_join.c test_doom_gc_resume_store.c)
    target_include_directories(doom_gc_late_join_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/console_os/components/platform_readonly_blob/include
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_late_join_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_late_join_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_late_join_tests PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_late_join_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_late_join_tests COMMAND doom_gc_late_join_tests)
    set_tests_properties(doom_gc_late_join_tests PROPERTIES TIMEOUT 20)
    add_executable(doom_gc_replay_pacing_tests test_doom_gc_replay_pacing.c test_doom_gc_resume_store.c)
    target_include_directories(doom_gc_replay_pacing_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/console_os/components/platform_readonly_blob/include
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_replay_pacing_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_replay_pacing_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_replay_pacing_tests PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_replay_pacing_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_replay_pacing_tests COMMAND doom_gc_replay_pacing_tests)
    set_tests_properties(doom_gc_replay_pacing_tests PROPERTIES TIMEOUT 20)
    add_executable(doom_gc_live_ready_tests test_doom_gc_live_ready.c test_doom_gc_resume_store.c)
    target_include_directories(doom_gc_live_ready_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/console_os/components/platform_readonly_blob/include
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_live_ready_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_live_ready_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_live_ready_tests PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_live_ready_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_live_ready_tests COMMAND doom_gc_live_ready_tests)
    set_tests_properties(doom_gc_live_ready_tests PROPERTIES TIMEOUT 20)
    add_executable(doom_gc_rx_coalesce_tests test_doom_gc_rx_coalesce.c test_doom_gc_resume_store.c)
    target_include_directories(doom_gc_rx_coalesce_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/console_os/components/platform_readonly_blob/include
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_rx_coalesce_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_rx_coalesce_tests PRIVATE doom_multiplayer)
    target_compile_options(doom_gc_rx_coalesce_tests PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(doom_gc_rx_coalesce_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_rx_coalesce_tests COMMAND doom_gc_rx_coalesce_tests)
    set_tests_properties(doom_gc_rx_coalesce_tests PROPERTIES TIMEOUT 20)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME doom_network_stats
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../scripts/tests/test-doom-net-stats.py)
add_test(NAME doom_arena_framebuffer
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_doom_arena_framebuffer.py)

# Execute the production OS admission/start functions with bounded host mocks.
add_test(NAME console_arena_resume
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../scripts/tests/test-console-arena-resume.py)
add_test(NAME console_arena_start
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../scripts/tests/test-console-arena-start.py)
add_test(NAME console_multiplayer_flow
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../scripts/tests/test-console-multiplayer-flow.py)

add_test(NAME console_doom_loading_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../scripts/tests/test-console-doom-loading-boundary.py)

add_test(NAME doom_replay_idle
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../../scripts/tests/test-doom-replay-idle.py)

# Portable SHA for host checkpoint fixtures. The firmware codec has its own
# crypto provider; these tests use the repository's existing save-data SHA.
add_library(doom_checkpoint_host_sha OBJECT
    checkpoint_host_sha.c ../../p4_game_save/src/sha256.c)
target_include_directories(doom_checkpoint_host_sha PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ../../p4_game_save/src)
target_compile_features(doom_checkpoint_host_sha PRIVATE c_std_11)
target_compile_options(doom_checkpoint_host_sha PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)

if(UNIX)
    # Legacy fixtures still negotiate protocol 7. A checkpoint call here is a
    # test failure, not a successful simulated restore.
    foreach(legacy_suite doom_gc_adapter_tests doom_gc_stall_tests
        doom_gc_startup_lease_tests doom_gc_startup_deadline_tests
        doom_gc_postconfigure_loading_tests doom_gc_rejoin_tests
        doom_gc_late_join_tests doom_gc_replay_pacing_tests doom_gc_live_ready_tests doom_gc_rx_coalesce_tests)
        target_sources(${legacy_suite} PRIVATE checkpoint_engine_stub.c)
        target_link_libraries(${legacy_suite} PRIVATE doom_checkpoint_host_sha)
    endforeach()

    add_executable(doom_gc_checkpoint_tests
        test_doom_gc_checkpoint.c test_doom_gc_resume_store.c)
    target_include_directories(doom_gc_checkpoint_tests PRIVATE mocks
        ../../../apps/console_os/main
        ../../../apps/console_os/components/platform_readonly_blob/include
        ../../../apps/doom_audio_probe/components/doom_engine_audio)
    target_include_directories(doom_gc_checkpoint_tests SYSTEM PRIVATE
        ../../../third_party/doomgeneric/doomgeneric)
    target_link_libraries(doom_gc_checkpoint_tests PRIVATE
        doom_multiplayer doom_checkpoint_host_sha)
    target_compile_options(doom_gc_checkpoint_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(doom_gc_checkpoint_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME doom_gc_checkpoint_tests COMMAND doom_gc_checkpoint_tests)
    set_tests_properties(doom_gc_checkpoint_tests PROPERTIES TIMEOUT 30)
endif()

foreach(checkpoint_suite doom_replay_rolling doom_replay_checkpoint doom_checkpoint_service)
    add_executable(${checkpoint_suite}_tests test_${checkpoint_suite}.c)
    target_link_libraries(${checkpoint_suite}_tests PRIVATE doom_multiplayer doom_checkpoint_host_sha)
    target_compile_options(${checkpoint_suite}_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${checkpoint_suite}_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME ${checkpoint_suite}_tests COMMAND ${checkpoint_suite}_tests)
    set_tests_properties(${checkpoint_suite}_tests PROPERTIES TIMEOUT 30)
endforeach()

# This wire fixture spans P4MP framing and Doom checkpoint transfer, so its
# registration belongs with the combined Doom suite rather than standalone P4MP.
add_executable(doom_checkpoint_wire_tests ../../p4_multiplayer/tests/test_checkpoint_wire.c)
target_link_libraries(doom_checkpoint_wire_tests PRIVATE doom_multiplayer doom_checkpoint_host_sha)
target_compile_options(doom_checkpoint_wire_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(doom_checkpoint_wire_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME doom_checkpoint_wire_tests COMMAND doom_checkpoint_wire_tests)
set_tests_properties(doom_checkpoint_wire_tests PROPERTIES TIMEOUT 30)

# Bounded live pipelining uses the actual adapter and canonical codecs. The
# included legacy fixtures provide deterministic transport/engine boundaries.
if(UNIX)
    foreach(pipeline_suite doom_gc_live_pipeline doom_gc_pipeline_budget)
        add_executable(${pipeline_suite}_tests
            test_${pipeline_suite}.c test_doom_gc_resume_store.c)
        target_include_directories(${pipeline_suite}_tests PRIVATE mocks
            ../../../apps/console_os/main
            ../../../apps/console_os/components/platform_readonly_blob/include
            ../../../apps/doom_audio_probe/components/doom_engine_audio)
        target_include_directories(${pipeline_suite}_tests SYSTEM PRIVATE
            ../../../third_party/doomgeneric/doomgeneric)
        target_link_libraries(${pipeline_suite}_tests PRIVATE
            doom_multiplayer doom_checkpoint_host_sha)
        target_compile_options(${pipeline_suite}_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${pipeline_suite}_tests PRIVATE -fsanitize=address,undefined)
        add_test(NAME ${pipeline_suite}_tests COMMAND ${pipeline_suite}_tests)
        set_tests_properties(${pipeline_suite}_tests PROPERTIES TIMEOUT 30)
    endforeach()
    target_sources(doom_gc_live_pipeline_tests PRIVATE checkpoint_engine_stub.c)
endif()
