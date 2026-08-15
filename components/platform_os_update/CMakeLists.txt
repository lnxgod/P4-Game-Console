# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_os_update.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_partition mbedtls p4_os_update_package
        platform_game_storage
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
