// SPDX-License-Identifier: MIT

#ifndef PLATFORM_DISPLAY_H
#define PLATFORM_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform/board.h"
#include "platform/display_region.h"

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
    PLATFORM_DISPLAY_SHELL_WIDTH = 384,
    PLATFORM_DISPLAY_SHELL_HEIGHT = 240,
    PLATFORM_DISPLAY_CONTENT_WIDTH = 768,
    PLATFORM_DISPLAY_CONTENT_HEIGHT = 480,
};

typedef enum {
    PLATFORM_DISPLAY_PATTERN_COLOR_BARS_VERTICAL = 0,
    PLATFORM_DISPLAY_PATTERN_COLOR_BARS_HORIZONTAL,
    PLATFORM_DISPLAY_PATTERN_BER_VERTICAL,
    PLATFORM_DISPLAY_PATTERN_BLACK,
} platform_display_pattern_t;

/* Classification of the last input-correlated native shell handoff. */
typedef enum {
    PLATFORM_DISPLAY_INTERACTIVE_PRESENT_NONE = 0,
    PLATFORM_DISPLAY_INTERACTIVE_PRESENT_FULL,
    PLATFORM_DISPLAY_INTERACTIVE_PRESENT_PARTIAL,
} platform_display_interactive_present_kind_t;

typedef struct {
    uint32_t submits_started;
    uint32_t submits_completed;
    uint32_t submit_timeouts;
    uint32_t submit_failures;
    uint32_t refresh_completions;
    uint32_t accelerated_submits;
    uint32_t accelerator_failures;
    /* Latest and peak platform-only submission phases. These exclude source
     * rendering in the caller. */
    uint32_t pipeline_reuse_wait_last_us;
    uint32_t pipeline_reuse_wait_max_us;
    uint32_t pipeline_transform_last_us;
    uint32_t pipeline_transform_max_us;
    uint32_t pipeline_handoff_last_us;
    uint32_t pipeline_handoff_max_us;
    uint32_t pipeline_reserved_refreshes;
    /* Intervals between actual DSI refresh callbacks, rather than submitted
     * frames. Zero until the first measured refresh interval. */
    uint32_t pipeline_refresh_interval_last_us;
    uint32_t pipeline_refresh_interval_min_us;
    uint32_t pipeline_refresh_interval_max_us;
    uint32_t pipeline_refresh_events;
    bool underrun_count_available;
    /* Waveshare native-content dirty-region presentation telemetry. */
    uint32_t partial_content_submits;
    uint32_t partial_content_source_pixels;
    uint32_t partial_content_full_fallbacks;
    /* Input-correlated native shell handoffs. Timings are in microseconds;
     * count/total fields saturate at UINT32_MAX. These remain zero on display
     * adapters that do not use the Waveshare DSI handoff pipeline. */
    uint32_t interactive_latency_samples;
    uint32_t interactive_partial_presentations;
    uint32_t interactive_full_presentations;
    uint32_t interactive_input_to_refresh_total_us;
    uint32_t interactive_input_to_refresh_max_us;
    uint32_t interactive_input_to_refresh_last_us;
    uint32_t interactive_handoff_to_refresh_total_us;
    uint32_t interactive_handoff_to_refresh_max_us;
    uint32_t interactive_handoff_to_refresh_last_us;
    uint32_t interactive_reuse_wait_last_us;
    uint32_t interactive_transform_last_us;
    uint8_t interactive_replay_region_count;
    platform_display_interactive_present_kind_t interactive_present_kind;
} platform_display_stats_t;

esp_err_t platform_display_init(void);
esp_err_t platform_display_deinit(void);
esp_err_t platform_display_show_pattern(platform_display_pattern_t pattern);

/** Present the stable 320x200 RGB565 shell/game surface. */
esp_err_t platform_display_submit_rgb565(const uint16_t *source,
                                         size_t source_stride_pixels,
                                         uint32_t timeout_ms);

/** Present the 384x240 shell surface in the exact 768x480 viewport. */
esp_err_t platform_display_submit_shell_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms);

/** Present reviewed 768x480 legacy content where the board adapter supports it. */
esp_err_t platform_display_submit_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms);

/**
 * Present a complete 768x480 content source after updating only the supplied
 * source regions. This is a Waveshare shell optimization: callers must keep
 * the source framebuffer authoritative and provide every changed rectangle
 * for this generation. Other adapters fall back to full native content where
 * available, or explicitly report that native content is unsupported. Game
 * content must continue to use the game-specific API.
 */
esp_err_t platform_display_submit_content_regions_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    const platform_display_rgb565_region_t *regions,
    size_t region_count,
    uint32_t timeout_ms);

/**
 * Present a negotiated 768x480 game surface. On Waveshare this preserves the
 * baseline game contract by waiting for the selected panel buffer's refresh
 * before returning; shell content may retain its pipelined handoff.
 */
esp_err_t platform_display_submit_game_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms);

/**
 * Associate an optional touch/input sample with subsequent native shell
 * submissions. `timestamp_us` is the monotonic microsecond timestamp from
 * the input backend; pass zero to clear it. This changes no present timing or
 * ownership and is ignored by adapters without the DSI shell pipeline.
 */
esp_err_t platform_display_record_interactive_input_timestamp(
    int64_t timestamp_us);

esp_err_t platform_display_get_stats(platform_display_stats_t *out_stats);
esp_err_t platform_display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif
