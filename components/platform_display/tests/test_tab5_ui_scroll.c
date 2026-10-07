// SPDX-License-Identifier: MIT
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform_display_layout.h"
#include "tab5_frame_queue.h"
#include "tab5_ui_scroll.h"
#include "tab5_ui_region.h"

enum { LOGICAL_WIDTH=1280,LOGICAL_HEIGHT=720,PHYSICAL_WIDTH=720,PHYSICAL_HEIGHT=1280 };
static const size_t pixels=(size_t)LOGICAL_WIDTH*LOGICAL_HEIGHT;

static bool inside(platform_display_rgb565_region_t r,unsigned x,unsigned y)
{
    return x>=r.x&&y>=r.y&&x<(unsigned)r.x+r.width&&y<(unsigned)r.y+r.height;
}
static uint16_t pixel(const platform_display_ui_scroll_t *state,unsigned x,unsigned y)
{
    if(inside(state->viewport,x,y)){
        const uint32_t world=(uint32_t)((int64_t)y-state->viewport.y+state->current_offset);
        /* World-row-dependent content includes a focus/selection-like band,
         * which must translate with content rather than remain at screen y. */
        return (uint16_t)((world*97U+x*13U+(world%29U==0U?0x7000U:0U))&0xffffU);
    }
    if(inside(state->scrollbar,x,y))return (uint16_t)(0x4000U+
        ((uint32_t)state->current_offset*37U+x+y*31U)%0x4000U);
    return (uint16_t)((state->context*131U+x*53U+y*7U)&0xffffU);
}
static void render_rectangle(uint16_t *logical,const platform_display_ui_scroll_t *state,
    platform_display_rgb565_region_t region)
{
    for(unsigned y=region.y;y<(unsigned)region.y+region.height;++y)
        for(unsigned x=region.x;x<(unsigned)region.x+region.width;++x)
            logical[(size_t)y*LOGICAL_WIDTH+x]=pixel(state,x,y);
}
static void full_render(uint16_t *logical,const platform_display_ui_scroll_t *state)
{
    render_rectangle(logical,state,(platform_display_rgb565_region_t){0,0,1280,720});
}
static void rotate_rectangle(const uint16_t *logical,uint16_t *physical,
    platform_display_rgb565_region_t region)
{
    for(unsigned y=region.y;y<(unsigned)region.y+region.height;++y)
        for(unsigned x=region.x;x<(unsigned)region.x+region.width;++x)
            physical[(size_t)(1279U-x)*PHYSICAL_WIDTH+y]=logical[(size_t)y*LOGICAL_WIDTH+x];
}
static void dma_rectangle(const uint16_t *source,uint16_t *destination,
    platform_display_rgb565_region_t src,platform_display_rgb565_region_t dst)
{
    assert(source!=destination&&src.width==dst.width&&src.height==dst.height);
    for(unsigned row=0;row<src.height;++row)
        memcpy(destination+((size_t)dst.y+row)*PHYSICAL_WIDTH+dst.x,
            source+((size_t)src.y+row)*PHYSICAL_WIDTH+src.x,(size_t)src.width*sizeof(uint16_t));
}
static platform_display_ui_scroll_t state_for(uint16_t height)
{
    return (platform_display_ui_scroll_t){.context=1U,.previous_context=1U,.previous_offset=1000,.current_offset=1000,
        .viewport={264,178,964,height},.scrollbar={1228,178,32,height}};
}
static void geometry_test(void)
{
    for(unsigned page=0;page<2U;++page){
        platform_display_ui_scroll_t state=state_for(page?358U:448U);
        assert(tab5_ui_scroll_state_valid(&state));
        for(unsigned shift=1;shift<state.viewport.height;++shift){
            for(unsigned sign=0;sign<2U;++sign){
                state.current_offset=state.previous_offset+(sign?-(int32_t)shift:(int32_t)shift);
                tab5_ui_scroll_plan_t plan;
                assert(tab5_ui_scroll_plan(&state,&plan));
                const platform_display_rgb565_region_t physical=tab5_ui_scroll_rotate(state.viewport);
                assert(plan.interior_source.width==state.viewport.height-shift);
                assert(plan.interior_source.height==state.viewport.width);
                assert(plan.interior_destination.width==plan.interior_source.width);
                assert(plan.interior_destination.height==plan.interior_source.height);
                assert(plan.interior_source.x+(unsigned)plan.interior_source.width<=720U);
                assert(plan.interior_destination.x+(unsigned)plan.interior_destination.width<=720U);
                assert(plan.interior_source.y+(unsigned)plan.interior_source.height<=1280U);
                assert(plan.interior_destination.y+(unsigned)plan.interior_destination.height<=1280U);
                assert((uint32_t)plan.interior_source.width*plan.interior_source.height+
                    (uint32_t)plan.exposed.width*plan.exposed.height==
                    (uint32_t)state.viewport.width*state.viewport.height);
                assert(plan.exposed_destination.width==shift);
                assert(plan.exposed_destination.height==state.viewport.width);
                assert(plan.physical_damage.y==20U&&plan.physical_damage.height==996U);
                assert(plan.physical_damage.x==178U&&plan.physical_damage.width==state.viewport.height);
                /* Both odd/even offsets and widths are deliberately exercised
                 * with full physical bases and stride 720px = 1440 bytes. */
                assert(plan.interior_source.width%2U==(state.viewport.height-shift)%2U);
                if(sign){
                    assert(plan.interior_source.x==physical.x);
                    assert(plan.interior_destination.x==physical.x+shift);
                    assert(plan.exposed.y==state.viewport.y);
                }else{
                    assert(plan.interior_source.x==physical.x+shift);
                    assert(plan.interior_destination.x==physical.x);
                    assert(plan.exposed.y==state.viewport.y+state.viewport.height-shift);
                }
            }
        }
        tab5_ui_scroll_plan_t plan;
        state.current_offset=state.previous_offset;
        assert(!tab5_ui_scroll_plan(&state,&plan));
        state.current_offset=state.previous_offset+state.viewport.height;
        assert(!tab5_ui_scroll_plan(&state,&plan));
        state.previous_offset=INT32_MIN;state.current_offset=INT32_MAX;
        assert(!tab5_ui_scroll_plan(&state,&plan));
        state.previous_offset=INT32_MAX;state.current_offset=INT32_MIN;
        assert(!tab5_ui_scroll_plan(&state,&plan));
    }
    platform_display_ui_scroll_t invalid=state_for(448U);
    invalid.scrollbar.x=1200U;
    assert(!tab5_ui_scroll_state_valid(&invalid));
    invalid=state_for(448U);invalid.viewport.height=721U;
    assert(!tab5_ui_scroll_state_valid(&invalid));
}
static void repair_geometry_test(void)
{
    const platform_display_ui_scroll_t states[]={
        {.viewport={264,178,964,448},.scrollbar={1228,178,32,448}},
        {.viewport={264,178,964,358},.scrollbar={1228,178,32,358}},
        {.viewport={0,0,1278,720},.scrollbar={1278,0,2,720}},
        {.viewport={0,0,1000,500},.scrollbar={1000,0,32,500}},
        {.viewport={100,20,1000,700},.scrollbar={1100,20,180,700}},
    };
    uint8_t *coverage=calloc(pixels,sizeof(*coverage));
    assert(coverage);
    for(size_t test=0;test<sizeof(states)/sizeof(states[0]);++test){
        tab5_ui_scroll_repair_t repair;
        assert(tab5_ui_scroll_repair_plan(&states[test],&repair));
        assert(repair.count<=4U);
        memset(coverage,0,pixels);
        for(unsigned i=0;i<repair.count;++i){
            const platform_display_rgb565_region_t r=repair.rectangles[i];
            assert(r.width&&r.height&&r.x+r.width<=720U&&r.y+r.height<=1280U);
            for(unsigned y=r.y;y<(unsigned)r.y+r.height;++y)
                for(unsigned x=r.x;x<(unsigned)r.x+r.width;++x)
                    ++coverage[(size_t)y*PHYSICAL_WIDTH+x];
        }
        const platform_display_rgb565_region_t viewport=tab5_ui_scroll_rotate(states[test].viewport);
        const platform_display_rgb565_region_t scrollbar=tab5_ui_scroll_rotate(states[test].scrollbar);
        for(unsigned y=0;y<PHYSICAL_HEIGHT;++y)
            for(unsigned x=0;x<PHYSICAL_WIDTH;++x)
                assert(coverage[(size_t)y*PHYSICAL_WIDTH+x]==
                    (inside(viewport,x,y)||inside(scrollbar,x,y)?0U:1U));
    }
    platform_display_ui_scroll_t gap=state_for(448U);
    gap.scrollbar.x+=1U;
    tab5_ui_scroll_repair_t repair;
    assert(tab5_ui_scroll_state_valid(&gap));
    assert(!tab5_ui_scroll_repair_plan(&gap,&repair));
    gap=state_for(448U);gap.scrollbar.height-=1U;
    assert(tab5_ui_scroll_state_valid(&gap));
    assert(!tab5_ui_scroll_repair_plan(&gap,&repair));
    free(coverage);
}
static void rejection_and_ownership_test(void)
{
    uint16_t source[2]={0},other[2]={0};
    platform_display_ui_scroll_t state=state_for(448U);
    tab5_ui_scroll_tag_t selected={0},target={0};
    tab5_ui_scroll_tag_commit(&selected,source,1280U,&state);
    tab5_ui_scroll_tag_commit(&target,source,1280U,&state);
    state.current_offset+=1;
    assert(tab5_ui_scroll_eligible(&selected,&target,source,1280U,&state,true));
    assert(!tab5_ui_scroll_eligible(&selected,&target,source,1280U,&state,false));
    assert(!tab5_ui_scroll_eligible(&selected,&target,other,1280U,&state,true));
    assert(!tab5_ui_scroll_eligible(&selected,&target,source,1281U,&state,true));
    tab5_ui_scroll_repair_t repair;
    bool needs_repair=false;
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&state,true,&repair,&needs_repair));
    assert(!needs_repair&&repair.count==0U);
    --target.state.context;
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&state,true,&repair,&needs_repair));
    assert(needs_repair&&repair.count==4U);
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&state,false,&repair,&needs_repair));
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,other,1280U,&state,true,&repair,&needs_repair));
    target.valid=false;
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&state,true,&repair,&needs_repair));
    assert(needs_repair);
    /* Generic disjoint geometry remains supported when target already matches,
     * but a stationary gap blocks warm repair before any pixel mutation. */
    platform_display_ui_scroll_t gap=state;gap.scrollbar.x+=1U;
    tab5_ui_scroll_tag_commit(&selected,source,1280U,&gap);
    selected.state.current_offset=gap.previous_offset;
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&gap,true,&repair,&needs_repair));
    tab5_ui_scroll_tag_commit(&target,source,1280U,&gap);
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&gap,true,&repair,&needs_repair));
    assert(!needs_repair);
    platform_display_ui_scroll_t prior=state;prior.current_offset=prior.previous_offset;
    tab5_ui_scroll_tag_commit(&selected,source,1280U,&prior);
    tab5_ui_scroll_tag_commit(&target,source,1280U,&prior);
    state.previous_offset+=1;
    assert(!tab5_ui_scroll_eligible(&selected,&target,source,1280U,&state,true));
    state.previous_offset-=1;state.context+=1U;
    const tab5_ui_scroll_tag_t saved_selected=selected,saved_target=target;
    assert(!tab5_ui_scroll_eligible(&selected,&target,source,1280U,&state,true));
    tab5_frame_queue_t queue;
    tab5_frame_queue_init(&queue);
    assert(tab5_frame_queue_begin(&queue,0U)==1);
    /* Pre-accept rejection simply abandons reservation, preserving warm tags
     * and immutable selected source. No pixel or history metadata changes. */
    tab5_frame_queue_cancel(&queue,false);
    assert(!memcmp(&selected,&saved_selected,sizeof(selected)));
    assert(!memcmp(&target,&saved_target,sizeof(target)));
    assert(queue.selected==0U&&!queue.failed&&queue.writing==-1);
    assert(tab5_frame_queue_begin(&queue,0U)==1);
    assert(queue.source_busy);
    assert(!tab5_frame_queue_publish(&queue,0U));
    /* An accepted/missing completion cannot release the reservation or source.
     * A late success permits source_complete only after all DMA/PPA pieces. */
    assert(queue.writing==1&&queue.selected==0U&&queue.source_busy);
    tab5_frame_queue_source_complete(&queue);
    assert(tab5_frame_queue_publish(&queue,0U));
    /* Post-mutation error invalidates eligibility; it cannot reuse old tags.
     * Model the adapter's conservative invalidation of all scroll metadata. */
    selected.valid=false;target.valid=false;
    assert(!tab5_ui_scroll_eligible(&selected,&target,source,1280U,&state,true));
}
static void triple_buffer_pixels_test(uint16_t height)
{
    uint16_t *logical=malloc(pixels*sizeof(*logical));
    uint16_t *reference_source=malloc(pixels*sizeof(*reference_source));
    uint16_t *reference=malloc(pixels*sizeof(*reference));
    uint16_t *saved_frames=malloc(TAB5_FRAME_COUNT*pixels*sizeof(*saved_frames));
    uint16_t *frames[TAB5_FRAME_COUNT];
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i){frames[i]=calloc(pixels,sizeof(*frames[i]));assert(frames[i]);}
    assert(logical&&reference_source&&reference&&saved_frames);
    tab5_frame_queue_t queue;
    tab5_frame_queue_init(&queue);
    tab5_ui_scroll_tag_t tags[TAB5_FRAME_COUNT]={0};
    /* Startup invalidates the driver's already C2M-published black buffers;
     * all initial slots are DMA-clean, even though only the selected UI slot
     * acquires a context tag after the first authoritative presentation. */
    bool dma_clean[TAB5_FRAME_COUNT]={true,true,true};
    platform_display_ui_scroll_t state=state_for(height);
    const int32_t shifts[]={1,2,-1,-2,17,-19,127,-129,3,-3,29,-31,
        (int32_t)height-1,-((int32_t)height-2),(int32_t)height,5,-7,0};
    unsigned successful_delta=0U,fallback=0U,repaired=0U;
    uint32_t refresh=0U;
    for(unsigned step=0;step<90U;++step){
        state.previous_context=state.context;
        state.previous_offset=state.current_offset;
        state.current_offset+=shifts[step%(sizeof(shifts)/sizeof(shifts[0]))];
        if(step==18U||step==43U||step==66U)++state.context;
        const unsigned selected=queue.selected;
        const int slot=tab5_frame_queue_begin(&queue,refresh);
        assert(slot>=0&&(unsigned)slot!=selected);
        for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)
            memcpy(saved_frames+(size_t)i*pixels,frames[i],pixels*sizeof(*saved_frames));
        tab5_ui_scroll_plan_t plan;
        tab5_ui_scroll_repair_t repair;
        bool needs_repair=false;
        const bool geometry=tab5_ui_scroll_plan(&state,&plan);
        const bool eligible=geometry&&tab5_ui_scroll_composition_plan(&tags[selected],&tags[slot],
            logical,1280U,&state,dma_clean[slot],&repair,&needs_repair);
        /* After a selected-context change forced one authoritative frame,
         * both subsequent stale-context targets repair without warm fallback. */
        if(step==1U||step==2U||step==19U||step==20U||step==44U||step==45U)
            assert(eligible&&needs_repair);
        if(eligible){
            /* Poison every unused logical pixel: physical translation must use
             * the selected immutable frame, never stale logical viewport data. */
            for(size_t i=0;i<pixels;++i)logical[i]=0xbeefU;
            render_rectangle(logical,&state,plan.exposed);
            render_rectangle(logical,&state,state.scrollbar);
            if(needs_repair){
                for(unsigned i=0;i<repair.count;++i){
                    dma_rectangle(frames[selected],frames[slot],repair.rectangles[i],repair.rectangles[i]);
                    /* Every complement piece joins, but publication must wait
                     * for the remaining complement/interior/strip/bar pieces. */
                    assert(queue.source_busy&&!tab5_frame_queue_publish(&queue,refresh));
                }
                ++repaired;
            }
            dma_rectangle(frames[selected],frames[slot],plan.interior_source,plan.interior_destination);
            assert(!tab5_frame_queue_publish(&queue,refresh));
            rotate_rectangle(logical,frames[slot],plan.exposed);
            rotate_rectangle(logical,frames[slot],state.scrollbar);
            ++successful_delta;
        }else{
            const tab5_ui_scroll_tag_t saved[TAB5_FRAME_COUNT]={tags[0],tags[1],tags[2]};
            /* Exact NOT_SUPPORTED cancellation retains warm slots. */
            tab5_frame_queue_cancel(&queue,false);
            assert(!memcmp(saved,tags,sizeof(tags)));
            assert(tab5_frame_queue_begin(&queue,refresh)==slot);
            full_render(logical,&state);
            assert(platform_display_layout_rgb565_1280x720(logical,1280U,frames[slot],720U,1280U));
            dma_clean[slot]=true;
            ++fallback;
        }
        tab5_frame_queue_source_complete(&queue);
        assert(tab5_frame_queue_publish(&queue,refresh));
        tab5_ui_scroll_tag_commit(&tags[slot],logical,1280U,&state);
        full_render(reference_source,&state);
        assert(platform_display_layout_rgb565_1280x720(reference_source,1280U,reference,720U,1280U));
        assert(!memcmp(frames[queue.selected],reference,pixels*sizeof(*reference)));
        for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)
            if(i!=(unsigned)slot)assert(!memcmp(frames[i],saved_frames+(size_t)i*pixels,
                pixels*sizeof(*saved_frames)));
        /* The selected source cannot be chosen as a writer. Replaced frames
         * retain the real two-refresh fence, including when tags match. */
        assert(queue.retired[selected]&&queue.retired_at[selected]==refresh);
        ++refresh;
    }
    assert(successful_delta>40U&&fallback>8U&&repaired>=5U);
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)free(frames[i]);
    free(logical);free(reference_source);free(reference);free(saved_frames);
}
static void region_clone_eligibility_test(void)
{
    uint16_t source[2]={0},other[2]={0};
    const platform_display_rgb565_region_t damage={15,25,97,51},whole={0,0,1280,720};
    const tab5_frame_history_t selected={.source=source,.stride=1280U};
    tab5_ui_scroll_tag_t tag={0};
    const uintptr_t a=0x100000U,b=0x300000U;
#define ELIGIBLE(d,r,replay,h,t,src,dst,available,sclean,dclean) \
    tab5_ui_region_clone_eligible(source,1280U,d,r,replay,h,t,src,dst,available,sclean,dclean)
    assert(ELIGIBLE(&damage,&whole,false,&selected,&tag,a,b,true,true,true));
    assert(!ELIGIBLE(&damage,&damage,true,&selected,&tag,a,b,true,true,true));
    const platform_display_rgb565_region_t half={0,0,1280,360},large={0,0,1280,361};
    assert(!ELIGIBLE(&damage,&half,true,&selected,&tag,a,b,true,true,true));
    assert(ELIGIBLE(&damage,&large,true,&selected,&tag,a,b,true,true,true));
    assert(!ELIGIBLE(&large,&large,true,&selected,&tag,a,b,true,true,true));
    assert(!ELIGIBLE(&whole,&whole,false,&selected,&tag,a,b,true,true,true));
    assert(!ELIGIBLE(&damage,&whole,false,&selected,&tag,a,b,false,true,true));
    assert(!ELIGIBLE(&damage,&whole,false,&selected,&tag,a,b,true,false,true));
    assert(!ELIGIBLE(&damage,&whole,false,&selected,&tag,a,b,true,true,false));
    assert(!ELIGIBLE(&damage,&whole,false,&selected,&tag,a,a,true,true,true));
    assert(!ELIGIBLE(&damage,&whole,false,&selected,&tag,a,a+64U,true,true,true));
    assert(!ELIGIBLE(&damage,&whole,false,&selected,&tag,a,b+2U,true,true,true));
    assert(!ELIGIBLE(&damage,&whole,false,&selected,&tag,a,UINTPTR_MAX-63U,true,true,true));
    assert(!tab5_ui_region_clone_eligible(other,1280U,&damage,&whole,false,&selected,&tag,
        a,b,true,true,true));
    assert(!tab5_ui_region_clone_eligible(source,1281U,&damage,&whole,false,&selected,&tag,
        a,b,true,true,true));
    /* Scroll clears ordinary histories, but its published tag proves that the
     * selected physical frame came from this exact source/stride. */
    platform_display_ui_scroll_t state=state_for(448U);
    tab5_ui_scroll_tag_commit(&tag,source,1280U,&state);
    assert(ELIGIBLE(&damage,&whole,false,NULL,&tag,a,b,true,true,true));
    tag.valid=false;
    assert(!ELIGIBLE(&damage,&whole,false,NULL,&tag,a,b,true,true,true));
#undef ELIGIBLE
}
static void region_clone_pixels_test(void)
{
    uint16_t *logical[2]={malloc(pixels*sizeof(uint16_t)),malloc(pixels*sizeof(uint16_t))};
    uint16_t *reference=malloc(pixels*sizeof(uint16_t));
    uint16_t *saved=malloc(TAB5_FRAME_COUNT*pixels*sizeof(uint16_t));
    uint16_t *frames[TAB5_FRAME_COUNT];
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i){
        frames[i]=aligned_alloc(64U,pixels*sizeof(uint16_t));assert(frames[i]);
        memset(frames[i],0,pixels*sizeof(uint16_t));
    }
    assert(logical[0]&&logical[1]&&reference&&saved);
    platform_display_ui_scroll_t state=state_for(448U);
    full_render(logical[0],&state);memcpy(logical[1],logical[0],pixels*sizeof(uint16_t));
    tab5_frame_queue_t queue;tab5_frame_queue_init(&queue);
    tab5_frame_history_t history[TAB5_FRAME_COUNT]={0};
    tab5_ui_scroll_tag_t tags[TAB5_FRAME_COUNT]={0};
    const platform_display_rgb565_region_t whole={0,0,1280,720},physical_whole={0,0,720,1280};
    int slot=tab5_frame_queue_begin(&queue,0U);assert(slot==1);
    rotate_rectangle(logical[0],frames[slot],whole);
    tab5_frame_queue_source_complete(&queue);assert(tab5_frame_queue_publish(&queue,0U));
    tab5_frame_history_commit(history,(unsigned)slot,logical[0],1280U,whole);
    unsigned source_index=0U,clones=0U,replays=0U;
    const platform_display_rgb565_region_t damages[]={
        {15,25,97,51},{1269,709,11,11},{264,178,965,449},
        {0,719,1280,1},{1279,0,1,720},{1,1,1279,719},
        {17,27,95,49},{1229,179,31,447},
    };
    for(unsigned step=0;step<40U;++step){
        if(step==17U){
            memcpy(logical[1],logical[0],pixels*sizeof(uint16_t));source_index=1U;
        }
        uint16_t *source=logical[source_index];
        const platform_display_rgb565_region_t current=step<8U?
            (platform_display_rgb565_region_t){(uint16_t)(15U+step%3U*2U),
                (uint16_t)(25U+step%3U*2U),97,51}:
            damages[step%(sizeof(damages)/sizeof(damages[0]))];
        for(unsigned y=current.y;y<(unsigned)current.y+current.height;++y)
            for(unsigned x=current.x;x<(unsigned)current.x+current.width;++x)
                source[(size_t)y*1280U+x]^=(uint16_t)(0x101U+step*17U);
        const unsigned selected=queue.selected;
        slot=tab5_frame_queue_begin(&queue,step+1U);assert(slot>=0&&(unsigned)slot!=selected);
        for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)
            memcpy(saved+(size_t)i*pixels,frames[i],pixels*sizeof(uint16_t));
        if(step==9U){
            /* Scroll→region: the selected frame remains authoritative while
             * all ordinary replay histories were deliberately invalidated. */
            memset(history,0,sizeof(history));
            tab5_ui_scroll_tag_commit(&tags[selected],source,1280U,&state);
        }
        if(step==12U)history[slot].source=NULL;
        const bool replay=history[slot].source==source&&history[slot].stride==1280U;
        const platform_display_rgb565_region_t previous=replay&&history[slot].damage.width?
            history[slot].damage:current;
        platform_display_rgb565_region_t region,mapped;
        assert(platform_display_layout_tab5_damage(&current,replay?&previous:NULL,&region,&mapped));
        const bool clone=tab5_ui_region_clone_eligible(source,1280U,&current,&region,replay,
            &history[selected],&tags[selected],(uintptr_t)frames[selected],(uintptr_t)frames[slot],
            true,true,true);
        if(step==9U)assert(clone);
        if(step==12U)assert(clone);
        if(step==17U)assert(!clone); /* Exact source handoff rejects physical clone. */
        history[slot].source=NULL;tags[slot].valid=false;
        if(clone){
            dma_rectangle(frames[selected],frames[slot],physical_whole,physical_whole);
            /* Even after successful DMA, source ownership remains until region
             * PPA completes. No selected/third buffer mutation is permitted. */
            assert(queue.source_busy&&!tab5_frame_queue_publish(&queue,step+1U));
            rotate_rectangle(source,frames[slot],current);
            ++clones;
            if(step==12U){
                /* Simulated joined PPA failure: scribble a partial target, then
                 * reconstruct fully from the authoritative ordinary source. */
                frames[slot][0]=0U;rotate_rectangle(source,frames[slot],whole);
            }
        }else{rotate_rectangle(source,frames[slot],region);if(replay)++replays;}
        tab5_frame_queue_source_complete(&queue);assert(tab5_frame_queue_publish(&queue,step+1U));
        tab5_frame_history_commit(history,(unsigned)slot,source,1280U,current);
        memset(tags,0,sizeof(tags));
        rotate_rectangle(source,reference,whole);
        assert(!memcmp(frames[queue.selected],reference,pixels*sizeof(uint16_t)));
        for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)
            if(i!=(unsigned)slot)assert(!memcmp(frames[i],saved+(size_t)i*pixels,pixels*sizeof(uint16_t)));
    }
    assert(clones>5U&&replays>5U);
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)free(frames[i]);
    free(logical[0]);free(logical[1]);free(reference);free(saved);
}
static void stationary_patch_contract_test(void)
{
    uint16_t source[2]={0};
    platform_display_ui_scroll_t old=state_for(358U);
    old.previous_offset=0;old.current_offset=0;
    tab5_ui_scroll_tag_t selected={0},target={0};
    tab5_ui_scroll_tag_commit(&selected,source,1280U,&old);
    tab5_ui_scroll_tag_commit(&target,source,1280U,&old);
    platform_display_ui_scroll_t next=old;
    next.previous_offset=0;next.current_offset=1;next.context=2U;
    tab5_ui_scroll_plan_t plan;
    tab5_ui_scroll_repair_t repair;
    bool needs_repair=false;
    assert(!tab5_ui_scroll_plan(&next,&plan)); /* Changed context, no explicit patch. */
    next.stationary_damage=(platform_display_rgb565_region_t){368,558,288,88};
    assert(tab5_ui_scroll_plan(&next,&plan));
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    assert(!needs_repair&&repair.count==0U);
    assert(tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    assert(tab5_ui_scroll_region_equal(plan.stationary_destination,
        (platform_display_rgb565_region_t){558,624,88,288}));
    assert(plan.physical_damage.x==178U&&plan.physical_damage.width==468U);
    assert(plan.physical_damage.y==20U&&plan.physical_damage.height==996U);
    /* An already-new-context target skips complement repair, but the explicit
     * patch is still part of the planned DMA/PPA work on every transition. */
    tab5_ui_scroll_tag_commit(&target,source,1280U,&next);
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    assert(!needs_repair&&plan.stationary_destination.width==88U);
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    next.previous_context=3U;
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    next.previous_context=old.context;
    const platform_display_rgb565_region_t invalid[]={
        {368,535,288,88}, /* Viewport overlap. */
        {1228,178,32,32}, /* Scrollbar overlap. */
        {1279,558,2,88}, /* Out of logical bounds. */
        {368,719,288,2},{368,558,0,88},{368,558,288,0},
    };
    const tab5_ui_scroll_tag_t saved_selected=selected,saved_target=target;
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
        next.stationary_damage=invalid[i];
        assert(!tab5_ui_scroll_plan(&next,&plan));
        assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
        assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
        assert(!memcmp(&selected,&saved_selected,sizeof(selected)));
        assert(!memcmp(&target,&saved_target,sizeof(target)));
    }
    /* A generic disjoint patch can extend the physical publication rows. */
    next.stationary_damage=(platform_display_rgb565_region_t){1,1,3,5};
    assert(tab5_ui_scroll_plan(&next,&plan));
    assert(plan.physical_damage.y==20U&&plan.physical_damage.height==1259U);
}
static void previous_context_reuse_contract_test(void)
{
    uint16_t source[2]={0},other[2]={0};
    platform_display_ui_scroll_t old=state_for(358U);
    old.current_offset=old.previous_offset=7;
    tab5_ui_scroll_tag_t selected={0},target={0};
    tab5_ui_scroll_tag_commit(&selected,source,1280U,&old);
    tab5_ui_scroll_tag_commit(&target,source,1280U,&old);
    /* The retired target may have a different scroll offset: every viewport
     * pixel and scrollbar pixel will come from this selected frame or PPA. */
    target.state.current_offset=84;
    platform_display_ui_scroll_t next=old;
    next.previous_offset=7;next.current_offset=0;next.context=2U;
    next.stationary_damage=(platform_display_rgb565_region_t){368,558,288,88};
    tab5_ui_scroll_repair_t repair;
    bool needs_repair=true;
    assert(tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    assert(!needs_repair&&repair.count==0U);
    const tab5_ui_scroll_tag_t old_target=target;
    /* A new-context target also skips repair, but is not counted as reuse of
     * the previous context. A third or unknown context always repairs. */
    target.state.context=next.context;
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    assert(!needs_repair);
    target.state.context=3U;
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    assert(needs_repair&&repair.count==4U);
    target=old_target;target.valid=false;
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    assert(needs_repair);
    /* Source handoff and exact stride/geometry mismatches cannot borrow the
     * old-context proof. With a valid selected source they require repair. */
    for(unsigned mismatch=0U;mismatch<4U;++mismatch){
        target=old_target;
        if(mismatch==0U)target.source=other;
        if(mismatch==1U)++target.stride;
        if(mismatch==2U)--target.state.viewport.width;
        if(mismatch==3U)--target.state.scrollbar.width;
        assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
        assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
        assert(needs_repair&&repair.count==4U);
    }
    target=old_target;
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,false));
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,false,&repair,&needs_repair));
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,other,1280U,&next,true,&repair,&needs_repair));
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1281U,&next,true,&repair,&needs_repair));
    --selected.state.current_offset;
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    ++selected.state.current_offset;
    ++selected.state.context;
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    --selected.state.context;
    next.stationary_damage=(platform_display_rgb565_region_t){0};
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    assert(!tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    next.context=next.previous_context;
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    assert(!needs_repair);
    /* A valid zero-valued previous context is allowed; validity is explicit. */
    next.context=2U;next.previous_context=0U;
    next.stationary_damage=(platform_display_rgb565_region_t){368,558,288,88};
    selected.state.context=target.state.context=0U;
    assert(tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    assert(tab5_ui_scroll_composition_plan(&selected,&target,source,1280U,&next,true,&repair,&needs_repair));
    assert(!needs_repair);
    /* Pre-mutation rejection preserves both warm tags. Accepted work revokes
     * the target tag and cannot publish/count until the mandatory patch joins. */
    const tab5_ui_scroll_tag_t saved_selected=selected,saved_target=target;
    tab5_frame_queue_t queue;tab5_frame_queue_init(&queue);
    assert(tab5_frame_queue_begin(&queue,0U)==1);
    tab5_frame_queue_cancel(&queue,false);
    assert(!memcmp(&selected,&saved_selected,sizeof(selected)));
    assert(!memcmp(&target,&saved_target,sizeof(target)));
    assert(tab5_frame_queue_begin(&queue,0U)==1);
    unsigned successful_reuses=0U;
    target.valid=false;
    assert(queue.source_busy&&!tab5_frame_queue_publish(&queue,0U));
    assert(successful_reuses==0U&&!target.valid);
    /* Accepted work retains the surfaces while its callback is missing. */
    assert(queue.writing==1&&queue.selected==0U&&queue.source_busy);
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
    /* Once accepted DMA joins, a later strip/patch failure cancels without a
     * presentation. The conservative error path revokes both context proofs;
     * it cannot count reuse or expose a partly composed framebuffer. */
    tab5_frame_queue_source_complete(&queue);
    selected.valid=false;
    tab5_frame_queue_cancel(&queue,false);
    assert(queue.selected==0U&&queue.writing==-1&&!queue.source_busy);
    assert(successful_reuses==0U&&!selected.valid&&!target.valid);
    /* Independent successful fixture: no new tag/count before every join. */
    selected=saved_selected;target=saved_target;
    tab5_frame_queue_init(&queue);
    assert(tab5_frame_queue_begin(&queue,0U)==1);
    const bool reused=tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true);
    target.valid=false;
    assert(queue.source_busy&&!tab5_frame_queue_publish(&queue,0U));
    assert(successful_reuses==0U&&!target.valid);
    /* Model eventual joined success of all pieces, including the patch. */
    tab5_frame_queue_source_complete(&queue);
    assert(tab5_frame_queue_publish(&queue,0U));
    tab5_ui_scroll_tag_commit(&target,source,1280U,&next);
    if(reused)++successful_reuses;
    assert(successful_reuses==1U&&target.state.context==next.context);
    assert(!tab5_ui_scroll_previous_context_reusable(&target,source,1280U,&next,true));
}
static uint64_t endpoint_context(int32_t offset,int32_t maximum)
{
    return 0x1000U|(offset==0?1U:0U)|(offset==maximum?2U:0U);
}
static uint16_t endpoint_pixel(const platform_display_ui_scroll_t *state,unsigned x,unsigned y)
{
    const platform_display_rgb565_region_t footer={368,558,288,88};
    if(inside(footer,x,y))return (uint16_t)((state->context*197U+x*43U+y*31U)&0xffffU);
    if(inside(state->viewport,x,y)||inside(state->scrollbar,x,y))return pixel(state,x,y);
    return (uint16_t)((x*53U+y*7U+0x4000U)&0xffffU);
}
static void endpoint_render_rectangle(uint16_t *logical,const platform_display_ui_scroll_t *state,
    platform_display_rgb565_region_t region)
{
    for(unsigned y=region.y;y<(unsigned)region.y+region.height;++y)
        for(unsigned x=region.x;x<(unsigned)region.x+region.width;++x)
            logical[(size_t)y*1280U+x]=endpoint_pixel(state,x,y);
}
static void endpoint_pixels_test(int32_t maximum)
{
    uint16_t *logical=malloc(pixels*sizeof(uint16_t)),*reference_source=malloc(pixels*sizeof(uint16_t));
    uint16_t *reference=malloc(pixels*sizeof(uint16_t)),*saved=malloc(TAB5_FRAME_COUNT*pixels*sizeof(uint16_t));
    uint16_t *frames[TAB5_FRAME_COUNT];
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i){frames[i]=calloc(pixels,sizeof(uint16_t));assert(frames[i]);}
    assert(logical&&reference_source&&reference&&saved);
    platform_display_ui_scroll_t state=state_for(358U);
    state.previous_offset=0;state.current_offset=0;
    state.context=endpoint_context(0,maximum);state.previous_context=state.context;
    const platform_display_rgb565_region_t whole={0,0,1280,720},footer={368,558,288,88};
    endpoint_render_rectangle(logical,&state,whole);
    tab5_frame_queue_t queue;tab5_frame_queue_init(&queue);
    tab5_ui_scroll_tag_t tags[TAB5_FRAME_COUNT]={0};
    int slot=tab5_frame_queue_begin(&queue,0U);assert(slot==1);
    rotate_rectangle(logical,frames[slot],whole);
    tab5_frame_queue_source_complete(&queue);assert(tab5_frame_queue_publish(&queue,0U));
    tab5_ui_scroll_tag_commit(&tags[slot],logical,1280U,&state);
    /* Five-entry root has only84px scroll; the long list crosses the same
     * endpoint states with interior odd/even steps in both directions. */
    const int32_t offsets[]={1,6,maximum,maximum-1,maximum-6,0,3,maximum,0,7,maximum-3,0,
        maximum-12,maximum-8,maximum-3,maximum};
    unsigned patched=0U,repaired=0U,without_repair=0U,previous_context_reused=0U,new_context_reused=0U;
    for(unsigned step=0;step<32U;++step){
        state.previous_offset=state.current_offset;state.previous_context=state.context;
        state.current_offset=offsets[step%(sizeof(offsets)/sizeof(offsets[0]))];
        state.context=endpoint_context(state.current_offset,maximum);
        state.stationary_damage=state.context==state.previous_context?
            (platform_display_rgb565_region_t){0}:footer;
        const unsigned selected=queue.selected;
        slot=tab5_frame_queue_begin(&queue,step+1U);assert(slot>=0);
        for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)
            memcpy(saved+(size_t)i*pixels,frames[i],pixels*sizeof(uint16_t));
        tab5_ui_scroll_plan_t plan;
        tab5_ui_scroll_repair_t repair;
        bool needs_repair=false;
        const bool geometry=tab5_ui_scroll_plan(&state,&plan);
        const bool eligible=geometry&&tab5_ui_scroll_composition_plan(&tags[selected],&tags[slot],
            logical,1280U,&state,true,&repair,&needs_repair);
        const bool reused_previous=eligible&&tab5_ui_scroll_previous_context_reusable(&tags[slot],
            logical,1280U,&state,true);
        const bool reused_new=eligible&&!needs_repair&&tab5_ui_scroll_patch_present(&state)&&
            tab5_ui_scroll_tag_matches(&tags[slot],logical,1280U,&state);
        assert(!reused_previous||(!needs_repair&&!reused_new));
        if(maximum==84)assert(eligible); /* No endpoint-only slow fallback. */
        if(eligible){
            for(size_t i=0;i<pixels;++i)logical[i]=0xbeefU;
            endpoint_render_rectangle(logical,&state,plan.exposed);
            endpoint_render_rectangle(logical,&state,state.scrollbar);
            if(tab5_ui_scroll_patch_present(&state))endpoint_render_rectangle(logical,&state,footer);
            tags[slot].valid=false; /* Accepted mutation revokes its old tag. */
            if(needs_repair){
                for(unsigned i=0;i<repair.count;++i)
                    dma_rectangle(frames[selected],frames[slot],repair.rectangles[i],repair.rectangles[i]);
                ++repaired;
            }else ++without_repair;
            dma_rectangle(frames[selected],frames[slot],plan.interior_source,plan.interior_destination);
            rotate_rectangle(logical,frames[slot],plan.exposed);
            rotate_rectangle(logical,frames[slot],state.scrollbar);
            assert(queue.source_busy&&!tab5_frame_queue_publish(&queue,step+1U));
            assert(!tags[slot].valid&&tags[selected].state.context==state.previous_context);
            if(reused_previous){
                /* Without the mandatory patch this target would still show
                 * its old footer, even though its moving pixels are complete. */
                const size_t footer_pixel=(size_t)(1279U-footer.x)*720U+footer.y;
                assert(frames[slot][footer_pixel]==endpoint_pixel(&tags[selected].state,footer.x,footer.y));
                assert(frames[slot][footer_pixel]!=endpoint_pixel(&state,footer.x,footer.y));
            }
            /* A joined explicit patch is required before tagging or publishing
             * the new context, even when its target already had that context. */
            if(tab5_ui_scroll_patch_present(&state)){
                rotate_rectangle(logical,frames[slot],footer);++patched;
            }
            assert(!tags[slot].valid); /* New context is not committed early. */
        }else{
            endpoint_render_rectangle(logical,&state,whole);
            rotate_rectangle(logical,frames[slot],whole);
        }
        tab5_frame_queue_source_complete(&queue);assert(tab5_frame_queue_publish(&queue,step+1U));
        tab5_ui_scroll_tag_commit(&tags[slot],logical,1280U,&state);
        if(reused_previous)++previous_context_reused;
        if(reused_new)++new_context_reused;
        endpoint_render_rectangle(reference_source,&state,whole);rotate_rectangle(reference_source,reference,whole);
        assert(!memcmp(frames[queue.selected],reference,pixels*sizeof(uint16_t)));
        for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)
            if(i!=(unsigned)slot)assert(!memcmp(frames[i],saved+(size_t)i*pixels,pixels*sizeof(uint16_t)));
    }
    assert(patched>0U&&repaired>0U&&without_repair>0U&&previous_context_reused>0U);
    if(maximum==84)assert(new_context_reused>0U);
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)free(frames[i]);
    free(logical);free(reference_source);free(reference);free(saved);
}
int main(void)
{
    geometry_test();
    repair_geometry_test();
    rejection_and_ownership_test();
    triple_buffer_pixels_test(448U);
    triple_buffer_pixels_test(358U);
    region_clone_eligibility_test();
    region_clone_pixels_test();
    stationary_patch_contract_test();
    previous_context_reuse_contract_test();
    endpoint_pixels_test(84);
    endpoint_pixels_test(1000);
    puts("Tab5 physical scroll geometry, ownership and triple-buffer pixels passed");
    return 0;
}
