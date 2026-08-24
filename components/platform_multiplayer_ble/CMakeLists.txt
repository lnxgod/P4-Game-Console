# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_multiplayer_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        esp_timer
        freertos
        log
        p4_multiplayer
        platform_radio_hosted
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
