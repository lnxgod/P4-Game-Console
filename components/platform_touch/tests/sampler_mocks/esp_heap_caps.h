#ifndef SAMPLER_TEST_HEAP_H
#define SAMPLER_TEST_HEAP_H
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
void *heap_caps_calloc(size_t count, size_t size, uint32_t caps);
void heap_caps_free(void *pointer);
#endif
