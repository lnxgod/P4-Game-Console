idf_component_register(
    SRCS "sd_format_diag_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_sdmmc
        fatfs
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
    -Wno-sign-conversion
)
