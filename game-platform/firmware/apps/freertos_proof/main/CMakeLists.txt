idf_component_register(
    SRCS "freertos_proof_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        freertos
        log
        p4_cartridge_transfer
        p4_game_api
        p4_game_runtime_core
        p4_game_runtime_freertos
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wconversion -Wshadow -std=c11
)
