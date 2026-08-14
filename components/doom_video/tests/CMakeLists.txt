add_executable(doom_video_tests test_doom_video.c)
target_link_libraries(doom_video_tests PRIVATE doom_video)
target_compile_features(doom_video_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(doom_video_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )

    option(DOOM_VIDEO_ENABLE_SANITIZERS
           "Enable ASan and UBSan for native video tests" ON)
    if(DOOM_VIDEO_ENABLE_SANITIZERS)
        target_compile_options(doom_video PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(doom_video PRIVATE -fsanitize=address,undefined)
        target_compile_options(doom_video_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(doom_video_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME doom_video_tests COMMAND doom_video_tests)

# Compile the exact pinned upstream i_video.c into a small proof executable.
# Section garbage collection keeps this focused on cmap_to_fb() while ensuring
# the test exercises the actual immutable RGB565 branch rather than a rewrite.
set(P4_DOOMGENERIC_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/doomgeneric/doomgeneric")
add_executable(doom_pinned_rgb565_tests test_pinned_doom_rgb565.c)
target_include_directories(doom_pinned_rgb565_tests PRIVATE
    "${P4_DOOMGENERIC_DIR}")
target_compile_definitions(doom_pinned_rgb565_tests PRIVATE
    _DEFAULT_SOURCE
    _POSIX_C_SOURCE=200809L
    DOOMGENERIC_RESX=320
    DOOMGENERIC_RESY=200
    NORMALUNIX
    LINUX
    SNDSERV
)
target_compile_features(doom_pinned_rgb565_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(doom_pinned_rgb565_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
        -ffunction-sections
        -fdata-sections
    )
    if(APPLE)
        target_link_options(doom_pinned_rgb565_tests PRIVATE
            -Wl,-dead_strip)
    else()
        target_link_options(doom_pinned_rgb565_tests PRIVATE
            -Wl,--gc-sections)
    endif()
    if(DOOM_VIDEO_ENABLE_SANITIZERS)
        target_compile_options(doom_pinned_rgb565_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(doom_pinned_rgb565_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME doom_pinned_rgb565_tests COMMAND doom_pinned_rgb565_tests)
