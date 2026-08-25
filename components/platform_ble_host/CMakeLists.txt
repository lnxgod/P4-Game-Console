# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_ble_host.c"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        freertos
        heap
        log
        platform_radio_hosted
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
