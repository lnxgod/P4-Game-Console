# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_gamepad_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        esp_timer
        freertos
        gamepad_core
        log
        mbedtls
        nvs_flash
        platform_ble_host
        platform_gamepad
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
