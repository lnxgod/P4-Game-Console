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
 * nearest-neighbor aspect-preserving expansion into the largest centered
 * viewport that fits the selected board's logical display. On the Waveshare
 * 4.3-inch target this is a 768x480 viewport with 16-pixel side bars in an
 * 800x480 landscape canvas, rotated into the panel's native 480x800 scanout.
 * `source_stride_pixels` must be at least 320. The caller retains ownership
 * and may reuse the source only after this call returns.
 *
 * Initialization, pattern changes, brightness, and submits are serialized.
 * `timeout_ms` bounds lock acquisition and refresh completion; zero performs
 * a non-blocking attempt. A copied frame can still become visible after a
 * timeout, so callers must treat a timeout as an unknown presentation state.
 */
esp_err_t platform_display_submit_rgb565(const uint16_t *source,
                                         size_t source_stride_pixels,
                                         uint32_t timeout_ms);

/**
 * Copy and present one direct 768x480 Console OS content surface.
 *
 * This is the native surface for P4 Carts and reviewed legacy-engine adapters.
 * The display service alone scales/centers it for the selected landscape
 * viewport and rotates it into native panel scanout. Callers never infer the
 * panel orientation or write DSI buffers directly.
 */
esp_err_t platform_display_submit_content_rgb565(
    const uint16_t *source,
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
