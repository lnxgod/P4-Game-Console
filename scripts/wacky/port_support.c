/* SPDX-License-Identifier: MIT */
#include "port_support.h"
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
/* Probe allocations have a hard total limit and retain proper alignment. */
typedef union { max_align_t align; size_t bytes; } Header;
static size_t live_bytes, peak_bytes;
#ifdef WW_P4_PROBE_TEST
#define WW_HEAP_LIMIT (4U * 512U * 1024U) /* Four simultaneous test instances; device remains 512 KiB. */
#else
#define WW_HEAP_LIMIT (512U * 1024U)
#endif
void *ww_p4_calloc(size_t count, size_t bytes)
{
    if (bytes && count > SIZE_MAX / bytes) return NULL;
    size_t n = count * bytes;
    if (n > WW_HEAP_LIMIT || live_bytes > WW_HEAP_LIMIT - n) return NULL;
    Header *p = calloc(1, sizeof(*p) + n);
    if (!p) return NULL;
    p->bytes = n; live_bytes += n;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return p + 1;
}
void *ww_p4_malloc(size_t bytes) { return ww_p4_calloc(1, bytes); }
void ww_p4_free(void *ptr)
{
    if (!ptr) return;
    Header *p = (Header *)ptr - 1;
    live_bytes -= p->bytes; free(p);
}
size_t ww_p4_heap_live(void) { return live_bytes; }
size_t ww_p4_heap_peak(void) { return peak_bytes; }
