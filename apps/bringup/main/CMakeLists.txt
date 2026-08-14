idf_component_register(
    SRCS "bringup_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_common
        esp_hw_support
        esp_psram
        esp_system
        freertos
        heap
        log
        spi_flash
)
