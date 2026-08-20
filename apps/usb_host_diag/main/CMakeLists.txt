idf_component_register(
    SRCS "usb_host_diag_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_timer
        freertos
        log
        p4_tinyusb_host
        usb
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wshadow
    -Werror
)
