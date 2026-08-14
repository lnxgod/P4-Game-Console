idf_component_register(
    SRCS "display_diag_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        freertos
        log
        platform_display
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
)
