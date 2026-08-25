idf_component_register(
    SRCS src/platform_console_settings.c
    INCLUDE_DIRS include
    REQUIRES esp_hw_support gamepad_core nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
)
