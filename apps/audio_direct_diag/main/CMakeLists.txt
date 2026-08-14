idf_component_register(
    SRCS "audio_direct_diag_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_hw_support
        esp_rom
        esp_timer
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
    # ESP-IDF 5.5.3 public RISC-V headers contain signedness conversions.
    -Wno-sign-conversion
)
