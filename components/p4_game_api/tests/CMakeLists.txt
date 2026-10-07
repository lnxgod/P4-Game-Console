add_executable(p4_game_api_tests test_p4_game_api.c)
target_link_libraries(p4_game_api_tests PRIVATE p4_game_api)
target_compile_features(p4_game_api_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_game_api_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_game_api_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_game_api_tests COMMAND p4_game_api_tests)

add_executable(p4_game_api_waveshare_tests
    test_p4_game_api.c
    ../src/achievements.c
    ../src/audio_mixer.c
    ../src/audio_pack.c
    ../src/draw.c
    ../src/feedback.c
    ../src/game_runtime.c
    ../src/input.c
    ../src/presentation_font.c
    ../src/visual.c
)
target_include_directories(p4_game_api_waveshare_tests PRIVATE ../include)
target_link_libraries(p4_game_api_waveshare_tests PRIVATE p4_cp437)
target_compile_definitions(p4_game_api_waveshare_tests PRIVATE
    P4_INPUT_TARGET_WIDTH=800U
    P4_INPUT_TARGET_HEIGHT=480U
)
target_compile_features(p4_game_api_waveshare_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_game_api_waveshare_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_game_api_waveshare_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_game_api_waveshare_tests
         COMMAND p4_game_api_waveshare_tests)

function(p4_add_cartridge_compat_test target mode)
    add_executable(${target}
        test_cartridge_compat.c
        ../runtime/cartridge_main.c
    )
    target_link_libraries(${target} PRIVATE p4_game_api)
    target_compile_definitions(${target} PRIVATE
        P4_GAME_ENTRY_SYMBOL=p4_test_cartridge_game
        P4_TEST_SAVE_MODE=${mode}
    )
    target_compile_features(${target} PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

p4_add_cartridge_compat_test(p4_cartridge_old_compat_tests 0)
p4_add_cartridge_compat_test(p4_cartridge_save_required_tests 1)
p4_add_cartridge_compat_test(p4_cartridge_save_optional_tests 2)

add_executable(p4_presentation_tests test_presentation.c)
target_link_libraries(p4_presentation_tests PRIVATE p4_game_api)
target_compile_features(p4_presentation_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_presentation_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(p4_presentation_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_presentation_tests COMMAND p4_presentation_tests)

add_executable(p4_raster_equivalence_tests test_raster_equivalence.c)
target_link_libraries(p4_raster_equivalence_tests PRIVATE p4_game_api)
target_compile_features(p4_raster_equivalence_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_raster_equivalence_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(p4_raster_equivalence_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_raster_equivalence_tests COMMAND p4_raster_equivalence_tests)

add_executable(p4_mesh_tests test_mesh.c)
target_link_libraries(p4_mesh_tests PRIVATE p4_game_api)
target_compile_features(p4_mesh_tests PRIVATE c_std_11)
target_compile_options(p4_mesh_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(p4_mesh_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME p4_mesh_tests COMMAND p4_mesh_tests)

add_executable(p4_cartridge_rotating_surface_tests
    test_cartridge_rotating_surface.c ../runtime/cartridge_main.c)
target_link_libraries(p4_cartridge_rotating_surface_tests PRIVATE p4_game_api)
target_compile_definitions(p4_cartridge_rotating_surface_tests PRIVATE
    P4_GAME_ENTRY_SYMBOL=p4_test_rotating_game)
target_compile_features(p4_cartridge_rotating_surface_tests PRIVATE c_std_11)
target_compile_options(p4_cartridge_rotating_surface_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(p4_cartridge_rotating_surface_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME p4_cartridge_rotating_surface_tests COMMAND p4_cartridge_rotating_surface_tests)
