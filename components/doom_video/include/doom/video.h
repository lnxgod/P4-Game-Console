#ifndef DOOM_VIDEO_H
#define DOOM_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include <stdint.h>

#include "esp_err.h"
#include "doom/video_dimensions.h"

#ifndef P4_DOOM_INDEXED_PACKET_EXPERIMENT
#define P4_DOOM_INDEXED_PACKET_EXPERIMENT 0
#endif

#ifdef __cplusplus
extern "C" {
#endif


/** Allocate the project-owned RGB565 conversion surface in PSRAM. */
esp_err_t doom_video_init(void);

/** Submit the adapter's zero-filled first frame before enabling backlight. */
esp_err_t doom_video_submit_black(uint32_t timeout_ms);

/**
 * Convert and submit one pinned-doomgeneric default-format frame.
 *
 * The source contains DOOM_VIDEO_WIDTH x DOOM_VIDEO_HEIGHT uint32_t pixels in
 * 0x00RRGGBB layout: 768x480 on native Tab5, 320x200 on legacy adapters.
 * That layout comes from the pinned engine's default/`rgba8888` mode. The
 * adapter deliberately converts it to standard RGB565 before calling
 * platform_display, giving D1 a host-testable engine/platform isolation seam.
 * The pinned engine's separate `rgb565` conversion is also standard RGB565;
 * this adapter choice is not a workaround for an upstream color bug.
 */
esp_err_t doom_video_submit_xrgb8888(const uint32_t *source,
                                     size_t source_stride_pixels,
                                     uint32_t timeout_ms);

#if CONFIG_P4_BOARD_M5STACK_TAB5
/** Acquire one writable frame from the shared worker. Fill all
 * DOOM_VIDEO_WIDTH x DOOM_VIDEO_HEIGHT pixels and publish before acquiring
 * again. Pixels become immutable on successful publish and must not be
 * accessed until acquired anew. Calls are foreground-only and serialized.
 * Deinit cancels an unpublished lease. */
esp_err_t doom_video_acquire_xrgb8888(uint32_t **pixels, uint32_t timeout_ms);
/** Publish one lease. Native Tab5 true-color compatibility submissions always
 * drain before return, irrespective of wait, to bound transient scratch to
 * one full frame. Indexed gameplay supports concurrent conversion/display.
 * The acquired pointer is invalid after every publish call, including failure.
 * A failure before
 * admission cancels the writable lease; a drain timeout after admission leaves
 * the frame worker-owned. Backend errors latch for subsequent calls. */
esp_err_t doom_video_publish_xrgb8888(bool wait, uint32_t timeout_ms);
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
/** Native Tab5 gameplay packet. Fill all DOOM_VIDEO_WIDTH x DOOM_VIDEO_HEIGHT
 * indices and all 256 gamma-corrected numeric 0x00RRGGBB palette words before
 * publish. These are producer-owned
 * payload subpointers, not worker lease tokens. Do not retain after publish.
 * Calls share the same one-lease-at-a-time rule as the XRGB API. */
esp_err_t doom_video_acquire_indexed(uint8_t **pixels, uint32_t **palette,
                                    uint32_t timeout_ms);
/** Snapshot the validated touch mask and publish. wait=true drains before
 * return; false admits one immutable frame for concurrent conversion/display.
 * Cancellation, timeout, pointer invalidation and backend-error semantics
 * otherwise match XRGB publish.
 * Wrong format or invalid mask cancels the held lease before returning. */
esp_err_t doom_video_publish_indexed(uint32_t active_actions, bool wait,
                                    uint32_t timeout_ms);
#endif
#endif

/** Release adapter buffers. Tab5 first drains/joins its worker; timeout retains
 * resources for a later deinit retry, with the display still initialized. */
esp_err_t doom_video_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
