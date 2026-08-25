# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_gamepad.c"
    INCLUDE_DIRS "include"
    REQUIRES gamepad_core
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
