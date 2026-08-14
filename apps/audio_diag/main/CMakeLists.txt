idf_component_register(
    SRCS "audio_diag_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_hw_support
        freertos
        log
        platform_audio
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
    # ESP-IDF 5.5.3's public RISC-V headers contain two signedness
    # conversions; retain conversion diagnostics for project source.
    -Wno-sign-conversion
)
