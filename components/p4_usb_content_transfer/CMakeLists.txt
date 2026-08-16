# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/content_transfer.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_uart
        esp_timer
        freertos
        log
        mbedtls
        p4_content_catalog
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
