idf_component_register(
    SRCS "src/tab5.c" "src/sensors.c" "src/sensor_decode.c" "src/rtc_clock.c"
    INCLUDE_DIRS "include"
    REQUIRES platform_board esp_driver_i2c esp_lcd
        espressif__esp_io_expander espressif__esp_io_expander_pi4ioe5v6408
    PRIV_REQUIRES freertos esp_driver_gpio esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
