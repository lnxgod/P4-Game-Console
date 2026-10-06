// SPDX-License-Identifier: MIT
#ifndef TAB5_FRAME_QUEUE_H
#define TAB5_FRAME_QUEUE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "platform/display_region.h"
enum { TAB5_FRAME_COUNT = 3, TAB5_RETIRE_REFRESHES = 2 };
/* Pinned IDF publishes cur_fb_index before DMA samples it. Keep each replaced
 * buffer immutable for two refresh callbacks AFTER publishing its successor. */
typedef struct {
    unsigned selected;
    int writing;
    bool source_busy, failed;
    bool retired[TAB5_FRAME_COUNT], pending[TAB5_FRAME_COUNT];
    uint32_t retired_at[TAB5_FRAME_COUNT], published_at[TAB5_FRAME_COUNT];
    uint32_t completed;
} tab5_frame_queue_t;
static inline void tab5_frame_queue_init(tab5_frame_queue_t *q)
{ *q=(tab5_frame_queue_t){.selected=0U,.writing=-1}; }
static inline void tab5_frame_queue_observe(tab5_frame_queue_t *q,uint32_t refresh)
{
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)
        if(q->pending[i]&&(uint32_t)(refresh-q->published_at[i])>=TAB5_RETIRE_REFRESHES){
            q->pending[i]=false;if(q->completed!=UINT32_MAX)++q->completed;
        }
}
static inline int tab5_frame_queue_begin(tab5_frame_queue_t *q,uint32_t refresh)
{
    tab5_frame_queue_observe(q,refresh);
    if(q->failed||q->writing>=0)return -1;
    for(unsigned offset=1U;offset<TAB5_FRAME_COUNT;++offset){
        const unsigned i=(q->selected+offset)%TAB5_FRAME_COUNT;
        if(!q->retired[i]||(uint32_t)(refresh-q->retired_at[i])>=TAB5_RETIRE_REFRESHES){
            q->writing=(int)i;q->source_busy=true;return (int)i;
        }
    }
    return -1;
}
static inline void tab5_frame_queue_source_complete(tab5_frame_queue_t *q)
{ q->source_busy=false; }
static inline bool tab5_frame_queue_publish(tab5_frame_queue_t *q,uint32_t refresh)
{
    if(q->failed||q->writing<0||q->source_busy)return false;
    const unsigned next=(unsigned)q->writing;
    q->retired[q->selected]=true;q->retired_at[q->selected]=refresh;
    q->selected=next;q->retired[next]=false;
    q->pending[next]=true;q->published_at[next]=refresh;q->writing=-1;
    return true;
}
static inline void tab5_frame_queue_cancel(tab5_frame_queue_t *q,bool ambiguous_handoff)
{ q->writing=-1;q->source_busy=false;q->failed|=ambiguous_handoff; }
typedef struct {
    const uint16_t *source;
    size_t stride;
    platform_display_rgb565_region_t damage;
} tab5_frame_history_t;
static inline platform_display_rgb565_region_t tab5_damage_union(
    platform_display_rgb565_region_t a,platform_display_rgb565_region_t b)
{
    if(!a.width)return b;
    if(!b.width)return a;
    const unsigned x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y;
    const unsigned ar=(unsigned)a.x+a.width,br=(unsigned)b.x+b.width;
    const unsigned ab=(unsigned)a.y+a.height,bb=(unsigned)b.y+b.height;
    return (platform_display_rgb565_region_t){(uint16_t)x,(uint16_t)y,
        (uint16_t)((ar>br?ar:br)-x),(uint16_t)((ab>bb?ab:bb)-y)};
}
static inline void tab5_frame_history_commit(tab5_frame_history_t history[TAB5_FRAME_COUNT],
    unsigned written,const uint16_t *source,size_t stride,platform_display_rgb565_region_t damage)
{
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i){
        if(i==written)history[i]=(tab5_frame_history_t){.source=source,.stride=stride};
        else if(history[i].source==source&&history[i].stride==stride)
            history[i].damage=tab5_damage_union(history[i].damage,damage);
        else history[i]=(tab5_frame_history_t){0};
    }
}
#endif
