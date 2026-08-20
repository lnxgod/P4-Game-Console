cmake_minimum_required(VERSION 3.20)

set(PROJECT_VER "0.1.0")

set(P4_TINYUSB_SOURCE "$ENV{P4_TINYUSB_SOURCE}")
if(P4_TINYUSB_SOURCE STREQUAL "")
    set(P4_TINYUSB_SOURCE
        "${CMAKE_CURRENT_LIST_DIR}/../console_os/managed_components/espressif__tinyusb")
endif()
get_filename_component(P4_TINYUSB_SOURCE "${P4_TINYUSB_SOURCE}" ABSOLUTE)
if(NOT EXISTS "${P4_TINYUSB_SOURCE}/src/host/usbh.c" OR
   NOT EXISTS "${P4_TINYUSB_SOURCE}/src/portable/synopsys/dwc2/hcd_dwc2.c" OR
   NOT EXISTS "${P4_TINYUSB_SOURCE}/.component_hash")
    message(FATAL_ERROR
        "Pinned TinyUSB source is unavailable at ${P4_TINYUSB_SOURCE}. "
        "Resolve Console OS managed dependencies first or set P4_TINYUSB_SOURCE.")
endif()
file(READ "${P4_TINYUSB_SOURCE}/.component_hash" P4_TINYUSB_COMPONENT_HASH)
string(STRIP "${P4_TINYUSB_COMPONENT_HASH}" P4_TINYUSB_COMPONENT_HASH)
if(NOT P4_TINYUSB_COMPONENT_HASH STREQUAL
       "a72b7d67472914ab76309340fd50d578b31e310963d45ad0f81144bde3314752")
    message(FATAL_ERROR
        "Refusing unpinned TinyUSB package hash: ${P4_TINYUSB_COMPONENT_HASH}")
endif()
set(P4_TINYUSB_SOURCE "${P4_TINYUSB_SOURCE}" CACHE INTERNAL
    "Exact TinyUSB component source used by the USB-only diagnostic")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(p4_usb_host_diag)
