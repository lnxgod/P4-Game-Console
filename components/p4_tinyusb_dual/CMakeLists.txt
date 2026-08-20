# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(REQUIRES esp_tinyusb espressif__tinyusb)

    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
        idf_component_get_property(P4_TINYUSB_COMPONENT_DIR
            espressif__tinyusb COMPONENT_DIR)
        idf_component_get_property(P4_TINYUSB_LIBRARY
            espressif__tinyusb COMPONENT_LIB)
        set(P4_TINYUSB_SRC "${P4_TINYUSB_COMPONENT_DIR}/src")

        function(p4_require_tinyusb_source P4_RELATIVE_PATH P4_EXPECTED_SHA256)
            file(SHA256
                "${P4_TINYUSB_COMPONENT_DIR}/${P4_RELATIVE_PATH}"
                P4_ACTUAL_SHA256)
            if(NOT P4_ACTUAL_SHA256 STREQUAL "${P4_EXPECTED_SHA256}")
                message(FATAL_ERROR
                    "Pinned TinyUSB source changed: ${P4_RELATIVE_PATH} "
                    "(${P4_ACTUAL_SHA256})")
            endif()
        endfunction()

        p4_require_tinyusb_source("src/host/usbh.c"
            "2b537fc20eb9f6757d53c59896290a4942daa1e4ce3ed892220a13596beb6ab8")
        p4_require_tinyusb_source("src/host/hub.c"
            "36b4fdf26f819a7882e0d9e76c5189ae3819ca5cb600ad902e27b5aa4a2f74cd")
        p4_require_tinyusb_source("src/class/hid/hid_host.c"
            "83856a19edbec34129fc1a7477c371fdced2108e6f9ebf77247974455bb9f2d6")
        p4_require_tinyusb_source("src/portable/synopsys/dwc2/hcd_dwc2.c"
            "3f8291ddcb47f85527f473126aeb84e6f6fcce7ae2f8cfce15fb686d8f5aaad9")

        # esp_tinyusb already compiles TinyUSB's common and device-mode files.
        # Add only the host-side translation units to that exact locked target,
        # producing one dual-role library with no duplicate common symbols.
        target_sources(${P4_TINYUSB_LIBRARY} PRIVATE
            "${P4_TINYUSB_SRC}/host/usbh.c"
            "${P4_TINYUSB_SRC}/host/hub.c"
            "${P4_TINYUSB_SRC}/class/hid/hid_host.c"
            "${P4_TINYUSB_SRC}/portable/synopsys/dwc2/hcd_dwc2.c"
        )
        target_compile_definitions(${P4_TINYUSB_LIBRARY} PUBLIC
            CFG_TUH_ENABLED=1
            CFG_TUH_MAX_SPEED=OPT_MODE_HIGH_SPEED
            CFG_TUH_ENUMERATION_BUFSIZE=512
            CFG_TUH_HUB=1
            CFG_TUH_DEVICE_MAX=4
            CFG_TUH_HID=8
            CFG_TUH_HID_EPIN_BUFSIZE=128
            CFG_TUH_HID_EPOUT_BUFSIZE=128
            CFG_TUH_CDC=0
            CFG_TUH_MSC=0
            CFG_TUH_MIDI=0
            CFG_TUH_MIDI2=0
            CFG_TUH_VENDOR=0
            CFG_TUH_DWC2_DMA_ENABLE=0
            CFG_TUH_DWC2_SLAVE_ENABLE=1
            CFG_TUH_DWC2_ENDPOINT_MAX=16
            CFG_TUH_MEM_ALIGN=__attribute__\(\(aligned\(64\)\)\)
            BOARD_TUH_RHPORT=1
            BOARD_TUH_MAX_SPEED=OPT_MODE_HIGH_SPEED
        )
        target_compile_options(${P4_TINYUSB_LIBRARY} PRIVATE -Wno-format)
    endif()
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_tinyusb_dual VERSION 0.1.0 LANGUAGES C)
endif()
