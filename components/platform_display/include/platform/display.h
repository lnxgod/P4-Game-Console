#ifndef PLATFORM_DISPLAY_H
#define PLATFORM_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLATFORM_DISPLAY_WIDTH = 1024,
    PLATFORM_DISPLAY_HEIGHT = 600,
    PLATFORM_DISPLAY_GAME_WIDTH = 320,
    PLATFORM_DISPLAY_GAME_HEIGHT = 200,
    PLATFORM_DISPLAY_GAME_SCALE = 3,
    PLATFORM_DISPLAY_GAME_VIEWPORT_WIDTH = 960,
    PLATFORM_DISPLAY_GAME_MARGIN_LEFT = 32,
    PLATFORM_DISPLAY_GAME_MARGIN_RIGHT = 32,
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
    /* ESP-IDF 5.5.3 logs DSI underruns internally but exposes no counter. */
    bool underrun_count_available;
} platform_display_stats_t;

/**
 * Initialize the display-only hardware path and keep the backlight dark.
 *
 * This service owns the panel power domains, MIPI-DSI host, EK79007 driver,
 * frame buffer, and backlight PWM. Games must not access those resources.
 */
esp_err_t platform_display_init(void);

/** Stop scanout, force the backlight dark, and release display resources. */
esp_err_t platform_display_deinit(void);

/** Select an ESP32-P4 DSI self-test pattern, or the black frame buffer. */
esp_err_t platform_display_show_pattern(platform_display_pattern_t pattern);

/**
 * Copy and present one standard RGB565 320x200 game surface.
 *
 * Each input word uses R[15:11], G[10:5], B[4:0]. The service performs a
 * nearest-neighbor 3x expansion into a centered 960x600 viewport with black
 * 32-pixel side margins. `source_stride_pixels` must be at least 320. The
 * caller retains ownership and may reuse the source only after this call
 * returns.
 *
 * Initialization, pattern changes, brightness, and submits are serialized.
 * `timeout_ms` bounds lock acquisition and refresh completion; zero performs
 * a non-blocking attempt. A copied frame can still become visible after a
 * timeout, so callers must treat a timeout as an unknown presentation state.
 */
esp_err_t platform_display_submit_rgb565(const uint16_t *source,
                                         size_t source_stride_pixels,
                                         uint32_t timeout_ms);

/** Copy the service's monotonic diagnostic counters. */
esp_err_t platform_display_get_stats(platform_display_stats_t *out_stats);

/** Set backlight duty in the range 0..100 percent. */
esp_err_t platform_display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif
