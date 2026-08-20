// SPDX-License-Identifier: MIT
#ifndef P4_USB_HOST_DIAG_TUSB_CONFIG_H
#define P4_USB_HOST_DIAG_TUSB_CONFIG_H

#ifdef ESP_PLATFORM
#define CFG_TUSB_OS_INC_PATH freertos/
#endif

#define CFG_TUH_ENABLED 1
#define CFG_TUH_MAX_SPEED BOARD_TUH_MAX_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 512

/* One powered hub with room for four downstream devices and eight HID links. */
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX 4
#define CFG_TUH_HID 8
#define CFG_TUH_HID_EP_BUFSIZE 128

#define CFG_TUH_CDC 0
#define CFG_TUH_MSC 0
#define CFG_TUH_MIDI 0
#define CFG_TUH_MIDI2 0
#define CFG_TUH_VENDOR 0

/* Slave/FIFO mode gives the first diagnostic the smallest DMA/cache surface. */
#define CFG_TUH_DWC2_DMA_ENABLE 0
#define CFG_TUH_DWC2_SLAVE_ENABLE 1
#define CFG_TUH_DWC2_ENDPOINT_MAX 16

#define CFG_TUSB_DEBUG 2
#define CFG_TUH_MEM_ALIGN __attribute__((aligned(64)))

#endif
