# SPDX-License-Identifier: GPL-2.0-or-later

idf_component_register(
    SRCS "doom_audio_probe_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        doom_audio
        doom_engine_audio
        esp_timer
        freertos
        log
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
)
