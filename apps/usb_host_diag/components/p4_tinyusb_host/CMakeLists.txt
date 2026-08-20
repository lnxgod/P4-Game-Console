if(NOT DEFINED P4_TINYUSB_SOURCE OR P4_TINYUSB_SOURCE STREQUAL "")
    set(P4_TINYUSB_SOURCE "$ENV{P4_TINYUSB_SOURCE}")
endif()
get_filename_component(P4_TINYUSB_SOURCE "${P4_TINYUSB_SOURCE}" ABSOLUTE)
set(P4_TINYUSB_SRC "${P4_TINYUSB_SOURCE}/src")

function(p4_require_tinyusb_source P4_RELATIVE_PATH P4_EXPECTED_SHA256)
    file(SHA256 "${P4_TINYUSB_SOURCE}/${P4_RELATIVE_PATH}"
         P4_ACTUAL_SHA256)
    if(NOT P4_ACTUAL_SHA256 STREQUAL "${P4_EXPECTED_SHA256}")
        message(FATAL_ERROR
            "Pinned TinyUSB source changed: ${P4_RELATIVE_PATH} "
            "(${P4_ACTUAL_SHA256})")
    endif()
endfunction()

p4_require_tinyusb_source("src/tusb.c"
    "b5f0e8ca00ba6e63914583f7b4711c540a125c261b261216f89e5136fda2ac6f")
p4_require_tinyusb_source("src/common/tusb_fifo.c"
    "d87e25d3329244226f3b794882e89898ac5fe20e04568a7750f4aff154f871ac")
p4_require_tinyusb_source("src/host/usbh.c"
    "2b537fc20eb9f6757d53c59896290a4942daa1e4ce3ed892220a13596beb6ab8")
p4_require_tinyusb_source("src/host/hub.c"
    "36b4fdf26f819a7882e0d9e76c5189ae3819ca5cb600ad902e27b5aa4a2f74cd")
p4_require_tinyusb_source("src/class/hid/hid_host.c"
    "83856a19edbec34129fc1a7477c371fdced2108e6f9ebf77247974455bb9f2d6")
p4_require_tinyusb_source("src/portable/synopsys/dwc2/hcd_dwc2.c"
    "3f8291ddcb47f85527f473126aeb84e6f6fcce7ae2f8cfce15fb686d8f5aaad9")
p4_require_tinyusb_source("src/portable/synopsys/dwc2/dwc2_common.c"
    "3971bcb1b0fd639dad056dab97a2671aa430a5892347938427a11d864679559b")

idf_component_register(
    SRCS
        "${P4_TINYUSB_SRC}/tusb.c"
        "${P4_TINYUSB_SRC}/common/tusb_fifo.c"
        "${P4_TINYUSB_SRC}/host/usbh.c"
        "${P4_TINYUSB_SRC}/host/hub.c"
        "${P4_TINYUSB_SRC}/class/hid/hid_host.c"
        "${P4_TINYUSB_SRC}/portable/synopsys/dwc2/hcd_dwc2.c"
        "${P4_TINYUSB_SRC}/portable/synopsys/dwc2/dwc2_common.c"
    INCLUDE_DIRS
        "include"
        "${P4_TINYUSB_SRC}"
    REQUIRES freertos
    PRIV_REQUIRES esp_mm
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CFG_TUSB_MCU=OPT_MCU_ESP32P4
    CFG_TUSB_OS=OPT_OS_FREERTOS
    BOARD_TUH_RHPORT=1
    BOARD_TUH_MAX_SPEED=OPT_MODE_HIGH_SPEED
    CFG_TUH_MEM_ALIGN=__attribute__\(\(aligned\(64\)\)\)
)

# Third-party code is pinned byte-for-byte; keep its upstream warning policy.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)
