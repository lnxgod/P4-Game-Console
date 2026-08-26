// SPDX-License-Identifier: MIT

#ifndef PLATFORM_DISPLAY_H
#define PLATFORM_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform/board.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLATFORM_DISPLAY_WIDTH = PLATFORM_BOARD_DISPLAY_WIDTH,
    PLATFORM_DISPLAY_HEIGHT = PLATFORM_BOARD_DISPLAY_HEIGHT,
    PLATFORM_DISPLAY_NATIVE_WIDTH = PLATFORM_BOARD_DISPLAY_NATIVE_WIDTH,
    PLATFORM_DISPLAY_NATIVE_HEIGHT = PLATFORM_BOARD_DISPLAY_NATIVE_HEIGHT,
    PLATFORM_DISPLAY_ROTATION_CW_DEGREES =
        PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES,
    PLATFORM_DISPLAY_GAME_WIDTH = PLATFORM_BOARD_GAME_SURFACE_WIDTH,
    PLATFORM_DISPLAY_GAME_HEIGHT = PLATFORM_BOARD_GAME_SURFACE_HEIGHT,
    PLATFORM_DISPLAY_GAME_VIEWPORT_WIDTH =
        PLATFORM_BOARD_GAME_VIEWPORT_WIDTH,
    PLATFORM_DISPLAY_GAME_VIEWPORT_HEIGHT =
        PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT,
    PLATFORM_DISPLAY_GAME_MARGIN_LEFT = PLATFORM_BOARD_GAME_MARGIN_LEFT,
    PLATFORM_DISPLAY_GAME_MARGIN_RIGHT = PLATFORM_BOARD_GAME_MARGIN_RIGHT,
    PLATFORM_DISPLAY_GAME_MARGIN_TOP = PLATFORM_BOARD_GAME_MARGIN_TOP,
    PLATFORM_DISPLAY_GAME_MARGIN_BOTTOM = PLATFORM_BOARD_GAME_MARGIN_BOTTOM,
    PLATFORM_DISPLAY_CONTENT_WIDTH = 768,
    PLATFORM_DISPLAY_CONTENT_HEIGHT = 480,
};

typedef enum {
    PLATFORM_DISPLAY_PATTERN_COLOR_BARS_VERTICAL = 0,
    PLATFORM_DISPLAY_PATTERN_COLOR_BARS_HORIZONTAL,
    PLATFORM_DISPLAY_PATTERN_BER_VERTICAL,
    PLATFORM_DISPLAY_PATTERN_BLACK,
} platform_display_pattern_t;

typedef struct {
    uint32_t submits_started;
    uint32_t submits_completed;
    uint32_t submit_timeouts;
    uint32_t submit_failures;
    uint32_t refresh_completions;
    uint32_t accelerated_submits;
    uint32_t accelerator_failures;
    bool underrun_count_available;
} platform_display_stats_t;

esp_err_t platform_display_init(void);
esp_err_t platform_display_deinit(void);
esp_err_t platform_display_show_pattern(platform_display_pattern_t pattern);

/** Present the stable 320x200 RGB565 shell/game surface. */
esp_err_t platform_display_submit_rgb565(const uint16_t *source,
                                         size_t source_stride_pixels,
                                         uint32_t timeout_ms);

/** Present reviewed 768x480 legacy content where the board adapter supports it. */
esp_err_t platform_display_submit_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms);

esp_err_t platform_display_get_stats(platform_display_stats_t *out_stats);
esp_err_t platform_display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif
