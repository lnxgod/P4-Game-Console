# SPDX-License-Identifier: GPL-2.0-or-later

set(PLATFORM_AUDIO_SOURCES "src/platform_audio_adapter.c")
set(PLATFORM_AUDIO_REQUIRES platform_audio_factory)
# During ESP-IDF's minimal-build dependency expansion the final Kconfig values
# are not available yet. Use the configure-time board profile as the primary
# selector so the Waveshare codec component enters the dependency graph; keep
# the Kconfig check for direct component builds.
if(P4_BOARD_PROFILE STREQUAL "waveshare-esp32-p4-wifi6-touch-lcd-4.3" OR
   CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    set(PLATFORM_AUDIO_SOURCES "src/platform_audio_waveshare_adapter.c")
    list(APPEND PLATFORM_AUDIO_REQUIRES platform_audio_es8311)
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
