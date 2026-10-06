// SPDX-License-Identifier: MIT
#include "tab5_frame_queue.h"
#include "platform_display_layout.h"
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
int main(void)
{
    ownership();damage();
    puts("PASS proposal: 3 buffers, two-refresh retirement, counter wrap/race, timeout preservation, ambiguous-handoff fail closed, blocking-source fence, 36-frame complete UI damage equivalence");
    return 0;
}
