set(PLATFORM_GAME_STORAGE_MODEL_SOURCES
    src/game_storage_model.c
    src/game_storage_files.c
    src/msc_write_policy.c
)

if(COMMAND idf_component_register)
    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" OR
       P4_ACTIVE_BOARD_PROFILE STREQUAL
           "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
        set(PLATFORM_GAME_STORAGE_BACKEND_SOURCES
            src/platform_game_storage.c
            src/fat_repair.c
        )
        set(PLATFORM_GAME_STORAGE_BACKEND_REQUIREMENTS
            esp_driver_gpio
            esp_driver_sdmmc
            fatfs
            sdmmc
        )
        if(P4_ACTIVE_BOARD_PROFILE STREQUAL
           "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
            if(NOT CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE OR
               CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH)
                list(APPEND PLATFORM_GAME_STORAGE_BACKEND_SOURCES
                    src/msc_write10_wrapper.c
                )
                list(APPEND PLATFORM_GAME_STORAGE_BACKEND_REQUIREMENTS
                    esp_hw_support
                    esp_tinyusb
                )
            endif()
        endif()
    else()
        set(PLATFORM_GAME_STORAGE_BACKEND_SOURCES
            src/platform_game_storage.c
            src/msc_write10_wrapper.c
        )
        set(PLATFORM_GAME_STORAGE_BACKEND_REQUIREMENTS
            esp_partition
            esp_tinyusb
            fatfs
            wear_levelling
        )
    endif()
    idf_component_register(
        SRCS
            ${PLATFORM_GAME_STORAGE_BACKEND_SOURCES}
            ${PLATFORM_GAME_STORAGE_MODEL_SOURCES}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES
            esp_timer
            freertos
            heap
            mbedtls
            ${PLATFORM_GAME_STORAGE_BACKEND_REQUIREMENTS}
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
        -Wno-sign-conversion
    )
    # esp_tinyusb 2.0.1 defers WRITE(10) after copying into one reusable
    # buffer. Route the callback through the platform service so a transfer is
    # committed and read back before the host receives success.
    if(NOT P4_ACTIVE_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" AND
       (NOT CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE OR
        CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH))
        target_link_options(${COMPONENT_LIB} INTERFACE
            "LINKER:--wrap=tud_msc_write10_cb"
            "LINKER:--undefined=__wrap_tud_msc_write10_cb"
            "LINKER:--wrap=tud_msc_start_stop_cb"
            "LINKER:--undefined=__wrap_tud_msc_start_stop_cb"
        )
    endif()
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_game_storage VERSION 0.1.0 LANGUAGES C)

    add_library(platform_game_storage_model STATIC
        ${PLATFORM_GAME_STORAGE_MODEL_SOURCES}
    )
    target_include_directories(platform_game_storage_model
        PUBLIC src include
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
