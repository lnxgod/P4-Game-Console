idf_component_register(SRCS "src/dice_ble.c" INCLUDE_DIRS "include"
    REQUIRES bt esp_timer esp_system freertos p4_multiplayer platform_ble_host)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
