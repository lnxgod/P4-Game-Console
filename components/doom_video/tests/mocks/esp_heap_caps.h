#ifndef ESP_HEAP_CAPS_H
#define ESP_HEAP_CAPS_H
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1U
#define MALLOC_CAP_8BIT 2U
#define MALLOC_CAP_INTERNAL 4U
void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps);
void heap_caps_free(void *pointer);
#endif
