idf_component_register(
    SRCS "wad_provisioner_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        freertos
        log
        mbedtls
        platform_storage
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
    # ESP-IDF 5.5.3's public RISC-V headers contain two signedness
    # conversions. Retain the other conversion diagnostics.
    -Wno-sign-conversion
)
