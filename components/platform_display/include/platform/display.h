// SPDX-License-Identifier: MIT

#ifndef PLATFORM_DISPLAY_H
#define PLATFORM_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform/board.h"
#include "platform/display_region.h"
#include "platform/display_scroll.h"

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
    /* Tab5 game transform subphases; latest values reset at transform start
     * and remain zero for UI transforms. Prescale is used only by the 320x200
     * game input path. PPA includes its driver's blocking cache/DMA work.
     * Both are inside transform timing; maxima retain historical samples. */
    uint32_t pipeline_prescale_last_us;
    uint32_t pipeline_prescale_max_us;
    uint32_t pipeline_ppa_last_us;
    uint32_t pipeline_ppa_max_us;
    uint32_t pipeline_handoff_last_us;
    uint32_t pipeline_handoff_max_us;
    uint32_t pipeline_reserved_refreshes;
    /* Intervals between actual DSI refresh callbacks, rather than submitted
     * frames. Zero until the first measured refresh interval. */
    uint32_t pipeline_refresh_interval_last_us;
    uint32_t pipeline_refresh_interval_min_us;
    uint32_t pipeline_refresh_interval_max_us;
    uint32_t pipeline_refresh_events;
    /* Prepared logical-cache DMA2D copy phases. Destination cache preparation
     * includes boundary C2M and ROI invalidation; enqueue includes private
     * descriptor setup; wait joins successful completion. Zero for a prepared
     * call that takes public PPA fallback before the corresponding phase. */
    uint32_t prepared_copy_prepare_last_us;
    uint32_t prepared_copy_prepare_max_us;
    uint32_t prepared_copy_enqueue_last_us;
    uint32_t prepared_copy_enqueue_max_us;
    uint32_t prepared_copy_wait_last_us;
    uint32_t prepared_copy_wait_max_us;
    /* Physical scroll warm-up copies stationary pixels from the selected
     * immutable frame when the retired slot has a different context. Counts
     * only successfully published repaired frames; time joins all complement
     * DMA rectangles, excluding the subsequent interior copy and strip PPA. */
    uint32_t ui_scroll_repaired_frames;
    uint32_t ui_scroll_repair_last_us;
    uint32_t ui_scroll_repair_max_us;
    /* Successful presentations that skipped stationary repair because the
     * retired DMA-clean slot exactly matched previous_context and an explicit
     * patch covered all stationary changes. Already-new-context slots do not
     * contribute to this boot-cumulative saturating counter. */
    uint32_t ui_scroll_previous_context_reused_frames;
    /* Ordinary authoritative REGION updates may clone the selected physical
     * frame before rotating only current damage when retired history would
     * require a large reconstruction. Count successful clone+region publishes;
     * clone timing joins the full physical DMA, excluding subsequent PPA. */
    uint32_t ui_region_cloned_frames;
    uint32_t ui_region_clone_last_us;
    uint32_t ui_region_clone_max_us;
    bool underrun_count_available;
    /* Waveshare native-content dirty-region presentation telemetry. */
    uint32_t partial_content_submits;
    uint32_t partial_content_source_pixels;
    uint32_t partial_content_full_fallbacks;
    /* Input-correlated native shell handoffs. Timings are in microseconds;
     * count/total fields saturate at UINT32_MAX. Tab5 attributes each sample
     * conservatively at two refresh callbacks after publication: the pinned
     * callback carries no framebuffer identity and cannot prove its exact
     * visible scanout time. Other unsupported adapters leave these zero. */
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

#if CONFIG_P4_BOARD_M5STACK_TAB5
/** Present a 320x200 game surface already expanded to 384x240 by the exact
 * Tab5 6:5 nearest-neighbor prescale. PPA consumes the source before return;
 * CPU fallback preserves the original 320x200 sampling, and scanout retains
 * the same three buffers and two-refresh retirement fence. */
esp_err_t platform_display_submit_prescaled_game_rgb565(
    const uint16_t *source, size_t source_stride_pixels, uint32_t timeout_ms);
/** Present the native 1280x720 Tab5 UI without scaling. */
esp_err_t platform_display_submit_ui_rgb565(const uint16_t *source,
    size_t source_stride_pixels, uint32_t timeout_ms);
/** Complete authoritative UI source plus all damage since its last submission.
 * Replays small retired-buffer damage. With missing/large history and proven
 * DMA-clean physical slots, clones the immutable selected frame and rotates
 * only current damage, publishing the entire owned buffer afterwards. Every
 * copy/rotation joins before return or full authoritative-source fallback;
 * retains the scanout reuse fence. */
esp_err_t platform_display_submit_ui_region_rgb565(const uint16_t *source,
    size_t source_stride_pixels, const platform_display_rgb565_region_t *region,
    uint32_t timeout_ms);
/** Copy the published native 720x1280 RGB565 scanout after its refresh fence.
 * Caller owns a buffer of at least native_width*native_height*2 bytes. The
 * display mutex is held only for the bounded fence and copy, never transport.
 * This captures submitted device pixels, not an optical view of the panel.
 * Its CPU read revokes this slot's DMA-only cache proof; later reuse may need
 * an authoritative reconstruction before physical scroll composition. */
esp_err_t platform_display_copy_scanout_rgb565(uint16_t *destination,
    size_t destination_bytes, uint32_t timeout_ms);
/** Ordinary authoritative UI presentation with optional scroll state. A NULL
 * damage presents the whole source; NULL state invalidates scroll eligibility.
 * State tags the exact source/stride and stationary context of the resulting
 * owned framebuffer, without changing the three-buffer retirement fence. */
esp_err_t platform_display_submit_ui_tagged_rgb565(const uint16_t *source,
    size_t source_stride_pixels,const platform_display_rgb565_region_t *damage,
    const platform_display_ui_scroll_t *state,uint32_t timeout_ms);
/** Compose a pure scroll from the immutable selected physical framebuffer
 * into a distinct retired framebuffer. Source contains the newly exposed
 * logical strip, complete scrollbar and any explicit stationary_damage patch;
 * other logical pixels may be stale. The selected slot must match
 * previous_context/source/stride/geometry and its offset
 * must equal previous_offset. A retired DMA-clean slot with a stale context
 * is repaired from the selected frame's stationary complement before shifting
 * content. An exact previous-context target also skips repair when a valid
 * explicit patch will replace every stationary difference; the patch is still
 * written. Repair requires a contiguous right-adjacent scrollbar sharing the
 * viewport's y/height. ESP_ERR_NOT_SUPPORTED rejects
 * eligibility before any pixel mutation, preserving unchanged slot tags for
 * warm-up. The caller may then reconstruct and submit an authoritative source.
 * Other errors never authorize fallback with an incomplete logical source.
 * A changed context requires a nonempty bounded patch disjoint from viewport
 * and scrollbar, containing every stationary difference between the old/new
 * contexts. No patch requires previous_context==context. The patch always
 * rotates after moving content; only complete publication tags the new context.
 * Joined DMA/rotation retains all surfaces through completion. A completion
 * timeout warns once then waits while retaining ownership; it cannot release
 * an accepted transaction to CPU reuse. Only nonencrypted driver-owned RGB565
 * buffers are used by the physical DMA2D path. */
esp_err_t platform_display_submit_ui_scroll_rgb565(const uint16_t *source,
    size_t source_stride_pixels,const platform_display_ui_scroll_t *state,
    uint32_t timeout_ms);
/** Synchronous non-overlapping logical RGB565 rectangle copy using PPA.
 * Source/destination backing ranges must be disjoint. The destination base
 * and complete stride*height*2 allocation must be 64-byte aligned. Dimensions
 * are bounded to 4095. Keep source immutable and destination exclusively owned
 * until return; CPU drawing may resume after the joined DMA/cache fence.
 * Neighbouring CPU-dirty pixels in touched rows are preserved. This is not a
 * scanout publication API and cannot write driver-owned panel buffers.
 * timeout_ms bounds mutex acquisition; pinned blocking PPA joins without an
 * abort timeout. The driver conservatively flushes the source on every copy. */
esp_err_t platform_display_copy_rgb565_rectangle(const uint16_t *source,
    size_t source_stride,size_t source_height,
    const platform_display_rgb565_region_t *source_region,
    uint16_t *destination,size_t destination_stride,size_t destination_height,
    uint16_t destination_x,uint16_t destination_y,uint32_t timeout_ms);
/** Publish CPU-prepared source rows to memory during idle preparation.
 * The source base and stride*2 must be 64-byte aligned; dimensions are bounded
 * to 4095 and the nonempty row range must fit the backing allocation. Keep the
 * source exclusively owned until this synchronous C2M publication returns.
 * The caller tracks which preparation epoch/rows were published and revokes
 * cleanliness before any subsequent CPU write. Operations use <=32 KiB chunks.
 * timeout_ms bounds mutex acquisition, not an abort of cache maintenance. */
esp_err_t platform_display_prepare_rgb565_rows(const uint16_t *source,
    size_t source_stride,size_t source_height,size_t first_row,size_t row_count,
    uint32_t timeout_ms);
/** Joined rectangle copy from a previously published immutable source.
 * Same backing-range/exclusive-destination rules as the ordinary copy above.
 * All source rows read by this operation must have completed publication in
 * their current preparation epoch. This promise is caller-owned, not inferred
 * from a const pointer or content signature. CPU access may resume on return.
 * Supported DMA2D geometries invalidate only the aligned copied span in each
 * destination row, preserving dirty partial boundary cache lines first. No CPU
 * access to either surface is permitted while DMA owns it. Unsupported DMA
 * geometry, unavailable helper, or an enqueue rejection safely uses public PPA.
 * timeout_ms bounds mutex acquisition and the first completion wait. After an
 * accepted DMA transaction times out, this function warns once and retains both
 * surfaces/mutex while waiting for completion: it never returns an unfenced
 * timeout to a CPU fallback or permits freeing/reusing an in-flight surface. */
esp_err_t platform_display_copy_prepared_rgb565_rectangle(const uint16_t *source,
    size_t source_stride,size_t source_height,
    const platform_display_rgb565_region_t *source_region,
    uint16_t *destination,size_t destination_stride,size_t destination_height,
    uint16_t destination_x,uint16_t destination_y,uint32_t timeout_ms);
#endif

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
 * before returning; shell content may retain its pipelined handoff. Tab5
 * consumes the source synchronously with PPA, then pipelines scanout through
 * three owned buffers; a replaced buffer retains a two-refresh reuse fence.
 */
esp_err_t platform_display_submit_game_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms);

/**
 * Associate an optional touch/input sample with subsequent native shell
 * submissions. `timestamp_us` is the monotonic microsecond timestamp from
 * the input backend; pass zero to clear it. This changes no present timing or
 * ownership and is ignored by adapters without the DSI shell pipeline. Tab5
 * records a conservative two-callback publication bound, not a per-buffer
 * visible-frame acknowledgment.
 */
esp_err_t platform_display_record_interactive_input_timestamp(
    int64_t timestamp_us);

esp_err_t platform_display_get_stats(platform_display_stats_t *out_stats);
#if CONFIG_P4_BOARD_M5STACK_TAB5
/** Zero-wait diagnostic snapshot; ESP_ERR_TIMEOUT leaves output unchanged. */
esp_err_t platform_display_try_get_stats(platform_display_stats_t *out_stats);
#endif
esp_err_t platform_display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif
