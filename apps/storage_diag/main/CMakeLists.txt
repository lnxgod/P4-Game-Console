idf_component_register(
    SRCS "storage_diag_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        log
        platform_storage
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
)
