set(PLATFORM_GAME_STORAGE_MODEL_SOURCES
    src/game_storage_model.c
    src/game_storage_files.c
    src/msc_write_policy.c
    src/trusted_wad_table.c
    src/verified_reader.c
    src/verified_window.c
    src/verified_wad.c
    src/storage_block_read.c
    src/sd_metadata.c
)

# Experimental admission is off unless explicitly selected; the source also
# requires the maintained Tab5 target. This never changes content identity.
option(P4_ARENA_BASE_PSRAM "Keep the fully verified Arena BASE in PSRAM when admitted" OFF)

if(COMMAND idf_component_register)
    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5" OR
       P4_ACTIVE_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" OR
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
    # Generate metadata from exact authoritative local inputs. No WAD payload
    # is linked. Missing/wrong inputs fail generation; stale output is never a
    # successful alternative. Existing legacy targets do not gain this path.
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5" AND
       NOT CMAKE_BUILD_EARLY_EXPANSION)
        get_filename_component(P4_ARENA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
        idf_build_get_property(P4_ARENA_PYTHON PYTHON)
        set(P4_ARENA_DIGEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/arena-digests")
        set(P4_ARENA_DIGEST_SOURCE "${P4_ARENA_DIGEST_DIR}/arena_trusted_digests.c")
        set(P4_ARENA_DIGEST_RECEIPT "${P4_ARENA_DIGEST_DIR}/arena_trusted_digests.json")
        add_custom_command(
            OUTPUT "${P4_ARENA_DIGEST_SOURCE}" "${P4_ARENA_DIGEST_RECEIPT}"
            COMMAND "${P4_ARENA_PYTHON}"
                "${P4_ARENA_ROOT}/scripts/doom/arena-trusted-digests.py"
                --root "${P4_ARENA_ROOT}" --output-dir "${P4_ARENA_DIGEST_DIR}"
            DEPENDS
                "${P4_ARENA_ROOT}/scripts/doom/arena-trusted-digests.py"
                "${P4_ARENA_ROOT}/scripts/doom/arena-content.py"
                "${P4_ARENA_ROOT}/scripts/prepare-game-data.py"
                "${P4_ARENA_ROOT}/third_party/game-data.json"
                "${CMAKE_CURRENT_LIST_DIR}/include/platform/doom_arena_content.h"
                "${P4_ARENA_ROOT}/local-data/doom/arena-compact-v1/ARENA2.WAD"
                "${P4_ARENA_ROOT}/game-data/pure-hades/v0.6/PUREHADES.WAD"
                "${P4_ARENA_ROOT}/local-data/doom/arena-inbox/dwango5/DWANGO5.WAD"
            VERBATIM
        )
        list(APPEND PLATFORM_GAME_STORAGE_BACKEND_SOURCES "${P4_ARENA_DIGEST_SOURCE}")
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
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        P4_ARENA_BASE_PSRAM=$<BOOL:${P4_ARENA_BASE_PSRAM}>
    )
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
        -Wno-sign-conversion
    )
    # esp_tinyusb 2.0.1 defers WRITE(10) after copying into one reusable
    # buffer. Route the callback through the platform service so a transfer is
    # committed and read back before the host receives success.
    if(NOT P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5" AND
       NOT P4_ACTIVE_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" AND
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
