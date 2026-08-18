set(PLATFORM_AUDIO_ES8311_SOURCES
    src/platform_audio_es8311_policy.c
)
if(P4_BOARD_PROFILE STREQUAL "waveshare-esp32-p4-wifi6-touch-lcd-4.3" OR
   CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    list(APPEND PLATFORM_AUDIO_ES8311_SOURCES src/platform_audio_es8311.c)
endif()

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${PLATFORM_AUDIO_ES8311_SOURCES}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES
            esp_codec_dev
            esp_driver_gpio
            esp_driver_i2c
            esp_driver_i2s
            freertos
            platform_board
    )

    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wshadow
        -Werror
        -Wno-sign-conversion
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_audio_es8311_policy VERSION 0.1.0 LANGUAGES C)

    add_library(platform_audio_es8311_policy STATIC
        src/platform_audio_es8311_policy.c)
    target_include_directories(platform_audio_es8311_policy
        PUBLIC include
        PRIVATE src "${CMAKE_CURRENT_LIST_DIR}/../platform_board/include"
    )
    target_compile_features(platform_audio_es8311_policy PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_audio_es8311_policy PRIVATE
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
