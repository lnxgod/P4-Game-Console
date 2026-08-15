// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_BOARD_H
#define P4_PLATFORM_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLATFORM_BOARD_ELECROW_CROWPANEL_ADVANCED_10 = 0,
    PLATFORM_BOARD_OLIMEX_ESP32_P4_PC_REV_B,
} platform_board_id_t;

typedef struct {
    platform_board_id_t id;
    const char *slug;
    const char *vendor;
    const char *product;
    const char *revision;
    uint16_t display_width;
    uint16_t display_height;
    uint32_t flash_bytes;
    uint32_t psram_bytes;
    bool has_touch;
    bool has_hdmi;
    bool has_sd_card;
    bool has_usb_device_game_storage;
    bool has_integrated_usb_host_hub;
    bool has_speaker;
    bool has_headphone_codec;
} platform_board_descriptor_t;

/** Return the immutable descriptor selected at build time. */
const platform_board_descriptor_t *platform_board_get(void);

/** True only for the exact build-selected board. */
bool platform_board_is(platform_board_id_t board);

#ifdef __cplusplus
}
#endif

#endif
