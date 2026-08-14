# SPDX-License-Identifier: GPL-2.0-or-later

idf_component_register(
    SRCS "doom_runtime_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        doom_engine
        doom_video
        esp_timer
        freertos
        log
        platform_display
        platform_storage
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
)
