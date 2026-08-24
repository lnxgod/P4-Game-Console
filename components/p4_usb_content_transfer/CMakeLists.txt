# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS
        "src/content_transfer.c"
        "src/file_transfer.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_timer
        esp_system
        freertos
        heap
        log
        mbedtls
        p4_content_catalog
        p4_game_package
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
