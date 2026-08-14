#include "doom/video.h"

#include <stdbool.h>

#include "doom/video_convert.h"
#include "esp_heap_caps.h"
#include "platform/display.h"

#define DOOM_VIDEO_FRAME_PIXELS \
    ((size_t)DOOM_VIDEO_WIDTH * (size_t)DOOM_VIDEO_HEIGHT)

static uint16_t *s_rgb565_frame;

esp_err_t doom_video_init(void)
{
    if (s_rgb565_frame != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_rgb565_frame = heap_caps_calloc(
        DOOM_VIDEO_FRAME_PIXELS, sizeof(*s_rgb565_frame),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_rgb565_frame != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t doom_video_submit_black(uint32_t timeout_ms)
{
    if (s_rgb565_frame == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_display_submit_rgb565(s_rgb565_frame, DOOM_VIDEO_WIDTH,
                                          timeout_ms);
}

esp_err_t doom_video_submit_xrgb8888(const uint32_t *source,
                                     size_t source_stride_pixels,
                                     uint32_t timeout_ms)
{
    if (source == NULL || source_stride_pixels < DOOM_VIDEO_WIDTH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_rgb565_frame == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!doom_video_convert_xrgb8888_to_rgb565(
            source, source_stride_pixels, s_rgb565_frame, DOOM_VIDEO_WIDTH)) {
        return ESP_ERR_INVALID_ARG;
    }
    return platform_display_submit_rgb565(s_rgb565_frame, DOOM_VIDEO_WIDTH,
                                          timeout_ms);
}

esp_err_t doom_video_deinit(void)
{
    if (s_rgb565_frame == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    heap_caps_free(s_rgb565_frame);
    s_rgb565_frame = NULL;
    return ESP_OK;
}
