# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_signal_scan.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_event
        esp_driver_sdmmc
        esp_netif
        esp_wifi
        esp_wifi_remote
        freertos
        nvs_flash
        p4_game_api
        p4_signal_scan
        platform_radio_hosted
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
