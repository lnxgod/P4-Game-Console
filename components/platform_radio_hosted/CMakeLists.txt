set(P4_RADIO_BOARD_REQUIREMENTS platform_board)
idf_build_get_property(P4_RADIO_BOARD P4_BOARD_PROFILE)
if(P4_RADIO_BOARD STREQUAL "m5stack-tab5")
    list(APPEND P4_RADIO_BOARD_REQUIREMENTS platform_tab5)
endif()
# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_radio_hosted.c"
    INCLUDE_DIRS "include"
    REQUIRES
        ${P4_RADIO_BOARD_REQUIREMENTS}
        esp_hosted
        esp_driver_sdmmc
        freertos
        log
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
