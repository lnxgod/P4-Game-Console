#ifndef DOOM_VIDEO_H
#define DOOM_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DOOM_VIDEO_WIDTH = 320,
    DOOM_VIDEO_HEIGHT = 200,
};

/** Allocate the project-owned RGB565 conversion surface in PSRAM. */
esp_err_t doom_video_init(void);

/** Submit the adapter's zero-filled first frame before enabling backlight. */
esp_err_t doom_video_submit_black(uint32_t timeout_ms);

/**
 * Convert and submit one pinned-doomgeneric default-format frame.
 *
 * The source contains 320x200 uint32_t numeric pixels in 0x00RRGGBB layout.
 * That layout comes from the pinned engine's default/`rgba8888` mode. The
 * adapter deliberately converts it to standard RGB565 before calling
 * platform_display, giving D1 a host-testable engine/platform isolation seam.
 * The pinned engine's separate `rgb565` conversion is also standard RGB565;
 * this adapter choice is not a workaround for an upstream color bug.
 */
esp_err_t doom_video_submit_xrgb8888(const uint32_t *source,
                                     size_t source_stride_pixels,
                                     uint32_t timeout_ms);

/** Release the adapter-owned conversion surface. */
esp_err_t doom_video_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
