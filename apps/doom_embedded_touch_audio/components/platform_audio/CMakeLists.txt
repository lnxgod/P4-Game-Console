# SPDX-License-Identifier: GPL-2.0-or-later

idf_component_register(
    SRCS "src/platform_audio_adapter.c"
    INCLUDE_DIRS "include"
    REQUIRES platform_audio_factory
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
