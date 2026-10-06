#ifndef WW_P4_SUPPORT_H
#define WW_P4_SUPPORT_H
#include <stddef.h>
void *ww_p4_malloc(size_t bytes);
void *ww_p4_calloc(size_t count, size_t bytes);
void ww_p4_free(void *p);
size_t ww_p4_heap_live(void);
size_t ww_p4_heap_peak(void);
#endif
