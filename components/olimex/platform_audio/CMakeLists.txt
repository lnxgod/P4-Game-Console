# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_audio_olimex.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        espressif__esp_codec_dev
        freertos
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror -Wno-sign-conversion
)
