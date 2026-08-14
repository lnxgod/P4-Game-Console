idf_component_register(
    SRCS "src/platform_storage_probe.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_sdmmc
        fatfs
        freertos
        heap
        mbedtls
        sdmmc
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
    # ESP-IDF 5.5.3's public RISC-V headers contain signedness conversions.
    -Wno-sign-conversion
)
