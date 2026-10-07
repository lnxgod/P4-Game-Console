#ifndef PLATFORM_DISPLAY_H
#define PLATFORM_DISPLAY_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t platform_display_submit_game_content_rgb565(const uint16_t *pixels, size_t stride, uint32_t timeout);
#endif
