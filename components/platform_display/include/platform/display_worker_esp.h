/* ESP-IDF task binding for the shared frame ownership service. */
#ifndef PLATFORM_DISPLAY_WORKER_ESP_H
#define PLATFORM_DISPLAY_WORKER_ESP_H
#include "platform/display_worker.h"
int display_worker_esp_create(
    int (*submit)(void *, const void *, size_t, uint32_t), void *context,
    size_t width, size_t height, size_t pixel_bytes,
    display_worker_t **out);
#endif
