// SPDX-License-Identifier: MIT
#ifndef TAB5_RASTER_COPY_H
#define TAB5_RASTER_COPY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "platform/display_region.h"

enum { TAB5_RASTER_CACHE_LINE = 64, TAB5_RASTER_MAX_DIMENSION = 4095 };
typedef struct {
    size_t source_bytes, destination_bytes;
    size_t destination_cache_offset, destination_cache_bytes;
} tab5_raster_copy_plan_t;

typedef struct {
    size_t source_bytes, cache_offset, cache_bytes;
} tab5_raster_publication_plan_t;

/* A published prefix is a caller-owned cleanliness promise, independent of
 * content signatures. Any later CPU write invalidates that promise. */
static inline bool tab5_raster_publication_plan(uintptr_t source,size_t stride,
    size_t height,size_t first_row,size_t row_count,tab5_raster_publication_plan_t *plan)
{
    if(!source||!plan||!stride||!height||!row_count||
       stride>TAB5_RASTER_MAX_DIMENSION||height>TAB5_RASTER_MAX_DIMENSION||
       first_row>=height||row_count>height-first_row||
       source%TAB5_RASTER_CACHE_LINE||(stride*sizeof(uint16_t))%TAB5_RASTER_CACHE_LINE)
        return false;
    const size_t bytes=stride*height*sizeof(uint16_t);
    if(source>UINTPTR_MAX-bytes)return false;
    *plan=(tab5_raster_publication_plan_t){bytes,first_row*stride*sizeof(uint16_t),
        row_count*stride*sizeof(uint16_t)};
    return true;
}

typedef struct {
    size_t row_stride_bytes, cache_offset, cache_bytes;
    size_t boundary_offset[2];
    unsigned row_count, boundary_count;
} tab5_raster_dma_plan_t;

/* The PPA invalidates whole output rows, expanded to cache-line boundaries.
 * Preserve CPU-dirty neighbouring pixels before PPA invalidates that same
 * bounded window. Exclusive ownership prevents CPU reloads until DMA joins. */
static inline bool tab5_raster_copy_plan(uintptr_t source,size_t source_stride,
    size_t source_height,const platform_display_rgb565_region_t *region,
    uintptr_t destination,size_t destination_stride,size_t destination_height,
    unsigned destination_x,unsigned destination_y,tab5_raster_copy_plan_t *plan)
{
    if(!source||!destination||!region||!plan||!region->width||!region->height||
       !source_stride||!source_height||!destination_stride||!destination_height||
       source_stride>TAB5_RASTER_MAX_DIMENSION||source_height>TAB5_RASTER_MAX_DIMENSION||
       destination_stride>TAB5_RASTER_MAX_DIMENSION||destination_height>TAB5_RASTER_MAX_DIMENSION||
       region->x>=source_stride||region->y>=source_height||
       region->width>source_stride-region->x||region->height>source_height-region->y||
       destination_x>=destination_stride||destination_y>=destination_height||
       region->width>destination_stride-destination_x||region->height>destination_height-destination_y)
        return false;
    const size_t source_bytes=source_stride*source_height*sizeof(uint16_t);
    const size_t destination_bytes=destination_stride*destination_height*sizeof(uint16_t);
    if(source>UINTPTR_MAX-source_bytes||destination>UINTPTR_MAX-destination_bytes||
       (source<destination+destination_bytes&&destination<source+source_bytes)||
       destination%TAB5_RASTER_CACHE_LINE||destination_bytes%TAB5_RASTER_CACHE_LINE)
        return false;
    const size_t row_bytes=destination_stride*sizeof(uint16_t);
    const size_t start=(size_t)destination_y*row_bytes;
    const size_t end=((size_t)destination_y+region->height)*row_bytes;
    const size_t aligned_start=start&~(size_t)(TAB5_RASTER_CACHE_LINE-1U);
    const size_t aligned_end=(end+TAB5_RASTER_CACHE_LINE-1U)&~(size_t)(TAB5_RASTER_CACHE_LINE-1U);
    if(aligned_end>destination_bytes)return false;
    *plan=(tab5_raster_copy_plan_t){source_bytes,destination_bytes,
        aligned_start,aligned_end-aligned_start};
    return true;
}

/* DMA2D receives require four-byte-aligned RGB565 addresses and lengths.
 * Cache-line-aligned row strides keep each row's preservation independent:
 * only partial boundary lines need C2M; complete copied lines are discarded
 * before DMA. The unprepared PPA path handles other valid copy geometries. */
static inline bool tab5_raster_dma_plan(uintptr_t source,size_t source_stride,
    size_t source_height,const platform_display_rgb565_region_t *region,
    uintptr_t destination,size_t destination_stride,size_t destination_height,
    unsigned destination_x,unsigned destination_y,tab5_raster_dma_plan_t *plan)
{
    tab5_raster_copy_plan_t copy;
    if(!plan||!tab5_raster_copy_plan(source,source_stride,source_height,region,
        destination,destination_stride,destination_height,destination_x,destination_y,&copy)||
       source%TAB5_RASTER_CACHE_LINE||
       (source_stride*sizeof(uint16_t))%TAB5_RASTER_CACHE_LINE||
       (destination_stride*sizeof(uint16_t))%TAB5_RASTER_CACHE_LINE||
       ((region->x|region->width|destination_x)&1U))return false;
    const size_t row_bytes=destination_stride*sizeof(uint16_t);
    const size_t start=(size_t)destination_y*row_bytes+(size_t)destination_x*sizeof(uint16_t);
    const size_t end=start+(size_t)region->width*sizeof(uint16_t);
    const size_t aligned_start=start&~(size_t)(TAB5_RASTER_CACHE_LINE-1U);
    const size_t aligned_end=(end+TAB5_RASTER_CACHE_LINE-1U)&~(size_t)(TAB5_RASTER_CACHE_LINE-1U);
    *plan=(tab5_raster_dma_plan_t){.row_stride_bytes=row_bytes,
        .cache_offset=aligned_start,.cache_bytes=aligned_end-aligned_start,
        .row_count=region->height};
    if(start!=aligned_start)plan->boundary_offset[plan->boundary_count++]=aligned_start;
    const size_t last_line=aligned_end-TAB5_RASTER_CACHE_LINE;
    if(end!=aligned_end&&(!plan->boundary_count||plan->boundary_offset[0]!=last_line))
        plan->boundary_offset[plan->boundary_count++]=last_line;
    return true;
}
#endif
