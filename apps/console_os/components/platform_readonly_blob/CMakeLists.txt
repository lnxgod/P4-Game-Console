# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "platform_game_storage_blob.c"
    INCLUDE_DIRS "include" "../../../../components/platform_readonly_blob/include"
    REQUIRES
        freertos
        esp_timer
        log
        platform_game_storage
        vfs
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
