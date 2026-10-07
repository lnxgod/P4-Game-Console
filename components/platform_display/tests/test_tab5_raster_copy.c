// SPDX-License-Identifier: MIT
#include "tab5_raster_copy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uintptr_t source_base=UINT32_C(0x10000000);
static const uintptr_t destination_base=UINT32_C(0x20000000);

static void publication_bounds(void)
{
    tab5_raster_publication_plan_t plan;
    assert(tab5_raster_publication_plan(source_base,1024,1280,31,16,&plan));
    assert(plan.source_bytes==1024U*1280U*2U);
    assert(plan.cache_offset==31U*2048U&&plan.cache_bytes==32768U);
    assert(tab5_raster_publication_plan(source_base,1024,1280,1279,1,&plan));
    assert(plan.cache_offset+plan.cache_bytes==plan.source_bytes);
    assert(!tab5_raster_publication_plan(source_base+2U,1024,1280,0,16,&plan));
    assert(!tab5_raster_publication_plan(source_base,1023,1280,0,16,&plan));
    assert(!tab5_raster_publication_plan(source_base,1024,1280,1280,1,&plan));
    assert(!tab5_raster_publication_plan(source_base,1024,1280,1279,2,&plan));
    assert(!tab5_raster_publication_plan(source_base,1024,1280,0,0,&plan));
    assert(!tab5_raster_publication_plan(source_base,4096,1280,0,16,&plan));
    assert(!tab5_raster_publication_plan(UINTPTR_MAX-63U,32,1,0,1,&plan));
}

static void dma_bounds(void)
{
    tab5_raster_dma_plan_t plan;
    const platform_display_rgb565_region_t games={0,123,964,448};
    assert(tab5_raster_dma_plan(source_base,1024,1280,&games,
        destination_base,1280,720,264,178,&plan));
    assert(plan.row_stride_bytes==2560U&&plan.row_count==448U);
    assert(plan.cache_offset==178U*2560U+512U&&plan.cache_bytes==1984U);
    assert(plan.boundary_count==2U);
    assert(plan.boundary_offset[0]==plan.cache_offset);
    assert(plan.boundary_offset[1]==178U*2560U+2432U);
    assert(plan.cache_bytes*plan.row_count==888832U);
    assert(plan.boundary_count*64U*plan.row_count==57344U);
    assert(plan.cache_bytes<plan.row_stride_bytes);
    const platform_display_rgb565_region_t full={0,0,1280,16};
    assert(tab5_raster_dma_plan(source_base,1280,720,&full,
        destination_base,1280,720,0,0,&plan));
    assert(plan.cache_bytes==plan.row_stride_bytes&&plan.boundary_count==0U);
    const platform_display_rgb565_region_t tiny={2,0,2,1};
    assert(tab5_raster_dma_plan(source_base,32,1,&tiny,
        destination_base,32,1,2,0,&plan));
    assert(plan.cache_bytes==64U&&plan.boundary_count==1U); /* same boundary line */
    const platform_display_rgb565_region_t odd_x={1,0,2,1},odd_width={0,0,3,1};
    assert(!tab5_raster_dma_plan(source_base,32,1,&odd_x,destination_base,32,1,2,0,&plan));
    assert(!tab5_raster_dma_plan(source_base,32,1,&odd_width,destination_base,32,1,2,0,&plan));
    assert(!tab5_raster_dma_plan(source_base,32,1,&tiny,destination_base,32,1,1,0,&plan));
    assert(!tab5_raster_dma_plan(source_base+2U,32,1,&tiny,destination_base,32,1,2,0,&plan));
    assert(!tab5_raster_dma_plan(source_base,33,32,&tiny,destination_base,32,32,2,0,&plan));
    assert(!tab5_raster_dma_plan(source_base,32,32,&tiny,destination_base,33,32,2,0,&plan));
    assert(!tab5_raster_dma_plan(source_base,32,32,&tiny,source_base+64U,32,32,2,0,&plan));
}

/* CPU and DMA have separate storage. Every destination line starts dirty;
 * invalidating without boundary C2M loses adjacent pixels. Fully copied lines
 * may be discarded, and complete untouched lines must remain cached/dirty.
 * CPU reads happen only after simulated DMA completion, matching the fence. */
static bool preserves_dirty_boundaries(unsigned x,unsigned width,bool preserve)
{
    enum { STRIDE=128, HEIGHT=8, PIXELS=STRIDE*HEIGHT, LINE_PIXELS=32, LINES=PIXELS/LINE_PIXELS };
    uint16_t cpu[PIXELS],memory[PIXELS]={0};
    bool valid[LINES],invalidated[LINES]={false};
    for(unsigned i=0;i<PIXELS;++i)cpu[i]=(uint16_t)(1000U+i);
    for(unsigned line=0;line<LINES;++line)valid[line]=true;
    const platform_display_rgb565_region_t region={0,1,(uint16_t)width,3};
    tab5_raster_dma_plan_t plan;
    assert(tab5_raster_dma_plan(source_base,STRIDE,HEIGHT,&region,
        destination_base,STRIDE,HEIGHT,x,2,&plan));
    for(unsigned row=0;row<plan.row_count;++row){
        const size_t row_bytes=(size_t)row*plan.row_stride_bytes;
        if(preserve)for(unsigned boundary=0;boundary<plan.boundary_count;++boundary){
            const size_t first=(row_bytes+plan.boundary_offset[boundary])/sizeof(uint16_t);
            memcpy(memory+first,cpu+first,64U);
        }
        const size_t first_line=(row_bytes+plan.cache_offset)/64U;
        for(size_t line=first_line;line<first_line+plan.cache_bytes/64U;++line){
            valid[line]=false;invalidated[line]=true;
        }
    }
    /* A speculative eviction of any still-valid dirty line must not overwrite
     * a DMA destination. Untouched lines retain CPU data until a later flush. */
    for(unsigned row=0;row<region.height;++row)for(unsigned column=0;column<width;++column){
        const unsigned i=(row+2U)*STRIDE+x+column;
        assert(!valid[i/LINE_PIXELS]);
        memory[i]=(uint16_t)(0xa000U+row*STRIDE+column);
    }
    for(unsigned line=0;line<LINES;++line)if(!invalidated[line]){
        assert(valid[line]);
        assert(memory[line*LINE_PIXELS]==0U); /* no full-row writeback */
    }
    for(unsigned i=0;i<PIXELS;++i){
        const unsigned line=i/LINE_PIXELS;
        if(!valid[line]){
            memcpy(cpu+line*LINE_PIXELS,memory+line*LINE_PIXELS,64U);valid[line]=true;
        }
        const unsigned column=i%STRIDE,row=i/STRIDE;
        const uint16_t expected=column>=x&&column<x+width&&row>=2U&&row<5U?
            (uint16_t)(0xa000U+(row-2U)*STRIDE+column-x):(uint16_t)(1000U+i);
        if(cpu[i]!=expected)return false;
    }
    return true;
}

int main(void)
{
    publication_bounds();dma_bounds();
    for(unsigned x=0;x<128U;x+=2U)for(unsigned width=2;width<=128U-x;width+=2U){
        assert(preserves_dirty_boundaries(x,width,true));
        /* Negative control: C2M is necessary precisely for partial boundaries. */
        const bool cache_aligned=x%32U==0U&&(x+width)%32U==0U;
        assert(preserves_dirty_boundaries(x,width,false)==cache_aligned);
    }
    puts("PASS: bounded idle publication, DMA alignment/fallback geometry, exhaustive dirty-boundary preservation, untouched cache lines and pre-DMA invalidation");
    return 0;
}
