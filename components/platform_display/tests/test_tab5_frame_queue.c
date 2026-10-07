// SPDX-License-Identifier: MIT
#include "tab5_frame_queue.h"
#include "platform_display_layout.h"
#include "tab5_raster_copy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void publish(tab5_frame_queue_t *q,uint32_t stamp)
{
    assert(q->source_busy);
    /* A synchronous PPA operation has not yet consumed the input; handoff and
       source reuse are prohibited until it returns. */
    assert(!tab5_frame_queue_publish(q,stamp));
    tab5_frame_queue_source_complete(q);
    assert(!q->source_busy);
    assert(tab5_frame_queue_publish(q,stamp));
}
static void ownership(void)
{
    for(unsigned race=0;race<3U;++race){
        tab5_frame_queue_t q;tab5_frame_queue_init(&q);
        uint32_t refresh=UINT32_MAX-2U;
        assert(tab5_frame_queue_begin(&q,refresh)==1);
        assert(tab5_frame_queue_begin(&q,refresh)==-1); /* no concurrent writer */
        publish(&q,refresh+race);refresh+=race;
        assert(tab5_frame_queue_begin(&q,refresh)==2);publish(&q,refresh);
        assert(tab5_frame_queue_begin(&q,refresh)==-1);
        assert(tab5_frame_queue_begin(&q,refresh+1U)==-1);
        /* A timeout leaves the pending/selected buffers owned and immutable. */
        assert(q.selected==2U&&q.writing==-1&&q.pending[2]);
        assert(tab5_frame_queue_begin(&q,refresh+2U)==0);
        publish(&q,refresh+2U);
        assert(tab5_frame_queue_begin(&q,refresh+2U)==1); /* retired, not selected */
        tab5_frame_queue_cancel(&q,false); /* transform failed before publication */
        assert(q.selected==0U&&!q.failed&&!q.source_busy);
        assert(tab5_frame_queue_begin(&q,refresh+2U)==1);
        tab5_frame_queue_cancel(&q,true); /* ambiguous handoff: fail closed */
        assert(tab5_frame_queue_begin(&q,refresh+500U)==-1);
    }
    /* At steady state one new frame per refresh is possible, without reducing
       any replaced buffer's two-refresh ownership fence. */
    tab5_frame_queue_t q;tab5_frame_queue_init(&q);
    for(uint32_t refresh=0;refresh<1000U;++refresh){
        int slot=tab5_frame_queue_begin(&q,refresh);
        assert(slot>=0&&(unsigned)slot!=q.selected);
        if(q.retired[slot])assert((uint32_t)(refresh-q.retired_at[slot])>=2U);
        publish(&q,refresh);
    }
}
static void damage(void)
{
    const size_t pixels=1280U*720U;
    uint16_t *source=calloc(pixels,sizeof(*source));
    uint16_t *frames[3]={calloc(pixels,sizeof(*source)),calloc(pixels,sizeof(*source)),calloc(pixels,sizeof(*source))};
    assert(source&&frames[0]&&frames[1]&&frames[2]);
    tab5_frame_history_t history[3]={{0}};
    tab5_frame_queue_t q;tab5_frame_queue_init(&q);
    for(unsigned frame=0;frame<36U;++frame){
        platform_display_rgb565_region_t current={(uint16_t)(frame*29U),(uint16_t)(frame*13U),73U,151U};
        if(frame%9U==0U)current=(platform_display_rgb565_region_t){0,0,1280,720};
        if(frame==5U)current=(platform_display_rgb565_region_t){0,0,1,720};
        for(unsigned y=current.y;y<(unsigned)current.y+current.height;++y)
            for(unsigned x=current.x;x<(unsigned)current.x+current.width;++x)source[(size_t)y*1280U+x]=(uint16_t)(frame*701U+x+y);
        const int slot=tab5_frame_queue_begin(&q,frame*2U);assert(slot>=0);
        platform_display_rgb565_region_t region,mapped;
        const bool replay=history[slot].source==source;
        const platform_display_rgb565_region_t previous=history[slot].damage.width?history[slot].damage:current;
        assert(platform_display_layout_tab5_damage(&current,replay?&previous:NULL,&region,&mapped));
        for(unsigned y=mapped.y;y<(unsigned)mapped.y+mapped.height;++y)
            for(unsigned x=mapped.x;x<(unsigned)mapped.x+mapped.width;++x)
                frames[slot][(size_t)y*720U+x]=source[(size_t)x*1280U+1279U-y];
        for(unsigned y=0;y<1280U;++y)for(unsigned x=0;x<720U;++x)
            assert(frames[slot][(size_t)y*720U+x]==source[(size_t)x*1280U+1279U-y]);
        publish(&q,frame*2U);
        tab5_frame_history_commit(history,(unsigned)slot,source,1280U,current);
        if(frame==17U)memset(history,0,sizeof(history)); /* dropped-frame history reset */
    }
    free(source);for(unsigned i=0;i<3U;++i)free(frames[i]);
}
static void publication(void)
{
    const platform_display_rgb565_region_t damage[]={
        {0,0,1,720},{1279,0,1,720},{264,178,996,448},{0,0,1280,720},
    };
    for(size_t i=0;i<sizeof(damage)/sizeof(damage[0]);++i){
        platform_display_rgb565_region_t source,mapped;
        tab5_frame_publication_t rows;
        assert(platform_display_layout_tab5_damage(&damage[i],&damage[i],&source,&mapped));
        assert(tab5_frame_publication_rows(&mapped,false,false,&rows));
        assert(rows.y==mapped.y&&rows.height==mapped.height&&!rows.cpu_dirty);
        assert((size_t)(rows.y+rows.height)*720U*sizeof(uint16_t)<=1280U*720U*sizeof(uint16_t));
        assert(tab5_frame_publication_rows(&mapped,true,false,&rows));
        assert(rows.y==0U&&rows.height==1280U&&rows.cpu_dirty); /* CPU fallback */
        assert(tab5_frame_publication_rows(&mapped,false,true,&rows));
        assert(rows.y==0U&&rows.height==1280U&&rows.cpu_dirty); /* CPU game margins */
    }
    tab5_frame_publication_t rows;
    const platform_display_rgb565_region_t invalid={0,1279,720,2};
    assert(!tab5_frame_publication_rows(&invalid,false,false,&rows));
}
static void copy_bounds(void)
{
    const uintptr_t source=UINT32_C(0x10000000),destination=UINT32_C(0x20000000);
    const platform_display_rgb565_region_t region={2,5,9,7};
    tab5_raster_copy_plan_t plan;
    assert(tab5_raster_copy_plan(source,32,32,&region,destination,17,64,4,3,&plan));
    assert(plan.source_bytes==2048U&&plan.destination_bytes==2176U);
    assert(plan.destination_cache_offset==64U&&plan.destination_cache_bytes==320U);
    assert(tab5_raster_copy_plan(source,32,32,&region,destination,17,64,4,57,&plan));
    assert(plan.destination_cache_offset+plan.destination_cache_bytes==plan.destination_bytes);
    assert(!tab5_raster_copy_plan(source,32,32,&region,source+64U,17,64,4,3,&plan));
    assert(!tab5_raster_copy_plan(source,32,32,&region,destination+2U,17,64,4,3,&plan));
    assert(!tab5_raster_copy_plan(source,32,32,&region,destination,17,63,4,3,&plan));
    assert(!tab5_raster_copy_plan(source,32,32,&region,destination,17,64,9,3,&plan));
    assert(!tab5_raster_copy_plan(source,32,32,&region,destination,17,64,4,58,&plan));
    assert(!tab5_raster_copy_plan(source,4096,32,&region,destination,17,64,4,3,&plan));
    assert(!tab5_raster_copy_plan(UINTPTR_MAX-1000U,32,32,&region,destination,17,64,4,3,&plan));
    const platform_display_rgb565_region_t bad={31,31,2,2};
    assert(!tab5_raster_copy_plan(source,32,32,&bad,destination,17,64,4,3,&plan));
}
/* Model separate dirty CPU cache and DMA memory. PPA pre-invalidates aligned
 * full rows, including dirty chrome outside the copied rectangle. A missing
 * preservation writeback must fail this model even though copied pixels match. */
static bool copy_preserves_neighbours(bool preserve)
{
    enum { PIXELS=17*64, LINE_PIXELS=32, LINES=PIXELS/LINE_PIXELS };
    uint16_t cpu[PIXELS],memory[PIXELS]={0};bool valid[LINES];
    for(unsigned i=0;i<PIXELS;++i)cpu[i]=(uint16_t)(1000U+i);
    for(unsigned i=0;i<LINES;++i)valid[i]=true;
    const platform_display_rgb565_region_t region={2,5,9,7};
    tab5_raster_copy_plan_t plan;
    assert(tab5_raster_copy_plan(UINT32_C(0x10000000),32,32,&region,
        UINT32_C(0x20000000),17,64,4,3,&plan));
    const size_t first=plan.destination_cache_offset/sizeof(uint16_t);
    const size_t count=plan.destination_cache_bytes/sizeof(uint16_t);
    if(preserve)memcpy(memory+first,cpu+first,count*sizeof(uint16_t));
    for(size_t line=first/LINE_PIXELS;line<(first+count)/LINE_PIXELS;++line)valid[line]=false;
    for(unsigned y=0;y<region.height;++y)for(unsigned x=0;x<region.width;++x)
        memory[(y+3U)*17U+x+4U]=(uint16_t)(0xa000U+(y+region.y)*32U+x+region.x);
    for(unsigned i=0;i<PIXELS;++i){
        const unsigned line=i/LINE_PIXELS;
        if(!valid[line]){memcpy(cpu+line*LINE_PIXELS,memory+line*LINE_PIXELS,64U);valid[line]=true;}
        const unsigned x=i%17U,y=i/17U;
        const uint16_t expected=x>=4U&&x<13U&&y>=3U&&y<10U?
            (uint16_t)(0xa000U+(y-3U+region.y)*32U+x-4U+region.x):(uint16_t)(1000U+i);
        if(cpu[i]!=expected)return false;
    }
    return true;
}
static void telemetry(void)
{
    tab5_interactive_handoff_t sample={.input_us=100,.handoff_us=200,
        .published_refresh=UINT32_MAX,.pending=true};
    assert(!tab5_interactive_refresh_ready(&sample,UINT32_MAX));
    assert(!tab5_interactive_refresh_ready(&sample,0U));
    assert(tab5_interactive_refresh_ready(&sample,1U));
    sample.pending=false;assert(!tab5_interactive_refresh_ready(&sample,2U));
    assert(tab5_elapsed_us(300,100)==200U);
    assert(tab5_elapsed_us(300,0)==0U&&tab5_elapsed_us(100,300)==0U);
    assert(tab5_elapsed_us(INT64_C(5000000000),1)==UINT32_MAX);
    assert(tab5_saturating_add(UINT32_MAX-4U,5U)==UINT32_MAX);
    assert(tab5_saturating_add(40U,2U)==42U);
}
int main(void)
{
    ownership();damage();publication();copy_bounds();telemetry();
    assert(copy_preserves_neighbours(true));assert(!copy_preserves_neighbours(false));
    puts("PASS: 3-buffer retirement, blocking-source fence, 36-frame damage equivalence, exact publication rows, CPU fallback/margin flush, disjoint copy bounds, dirty-neighbour cache preservation, conservative refresh telemetry");
    return 0;
}
