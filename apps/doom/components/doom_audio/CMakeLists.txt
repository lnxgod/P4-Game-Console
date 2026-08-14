# SPDX-License-Identifier: GPL-2.0-or-later

set(DOOM_AUDIO_CORE_SOURCES
    src/doom_audio_mixer.c
    src/doom_audio_ring.c
)

if(COMMAND idf_component_register)
    set(DOOM_AUDIO_IDF_SOURCES src/doom_audio_runtime.c)
    if(CONFIG_DOOM_AUDIO_ENGINE_ADAPTER)
        list(APPEND DOOM_AUDIO_IDF_SOURCES src/doom_audio_sound_module.c)
    endif()
    idf_component_register(
        SRCS
            ${DOOM_AUDIO_CORE_SOURCES}
            ${DOOM_AUDIO_IDF_SOURCES}
        INCLUDE_DIRS include
        REQUIRES
            freertos
            platform_audio
    )

    if(CONFIG_DOOM_AUDIO_ENGINE_ADAPTER)
        target_include_directories(${COMPONENT_LIB} PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/doomgeneric/doomgeneric"
        )
    endif()

    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wshadow
        -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(doom_audio_core VERSION 0.1.0 LANGUAGES C)

    add_library(doom_audio_core STATIC ${DOOM_AUDIO_CORE_SOURCES})
    target_include_directories(doom_audio_core PUBLIC include)
    target_compile_features(doom_audio_core PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(doom_audio_core PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Werror
        )
    endif()

    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
