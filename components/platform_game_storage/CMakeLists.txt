set(PLATFORM_GAME_STORAGE_MODEL_SOURCES
    src/game_storage_model.c
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            src/platform_game_storage.c
            ${PLATFORM_GAME_STORAGE_MODEL_SOURCES}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES
            esp_partition
            esp_tinyusb
            fatfs
            freertos
            mbedtls
            wear_levelling
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
        -Wno-sign-conversion
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_game_storage VERSION 0.1.0 LANGUAGES C)

    add_library(platform_game_storage_model STATIC
        ${PLATFORM_GAME_STORAGE_MODEL_SOURCES}
    )
    target_include_directories(platform_game_storage_model
        PUBLIC src
    )
    target_compile_features(platform_game_storage_model PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_game_storage_model PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(platform_game_storage_model PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
