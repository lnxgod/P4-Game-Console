# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_radio_hosted.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_hosted
        esp_driver_sdmmc
        freertos
        log
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
