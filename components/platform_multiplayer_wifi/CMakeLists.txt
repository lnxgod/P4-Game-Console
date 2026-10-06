# SPDX-License-Identifier: MIT
idf_component_register(SRCS "src/platform_multiplayer_wifi.c" "src/discovery.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event esp_timer freertos lwip log p4_multiplayer platform_ble_host)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror)
