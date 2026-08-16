# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/platform_game_loader.c"
    INCLUDE_DIRS "include"
    REQUIRES elf_loader mbedtls p4_game_api p4_game_package
             platform_game_catalog platform_game_storage
)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
