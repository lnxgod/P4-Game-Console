# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.4.69")

if(NOT DEFINED P4_BOARD_PROFILE)
    set(P4_BOARD_PROFILE "elecrow-crowpanel-advanced-10")
endif()
if(NOT DEFINED P4_WAVESHARE_CONTROLLER_FIRST_BUILD)
    set(P4_WAVESHARE_CONTROLLER_FIRST_BUILD OFF)
endif()
if(NOT DEFINED P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD)
    set(P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD OFF)
endif()
if(NOT DEFINED P4_WAVESHARE_BLE_MULTIPLAYER_BUILD)
    set(P4_WAVESHARE_BLE_MULTIPLAYER_BUILD ON)
endif()
if(NOT P4_BOARD_PROFILE STREQUAL "elecrow-crowpanel-advanced-10" AND
   NOT P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" AND
   NOT P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    message(FATAL_ERROR "unsupported P4_BOARD_PROFILE=${P4_BOARD_PROFILE}")
endif()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components/console_shell"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_ansi"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_bbs"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_cp437"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_board"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_console_settings"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_display"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_catalog"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_loader"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_game_storage"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_os_update"
    "${CMAKE_CURRENT_LIST_DIR}/components/platform_readonly_blob"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_content_catalog"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_desktop"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_save"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_package"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_multiplayer"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_multiplayer"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_usb_content_transfer"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_os_update_package"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_script_audio"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_script_renderer"
    "${CMAKE_CURRENT_LIST_DIR}/../../game-platform/firmware/components/p4_game_runtime_core"
    "${CMAKE_CURRENT_LIST_DIR}/../../game-platform/firmware/components/p4_lua_runtime"
)

if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../components/board_deps_olimex"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/olimex/platform_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_gamepad_input"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/gamepad_core"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_platform"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_gamepad_usb"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_usb_host"
        "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
    )
elseif(P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../components/board_deps_waveshare"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_embedded_touch_audio/components/platform_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_gamepad_input"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_touch_input"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/gamepad_core"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_tinyusb_dual"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_es8311"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_factory"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_gamepad_usb"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_i2c_shared"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_touch"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_usb_host"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_platform"
        "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
    )
    if(P4_WAVESHARE_BLE_MULTIPLAYER_BUILD OR
       P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD)
        list(APPEND EXTRA_COMPONENT_DIRS
            "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_radio_hosted"
        )
    endif()
    if(P4_WAVESHARE_BLE_MULTIPLAYER_BUILD)
        list(APPEND EXTRA_COMPONENT_DIRS
            "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_multiplayer_ble"
        )
    endif()
    if(P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD)
        list(APPEND EXTRA_COMPONENT_DIRS
            "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_signal_scan"
            "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_signal_scan"
        )
    endif()
else()
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../../components/board_deps_elecrow"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_embedded_touch_audio/components/platform_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_touch_input"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/doom_video"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_audio_factory"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_i2c_shared"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/platform_touch"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_platform"
        "${CMAKE_CURRENT_LIST_DIR}/../doom/components/doom_audio"
        "${CMAKE_CURRENT_LIST_DIR}/../doom_audio_probe/components/doom_engine_audio"
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
    # The two hardware profiles intentionally resolve different managed
    # component graphs. Keep each resolution independently authoritative.
    idf_build_set_property(DEPENDENCIES_LOCK dependencies-olimex.lock)
elseif(P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    idf_build_set_property(DEPENDENCIES_LOCK dependencies-waveshare.lock)
endif()
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(P4_BOARD_PROFILE "${P4_BOARD_PROFILE}")
idf_build_set_property(P4_WAVESHARE_CONTROLLER_FIRST_BUILD
    "${P4_WAVESHARE_CONTROLLER_FIRST_BUILD}")
idf_build_set_property(P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD
    "${P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD}")
idf_build_set_property(P4_WAVESHARE_BLE_MULTIPLAYER_BUILD
    "${P4_WAVESHARE_BLE_MULTIPLAYER_BUILD}")
project(p4_console_os)

if(P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3" AND
   (P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD OR
    P4_WAVESHARE_BLE_MULTIPLAYER_BUILD))
    if(NOT CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE OR
       NOT CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE EQUAL 54 OR
       NOT CONFIG_ESP_HOSTED_SDIO_PIN_CLK EQUAL 18 OR
       NOT CONFIG_ESP_HOSTED_SDIO_PIN_CMD EQUAL 19 OR
       NOT CONFIG_ESP_HOSTED_SDIO_PIN_D0 EQUAL 14 OR
       NOT CONFIG_ESP_HOSTED_SDIO_PIN_D1 EQUAL 15 OR
       NOT CONFIG_ESP_HOSTED_SDIO_PIN_D2 EQUAL 16 OR
       NOT CONFIG_ESP_HOSTED_SDIO_PIN_D3 EQUAL 17)
        message(FATAL_ERROR
            "Waveshare radio services require their exact SDIO transport; "
            "refusing an ESP-Hosted fallback that can claim touch/display/audio pins")
    endif()
    if(P4_WAVESHARE_BLE_MULTIPLAYER_BUILD AND
       (NOT CONFIG_BT_ENABLED OR
        NOT CONFIG_BT_CONTROLLER_DISABLED OR
        NOT CONFIG_BT_NIMBLE_ENABLED OR
        NOT CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE OR
        NOT CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI))
        message(FATAL_ERROR
            "Waveshare BLE multiplayer requires the committed NimBLE/VHCI "
            "sdkconfig overlay; rebuild with scripts/build-waveshare-console-os.sh")
    endif()
endif()
idf_build_get_property(P4_IDF_PYTHON PYTHON)
get_filename_component(P4_REPOSITORY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# The controller-first candidate may replace only exact locked USB translation
# units with deterministic build-directory overlays. The managed Component
# Manager package and hub scheduler remain byte-for-byte untouched.
set(P4_USB_COMPONENT_OVERLAY_SOURCES)
if(P4_WAVESHARE_CONTROLLER_FIRST_BUILD AND
   CONFIG_P4_WAVESHARE_H2_FORCE_FULL_SPEED_HOST)
    set(P4_USB_HCD_UPSTREAM
        "${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__usb/src/hcd_dwc.c")
    set(P4_USB_HCD_GENERATOR
        "${P4_REPOSITORY_ROOT}/scripts/generate-espressif-usb-hcd-fsls-overlay.py")
    set(P4_USB_HCD_GENERATED
        "${CMAKE_BINARY_DIR}/generated/espressif-usb-1.5.0/hcd_dwc.c")
    execute_process(
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_USB_HCD_GENERATOR}"
            --input "${P4_USB_HCD_UPSTREAM}"
            --output "${P4_USB_HCD_GENERATED}"
        RESULT_VARIABLE P4_USB_HCD_GENERATE_RESULT
        ERROR_VARIABLE P4_USB_HCD_GENERATE_ERROR
    )
    if(NOT P4_USB_HCD_GENERATE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "failed to generate guarded hcd_dwc.c FS/LS overlay: "
            "${P4_USB_HCD_GENERATE_ERROR}")
    endif()
    add_custom_command(
        OUTPUT "${P4_USB_HCD_GENERATED}"
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_USB_HCD_GENERATOR}"
            --input "${P4_USB_HCD_UPSTREAM}"
            --output "${P4_USB_HCD_GENERATED}"
        DEPENDS
            "${P4_USB_HCD_UPSTREAM}"
            "${P4_USB_HCD_GENERATOR}"
        VERBATIM
    )
    set_source_files_properties(
        "${P4_USB_HCD_GENERATED}" PROPERTIES GENERATED TRUE)
    list(APPEND P4_USB_COMPONENT_OVERLAY_SOURCES
        "${P4_USB_HCD_GENERATED}")
endif()

if(P4_WAVESHARE_CONTROLLER_FIRST_BUILD AND
   CONFIG_P4_USB_HOST_EXT_PORT_ENUM_RETRY_ATTEMPTS GREATER 0)
    set(P4_USB_EXT_PORT_UPSTREAM
        "${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__usb/src/ext_port.c")
    set(P4_USB_EXT_PORT_GENERATOR
        "${P4_REPOSITORY_ROOT}/scripts/generate-espressif-usb-ext-port-overlay.py")
    set(P4_USB_EXT_PORT_GENERATED
        "${CMAKE_BINARY_DIR}/generated/espressif-usb-1.5.0/ext_port.c")
    execute_process(
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_USB_EXT_PORT_GENERATOR}"
            --input "${P4_USB_EXT_PORT_UPSTREAM}"
            --output "${P4_USB_EXT_PORT_GENERATED}"
        RESULT_VARIABLE P4_USB_EXT_PORT_GENERATE_RESULT
        ERROR_VARIABLE P4_USB_EXT_PORT_GENERATE_ERROR
    )
    if(NOT P4_USB_EXT_PORT_GENERATE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "failed to generate serialized ext_port.c overlay: "
            "${P4_USB_EXT_PORT_GENERATE_ERROR}")
    endif()
    add_custom_command(
        OUTPUT "${P4_USB_EXT_PORT_GENERATED}"
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_USB_EXT_PORT_GENERATOR}"
            --input "${P4_USB_EXT_PORT_UPSTREAM}"
            --output "${P4_USB_EXT_PORT_GENERATED}"
        DEPENDS
            "${P4_USB_EXT_PORT_UPSTREAM}"
            "${P4_USB_EXT_PORT_GENERATOR}"
        VERBATIM
    )
    set_source_files_properties(
        "${P4_USB_EXT_PORT_GENERATED}" PROPERTIES GENERATED TRUE)
    list(APPEND P4_USB_COMPONENT_OVERLAY_SOURCES
        "${P4_USB_EXT_PORT_GENERATED}")
endif()

if(P4_USB_COMPONENT_OVERLAY_SOURCES)
    idf_component_get_property(
        P4_USB_COMPONENT_LIB espressif__usb COMPONENT_LIB)
    get_target_property(
        P4_USB_COMPONENT_SOURCES ${P4_USB_COMPONENT_LIB} SOURCES)
    set(P4_USB_COMPONENT_FILTERED_SOURCES)
    set(P4_USB_EXT_PORT_SOURCE_MATCHES 0)
    set(P4_USB_HCD_SOURCE_MATCHES 0)
    foreach(P4_USB_COMPONENT_SOURCE IN LISTS P4_USB_COMPONENT_SOURCES)
        set(P4_USB_COMPONENT_SOURCE_REPLACED FALSE)
        if(DEFINED P4_USB_EXT_PORT_UPSTREAM)
            if(P4_USB_COMPONENT_SOURCE STREQUAL P4_USB_EXT_PORT_UPSTREAM OR
               P4_USB_COMPONENT_SOURCE MATCHES
                   "managed_components/espressif__usb/src/ext_port\\.c$")
                math(EXPR P4_USB_EXT_PORT_SOURCE_MATCHES
                    "${P4_USB_EXT_PORT_SOURCE_MATCHES} + 1")
                set(P4_USB_COMPONENT_SOURCE_REPLACED TRUE)
            endif()
        endif()
        if(DEFINED P4_USB_HCD_UPSTREAM)
            if(P4_USB_COMPONENT_SOURCE STREQUAL P4_USB_HCD_UPSTREAM OR
               P4_USB_COMPONENT_SOURCE MATCHES
                   "managed_components/espressif__usb/src/hcd_dwc\\.c$")
                math(EXPR P4_USB_HCD_SOURCE_MATCHES
                    "${P4_USB_HCD_SOURCE_MATCHES} + 1")
                set(P4_USB_COMPONENT_SOURCE_REPLACED TRUE)
            endif()
        endif()
        if(NOT P4_USB_COMPONENT_SOURCE_REPLACED)
            list(APPEND P4_USB_COMPONENT_FILTERED_SOURCES
                "${P4_USB_COMPONENT_SOURCE}")
        endif()
    endforeach()
    if(DEFINED P4_USB_EXT_PORT_UPSTREAM AND
       NOT P4_USB_EXT_PORT_SOURCE_MATCHES EQUAL 1)
        message(FATAL_ERROR
            "expected exactly one locked espressif__usb ext_port.c source; "
            "found ${P4_USB_EXT_PORT_SOURCE_MATCHES}")
    endif()
    if(DEFINED P4_USB_HCD_UPSTREAM AND
       NOT P4_USB_HCD_SOURCE_MATCHES EQUAL 1)
        message(FATAL_ERROR
            "expected exactly one locked espressif__usb hcd_dwc.c source; "
            "found ${P4_USB_HCD_SOURCE_MATCHES}")
    endif()
    set_property(TARGET ${P4_USB_COMPONENT_LIB} PROPERTY SOURCES
        "${P4_USB_COMPONENT_FILTERED_SOURCES}")
    target_sources(${P4_USB_COMPONENT_LIB} PRIVATE
        ${P4_USB_COMPONENT_OVERLAY_SOURCES})
endif()

# The locked ESP-Hosted 1.4.7 component registers a C constructor that starts
# SDIO before app_main. Console OS owns storage and launch ordering, so the
# Waveshare images remove only that constructor translation unit and call
# esp_hosted_init() from the selected radio service after the launcher is live.
# This keeps the controller-first H2 image from losing Byte Buddy's passive
# signal capability while preserving explicit OS-owned startup ordering. The
# managed component contents remain untouched and authoritative.
if(P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3" AND
   (P4_WAVESHARE_EXPERIMENTAL_SIGNAL_SCAN_BUILD OR
    P4_WAVESHARE_BLE_MULTIPLAYER_BUILD))
    idf_component_get_property(
        P4_HOSTED_COMPONENT_LIB espressif__esp_hosted COMPONENT_LIB)
    get_target_property(
        P4_HOSTED_COMPONENT_SOURCES ${P4_HOSTED_COMPONENT_LIB} SOURCES)
    set(P4_HOSTED_COMPONENT_FILTERED_SOURCES)
    set(P4_HOSTED_AUTOSTART_SOURCE_MATCHES 0)
    foreach(P4_HOSTED_COMPONENT_SOURCE IN LISTS P4_HOSTED_COMPONENT_SOURCES)
        if(P4_HOSTED_COMPONENT_SOURCE MATCHES
           "(^|/)host/esp_hosted_host_init\\.c$")
            math(EXPR P4_HOSTED_AUTOSTART_SOURCE_MATCHES
                "${P4_HOSTED_AUTOSTART_SOURCE_MATCHES} + 1")
        else()
            list(APPEND P4_HOSTED_COMPONENT_FILTERED_SOURCES
                "${P4_HOSTED_COMPONENT_SOURCE}")
        endif()
    endforeach()
    if(NOT P4_HOSTED_AUTOSTART_SOURCE_MATCHES EQUAL 1)
        message(FATAL_ERROR
            "expected exactly one locked ESP-Hosted autostart source; "
            "found ${P4_HOSTED_AUTOSTART_SOURCE_MATCHES}")
    endif()
    set_property(TARGET ${P4_HOSTED_COMPONENT_LIB} PROPERTY SOURCES
        "${P4_HOSTED_COMPONENT_FILTERED_SOURCES}")
endif()

# ESP-Hosted 1.4.7 creates its non-ISR worker stacks before the SDIO card's
# mandatory DMA alignment buffer. A late, opt-in BLE start can therefore drain
# internal DMA RAM even though 32 MiB of PSRAM is idle. Generate an exact-source
# overlay that uses ESP-IDF's supported WithCaps API for those worker stacks;
# the managed component and lockfile remain untouched.
if(P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3" AND
   P4_WAVESHARE_BLE_MULTIPLAYER_BUILD)
    set(P4_HOSTED_OS_WRAPPER_UPSTREAM
        "${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__esp_hosted/host/port/src/os_wrapper.c")
    set(P4_HOSTED_OS_WRAPPER_GENERATOR
        "${P4_REPOSITORY_ROOT}/scripts/generate-esp-hosted-psram-task-overlay.py")
    set(P4_HOSTED_OS_WRAPPER_GENERATED
        "${CMAKE_BINARY_DIR}/generated/esp-hosted-1.4.7/os_wrapper.c")
    execute_process(
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_HOSTED_OS_WRAPPER_GENERATOR}"
            --input "${P4_HOSTED_OS_WRAPPER_UPSTREAM}"
            --output "${P4_HOSTED_OS_WRAPPER_GENERATED}"
        RESULT_VARIABLE P4_HOSTED_OS_WRAPPER_GENERATE_RESULT
        ERROR_VARIABLE P4_HOSTED_OS_WRAPPER_GENERATE_ERROR
    )
    if(NOT P4_HOSTED_OS_WRAPPER_GENERATE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "failed to generate ESP-Hosted PSRAM task overlay: "
            "${P4_HOSTED_OS_WRAPPER_GENERATE_ERROR}")
    endif()
    add_custom_command(
        OUTPUT "${P4_HOSTED_OS_WRAPPER_GENERATED}"
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_HOSTED_OS_WRAPPER_GENERATOR}"
            --input "${P4_HOSTED_OS_WRAPPER_UPSTREAM}"
            --output "${P4_HOSTED_OS_WRAPPER_GENERATED}"
        DEPENDS
            "${P4_HOSTED_OS_WRAPPER_UPSTREAM}"
            "${P4_HOSTED_OS_WRAPPER_GENERATOR}"
        VERBATIM
    )
    set_source_files_properties(
        "${P4_HOSTED_OS_WRAPPER_GENERATED}" PROPERTIES GENERATED TRUE)
    idf_component_get_property(
        P4_HOSTED_COMPONENT_LIB espressif__esp_hosted COMPONENT_LIB)
    get_target_property(
        P4_HOSTED_COMPONENT_SOURCES ${P4_HOSTED_COMPONENT_LIB} SOURCES)
    set(P4_HOSTED_COMPONENT_FILTERED_SOURCES)
    set(P4_HOSTED_OS_WRAPPER_SOURCE_MATCHES 0)
    foreach(P4_HOSTED_COMPONENT_SOURCE IN LISTS P4_HOSTED_COMPONENT_SOURCES)
        if(P4_HOSTED_COMPONENT_SOURCE STREQUAL
               P4_HOSTED_OS_WRAPPER_UPSTREAM OR
           P4_HOSTED_COMPONENT_SOURCE MATCHES
               "managed_components/espressif__esp_hosted/host/port/src/os_wrapper\\.c$")
            math(EXPR P4_HOSTED_OS_WRAPPER_SOURCE_MATCHES
                "${P4_HOSTED_OS_WRAPPER_SOURCE_MATCHES} + 1")
        else()
            list(APPEND P4_HOSTED_COMPONENT_FILTERED_SOURCES
                "${P4_HOSTED_COMPONENT_SOURCE}")
        endif()
    endforeach()
    if(NOT P4_HOSTED_OS_WRAPPER_SOURCE_MATCHES EQUAL 1)
        message(FATAL_ERROR
            "expected exactly one locked ESP-Hosted os_wrapper.c source; "
            "found ${P4_HOSTED_OS_WRAPPER_SOURCE_MATCHES}")
    endif()
    set_property(TARGET ${P4_HOSTED_COMPONENT_LIB} PROPERTY SOURCES
        "${P4_HOSTED_COMPONENT_FILTERED_SOURCES}")
    target_sources(${P4_HOSTED_COMPONENT_LIB} PRIVATE
        "${P4_HOSTED_OS_WRAPPER_GENERATED}")
endif()

# A reviewed full-project install seeds a valid wear-levelled FAT volume.
# Subsequent app-only updates leave this persistent partition untouched.
if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" OR
   P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    set(P4_GAME_STORAGE_SEED_DIR "${CMAKE_BINARY_DIR}/sd-card")
    # This is a generated staging tree, never a mounted user card. Recreate it
    # on configure so stale packages and host-created duplicate filenames can
    # never leak into a release bundle.
    file(REMOVE_RECURSE "${P4_GAME_STORAGE_SEED_DIR}")
else()
    set(P4_GAME_STORAGE_SEED_DIR "${CMAKE_BINARY_DIR}/game-storage-seed")
endif()
set(P4_LOCAL_DOOM_WAD
    "${CMAKE_CURRENT_LIST_DIR}/../../local-data/doom/doom1.wad")
if(NOT EXISTS "${P4_LOCAL_DOOM_WAD}")
    message(FATAL_ERROR
        "console_os requires the ignored Doom 1.9 shareware WAD")
endif()
file(SIZE "${P4_LOCAL_DOOM_WAD}" P4_LOCAL_DOOM_WAD_BYTES)
file(SHA256 "${P4_LOCAL_DOOM_WAD}" P4_LOCAL_DOOM_WAD_SHA256)
if(NOT P4_LOCAL_DOOM_WAD_BYTES EQUAL 4196020 OR
   NOT P4_LOCAL_DOOM_WAD_SHA256 STREQUAL
       "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771")
    message(FATAL_ERROR "local Doom shareware WAD identity does not match")
endif()
file(MAKE_DIRECTORY "${P4_GAME_STORAGE_SEED_DIR}")
file(MAKE_DIRECTORY "${P4_GAME_STORAGE_SEED_DIR}/UPDATE")
file(MAKE_DIRECTORY "${P4_GAME_STORAGE_SEED_DIR}/P4/GAMES")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/game-storage/README.TXT"
    "${P4_GAME_STORAGE_SEED_DIR}/README.TXT"
    COPYONLY
)
if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/game-storage/README-OLIMEX.TXT"
        "${P4_GAME_STORAGE_SEED_DIR}/README.TXT"
        COPYONLY
    )
endif()
configure_file(
    "${P4_LOCAL_DOOM_WAD}"
    "${P4_GAME_STORAGE_SEED_DIR}/DOOM1.WAD"
    COPYONLY
)

# Seed two open, source-included games for the sandboxed Lua runtime. They
# remain additive to native P4G games and are loaded only from the SD bundle.
set(P4_SCRIPT_CART_OUTPUTS)
function(p4_add_script_cart_seed OUTPUT_NAME TEMPLATE_NAME)
    set(P4_CART_OUTPUT
        "${P4_GAME_STORAGE_SEED_DIR}/P4/GAMES/${OUTPUT_NAME}")
    file(GLOB_RECURSE P4_CART_SOURCES CONFIGURE_DEPENDS
        "${P4_REPOSITORY_ROOT}/game-platform/templates/${TEMPLATE_NAME}/*")
    add_custom_command(
        OUTPUT "${P4_CART_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${P4_CART_OUTPUT}"
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_REPOSITORY_ROOT}/game-platform/scripts/p4cart.py"
            pack
            "${P4_REPOSITORY_ROOT}/game-platform/templates/${TEMPLATE_NAME}"
            "${P4_CART_OUTPUT}"
        DEPENDS
            "${P4_REPOSITORY_ROOT}/game-platform/scripts/p4cart.py"
            ${P4_CART_SOURCES}
        VERBATIM
    )
    set(P4_SCRIPT_CART_OUTPUTS
        ${P4_SCRIPT_CART_OUTPUTS} "${P4_CART_OUTPUT}" PARENT_SCOPE)
endfunction()

p4_add_script_cart_seed("BOUNCE-LAB.P4CART" "bounce-lab")
p4_add_script_cart_seed("QR-DODGE.P4CART" "qr-dodge")
add_custom_target(p4_script_cart_seed ALL
    DEPENDS ${P4_SCRIPT_CART_OUTPUTS})

string(REPLACE "-gcc" "-strip" P4_RISCV_STRIP "${CMAKE_C_COMPILER}")
set(P4_PACKAGE_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/../../scripts/build-game-package.py")
set(P4_RESOURCE_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/../../scripts/build-game-resource.py")
set(P4_CARTRIDGE_COMMON_SOURCES
    "${P4_PACKAGE_SCRIPT}"
    "${CMAKE_CURRENT_LIST_DIR}/../../game-platform/runtime/cartridge_main.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_cp437/include/p4/cp437.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_cp437/src/cp437.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/include/p4/cartridge.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/include/p4/game.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/include/p4/audio_pack.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/include/p4/feedback.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/include/p4/visual.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/include/p4/draw.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/audio_pack.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/draw.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/feedback.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/generated/audio_pack.inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/generated/feedback_atlas.inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/game_runtime.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/input.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api/src/visual.c"
)

function(p4_add_seed_game PACKAGE_NAME GAME_NAME RESOURCE_NAME RESOURCE_PAYLOAD)
    set(P4_GAME_DIR
        "${P4_REPOSITORY_ROOT}/games/${GAME_NAME}")
    set(P4_PACKAGE_OUTPUT
        "${P4_GAME_STORAGE_SEED_DIR}/GAMES/${PACKAGE_NAME}")
    file(GLOB_RECURSE P4_GAME_SOURCES CONFIGURE_DEPENDS
        "${P4_GAME_DIR}/*.c" "${P4_GAME_DIR}/*.h")
    add_custom_command(
        OUTPUT "${P4_PACKAGE_OUTPUT}"
        COMMAND
            "${P4_IDF_PYTHON}"
            "${P4_PACKAGE_SCRIPT}"
            --manifest "${P4_GAME_DIR}/game.json"
            --compiler "${CMAKE_C_COMPILER}"
            --strip "${P4_RISCV_STRIP}"
            --output "${P4_PACKAGE_OUTPUT}"
        DEPENDS
            ${P4_CARTRIDGE_COMMON_SOURCES}
            ${P4_GAME_SOURCES}
            "${P4_GAME_DIR}/game.json"
        VERBATIM
    )
    set(P4_SEEDED_GAME_PACKAGES
        ${P4_SEEDED_GAME_PACKAGES} "${P4_PACKAGE_OUTPUT}"
        PARENT_SCOPE)
    if(NOT RESOURCE_NAME STREQUAL "")
        set(P4_RESOURCE_PAYLOAD
            "${P4_GAME_DIR}/${RESOURCE_PAYLOAD}")
        set(P4_RESOURCE_OUTPUT
            "${P4_GAME_STORAGE_SEED_DIR}/GAMES/${RESOURCE_NAME}")
        add_custom_command(
            OUTPUT "${P4_RESOURCE_OUTPUT}"
            COMMAND
                "${P4_IDF_PYTHON}"
                "${P4_RESOURCE_SCRIPT}"
                --manifest "${P4_GAME_DIR}/game.json"
                --payload "${P4_RESOURCE_PAYLOAD}"
                --output "${P4_RESOURCE_OUTPUT}"
            DEPENDS
                "${P4_RESOURCE_SCRIPT}"
                "${P4_RESOURCE_PAYLOAD}"
                "${P4_GAME_DIR}/game.json"
            VERBATIM
        )
        set(P4_SEEDED_GAME_PACKAGES
            ${P4_SEEDED_GAME_PACKAGES} "${P4_PACKAGE_OUTPUT}"
            "${P4_RESOURCE_OUTPUT}"
            PARENT_SCOPE)
    endif()
endfunction()

file(GLOB P4_GAME_MANIFESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../../games/*/game.json")
set(P4_GENERATED_GAMES_CMAKE "${CMAKE_BINARY_DIR}/p4-games.cmake")
execute_process(
    COMMAND
        "${P4_IDF_PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/generate-game-registry.py"
        --games-root "${CMAKE_CURRENT_LIST_DIR}/../../games"
        --output-cmake "${P4_GENERATED_GAMES_CMAKE}"
    RESULT_VARIABLE P4_GAME_MANIFEST_RESULT
    OUTPUT_VARIABLE P4_GAME_MANIFEST_REPORT
    ERROR_VARIABLE P4_GAME_MANIFEST_ERROR
)
if(NOT P4_GAME_MANIFEST_RESULT EQUAL 0)
    message(FATAL_ERROR
        "game manifest validation failed: ${P4_GAME_MANIFEST_ERROR}")
endif()
message(STATUS "${P4_GAME_MANIFEST_REPORT}")
include("${P4_GENERATED_GAMES_CMAKE}")
add_custom_target(p4_game_packages ALL DEPENDS ${P4_SEEDED_GAME_PACKAGES})

if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" OR
   P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    add_custom_target(p4_sd_card_bundle ALL
        DEPENDS p4_game_packages p4_script_cart_seed
        COMMENT "Preparing removable microSD game/update bundle"
    )
else()
    fatfs_create_spiflash_image(
        game_data "${P4_GAME_STORAGE_SEED_DIR}" FLASH_IN_PROJECT)
    add_dependencies(
        fatfs_game_data_bin p4_game_packages p4_script_cart_seed)
    add_custom_command(TARGET fatfs_game_data_bin POST_BUILD
        COMMAND
            "${P4_IDF_PYTHON}"
            "${CMAKE_CURRENT_LIST_DIR}/../../scripts/normalize-game-storage-image.py"
            "${CMAKE_BINARY_DIR}/game_data.bin"
        VERBATIM
    )
endif()

set(P4_OS_UPDATE_OUTPUT "${CMAKE_BINARY_DIR}/P4UPDATE.P4U")
add_custom_target(p4_os_update_package ALL
    COMMAND
        "${P4_IDF_PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/build-os-update.py"
        --image "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
        --version "${PROJECT_VER}"
        --source-root "${CMAKE_CURRENT_LIST_DIR}/../.."
        --output "${P4_OS_UPDATE_OUTPUT}"
    DEPENDS
        gen_project_binary
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/build-os-update.py"
    BYPRODUCTS "${P4_OS_UPDATE_OUTPUT}"
    VERBATIM
)
if(P4_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc" OR
   P4_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
    add_custom_command(TARGET p4_os_update_package POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${P4_OS_UPDATE_OUTPUT}"
            "${P4_GAME_STORAGE_SEED_DIR}/UPDATE/P4UPDATE.P4U"
        VERBATIM
    )
endif()
