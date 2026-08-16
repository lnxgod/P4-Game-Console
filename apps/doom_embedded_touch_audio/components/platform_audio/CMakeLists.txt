# SPDX-License-Identifier: GPL-2.0-or-later

set(PLATFORM_AUDIO_SOURCES "src/platform_audio_adapter.c")
set(PLATFORM_AUDIO_REQUIRES
    platform_audio_factory
    platform_audio_es8311)
if(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    set(PLATFORM_AUDIO_SOURCES "src/platform_audio_waveshare_adapter.c")
endif()

idf_component_register(
    SRCS ${PLATFORM_AUDIO_SOURCES}
    INCLUDE_DIRS "include"
    REQUIRES ${PLATFORM_AUDIO_REQUIRES}
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
