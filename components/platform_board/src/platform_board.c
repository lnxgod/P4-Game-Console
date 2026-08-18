// SPDX-License-Identifier: MIT

#include "platform/board.h"

#include "sdkconfig.h"

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
static const platform_board_descriptor_t s_board = {
    .id = PLATFORM_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3,
    .slug = "waveshare-esp32-p4-wifi6-touch-lcd-4.3",
    .vendor = "Waveshare",
    .product = "ESP32-P4-WIFI6-Touch-LCD-4.3",
    .revision = "exact-unit profile; PCB revision unrecorded",
    .display_width = 800,
    .display_height = 480,
    .flash_bytes = UINT32_C(33554432),
    .psram_bytes = UINT32_C(33554432),
    .has_touch = true,
    .has_hdmi = false,
    .has_sd_card = true,
    .has_usb_device_game_storage = true,
    .has_integrated_usb_host_hub = false,
    .has_speaker = true,
    .has_headphone_codec = true,
};
#elif CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static const platform_board_descriptor_t s_board = {
    .id = PLATFORM_BOARD_OLIMEX_ESP32_P4_PC_REV_B,
    .slug = "olimex-esp32-p4-pc-rev-b",
    .vendor = "Olimex",
    .product = "ESP32-P4-PC",
    .revision = "B",
    .display_width = 1280,
    .display_height = 720,
    .flash_bytes = UINT32_C(16777216),
    .psram_bytes = UINT32_C(33554432),
    .has_touch = false,
    .has_hdmi = true,
    .has_sd_card = true,
    .has_usb_device_game_storage = false,
    .has_integrated_usb_host_hub = true,
    .has_speaker = false,
    .has_headphone_codec = true,
};
#else
static const platform_board_descriptor_t s_board = {
    .id = PLATFORM_BOARD_ELECROW_CROWPANEL_ADVANCED_10,
    .slug = "elecrow-crowpanel-advanced-10",
    .vendor = "Elecrow",
    .product = "CrowPanel Advanced 10.1-inch",
    .revision = "cross-revision display/storage profile",
    .display_width = 1024,
    .display_height = 600,
    .flash_bytes = UINT32_C(16777216),
    .psram_bytes = UINT32_C(33554432),
    .has_touch = true,
    .has_hdmi = false,
    .has_sd_card = false,
    .has_usb_device_game_storage = true,
    .has_integrated_usb_host_hub = false,
    .has_speaker = true,
    .has_headphone_codec = false,
};
#endif

const platform_board_descriptor_t *platform_board_get(void)
{
    return &s_board;
}

bool platform_board_is(platform_board_id_t board)
{
    return s_board.id == board;
}

platform_board_kind_t platform_board_kind(void)
{
    return s_board.id;
}

const char *platform_board_name(void)
{
    return s_board.slug;
}
