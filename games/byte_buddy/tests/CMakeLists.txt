# SPDX-License-Identifier: MIT

add_executable(byte_buddy_tests test_byte_buddy.c)
target_link_libraries(byte_buddy_tests PRIVATE byte_buddy p4_game_api)
target_include_directories(byte_buddy_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src")
get_filename_component(BYTE_BUDDY_TEST_ART_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../assets/generated/byte_buddy_dragon_art.bin"
    ABSOLUTE)
if(NOT EXISTS "${BYTE_BUDDY_TEST_ART_PATH}")
    message(FATAL_ERROR
        "Byte Buddy test art is missing: ${BYTE_BUDDY_TEST_ART_PATH}")
endif()
target_compile_definitions(byte_buddy_tests PRIVATE
    "BYTE_BUDDY_TEST_ART_PATH=\"${BYTE_BUDDY_TEST_ART_PATH}\"")
target_compile_features(byte_buddy_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(byte_buddy_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(byte_buddy_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME byte_buddy_tests COMMAND byte_buddy_tests)

add_executable(byte_buddy_save_tests test_byte_buddy_save.c)
target_link_libraries(byte_buddy_save_tests PRIVATE byte_buddy)
target_include_directories(byte_buddy_save_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../src")
target_compile_features(byte_buddy_save_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(byte_buddy_save_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(byte_buddy_save_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME byte_buddy_save_tests COMMAND byte_buddy_save_tests)

add_executable(byte_buddy_touch_previews ../tools/render_touch_previews.c)
target_link_libraries(byte_buddy_touch_previews PRIVATE byte_buddy p4_game_api)
target_include_directories(byte_buddy_touch_previews PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../src")
target_compile_features(byte_buddy_touch_previews PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(byte_buddy_touch_previews PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(byte_buddy_touch_previews PRIVATE
        -fsanitize=address,undefined
    )
endif()

add_test(
    NAME byte_buddy_touch_preview_smoke
    COMMAND byte_buddy_touch_previews
        "${BYTE_BUDDY_TEST_ART_PATH}"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-egg.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-hatch.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-power.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-style.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-custom.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-flying.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-elemental.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-play.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-signals.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-track.ppm"
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-preview-battle.ppm"
)

add_test(
    NAME byte_buddy_animation_preview_smoke
    COMMAND byte_buddy_touch_previews
        "${BYTE_BUDDY_TEST_ART_PATH}"
        --animation
        "${CMAKE_CURRENT_BINARY_DIR}/byte-buddy-animation"
        178
)
set_tests_properties(byte_buddy_animation_preview_smoke PROPERTIES TIMEOUT 30)

add_test(
    NAME byte_buddy_signal_motion_preview_smoke
    COMMAND byte_buddy_touch_previews
        "${BYTE_BUDDY_TEST_ART_PATH}"
        --signal-motion
        -
        # Need-qualified Feed/Clean/Rest/Pet seeding fixes this motion budget.
        955
)
set_tests_properties(byte_buddy_signal_motion_preview_smoke PROPERTIES
    TIMEOUT 30)

add_test(
    NAME byte_buddy_authored_motion_preview_smoke
    COMMAND byte_buddy_touch_previews
        "${BYTE_BUDDY_TEST_ART_PATH}"
        --authored-motion
        -
        112
)
set_tests_properties(byte_buddy_authored_motion_preview_smoke PROPERTIES
    TIMEOUT 60)
