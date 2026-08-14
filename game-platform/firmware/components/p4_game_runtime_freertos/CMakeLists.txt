if(NOT COMMAND idf_component_register)
    message(FATAL_ERROR "p4_game_runtime_freertos is an ESP-IDF-only component")
endif()

idf_component_register(
    SRCS "src/runtime_freertos.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_timer
        esp_system
        freertos
        p4_game_api
        p4_game_runtime_core
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wconversion -Wshadow -std=c11
)
