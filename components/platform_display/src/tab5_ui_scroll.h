// SPDX-License-Identifier: MIT
#ifndef TAB5_UI_SCROLL_H
#define TAB5_UI_SCROLL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "platform/display_scroll.h"

typedef struct {
    const uint16_t *source;
    size_t stride;
    platform_display_ui_scroll_t state;
    bool valid;
} tab5_ui_scroll_tag_t;

typedef struct {
    platform_display_rgb565_region_t interior_source;
    platform_display_rgb565_region_t interior_destination;
    platform_display_rgb565_region_t exposed;
    platform_display_rgb565_region_t exposed_destination;
    platform_display_rgb565_region_t scrollbar_destination;
    platform_display_rgb565_region_t stationary_destination;
    platform_display_rgb565_region_t physical_damage;
} tab5_ui_scroll_plan_t;
typedef struct {
    platform_display_rgb565_region_t rectangles[4];
    unsigned count;
} tab5_ui_scroll_repair_t;

static inline bool tab5_ui_scroll_region_valid(const platform_display_rgb565_region_t *r)
{
    return r&&r->width&&r->height&&r->x<1280U&&r->y<720U&&
        r->width<=1280U-r->x&&r->height<=720U-r->y;
}
static inline bool tab5_ui_scroll_region_equal(platform_display_rgb565_region_t a,
    platform_display_rgb565_region_t b)
{
    return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;
}
static inline bool tab5_ui_scroll_region_disjoint(platform_display_rgb565_region_t a,
    platform_display_rgb565_region_t b)
{
    return (uint32_t)a.x+a.width<=b.x||(uint32_t)b.x+b.width<=a.x||
        (uint32_t)a.y+a.height<=b.y||(uint32_t)b.y+b.height<=a.y;
}
static inline bool tab5_ui_scroll_state_valid(const platform_display_ui_scroll_t *state)
{
    if(!state||!tab5_ui_scroll_region_valid(&state->viewport)||
        !tab5_ui_scroll_region_valid(&state->scrollbar))return false;
    const platform_display_rgb565_region_t a=state->viewport,b=state->scrollbar;
    return tab5_ui_scroll_region_disjoint(a,b);
}
static inline bool tab5_ui_scroll_patch_present(const platform_display_ui_scroll_t *state)
{
    return state&&state->stationary_damage.width&&state->stationary_damage.height;
}
static inline bool tab5_ui_scroll_transition_valid(const platform_display_ui_scroll_t *state)
{
    if(!tab5_ui_scroll_state_valid(state))return false;
    const platform_display_rgb565_region_t patch=state->stationary_damage;
    if(!patch.width&&!patch.height)return state->previous_context==state->context;
    return tab5_ui_scroll_region_valid(&patch)&&
        tab5_ui_scroll_region_disjoint(patch,state->viewport)&&
        tab5_ui_scroll_region_disjoint(patch,state->scrollbar);
}
static inline platform_display_rgb565_region_t tab5_ui_scroll_rotate(
    platform_display_rgb565_region_t r)
{
    return (platform_display_rgb565_region_t){r.y,(uint16_t)(1280U-r.x-r.width),r.height,r.width};
}
static inline bool tab5_ui_scroll_tag_matches_context(const tab5_ui_scroll_tag_t *tag,
    const uint16_t *source,size_t stride,const platform_display_ui_scroll_t *state,uint64_t context)
{
    return state&&tag&&tag->valid&&tag->source==source&&tag->stride==stride&&
        tag->state.context==context&&
        tab5_ui_scroll_region_equal(tag->state.viewport,state->viewport)&&
        tab5_ui_scroll_region_equal(tag->state.scrollbar,state->scrollbar);
}
static inline bool tab5_ui_scroll_tag_matches(const tab5_ui_scroll_tag_t *tag,
    const uint16_t *source,size_t stride,const platform_display_ui_scroll_t *state)
{
    return state&&tab5_ui_scroll_tag_matches_context(tag,source,stride,state,state->context);
}
static inline void tab5_ui_scroll_tag_commit(tab5_ui_scroll_tag_t *tag,
    const uint16_t *source,size_t stride,const platform_display_ui_scroll_t *state)
{
    if(state)*tag=(tab5_ui_scroll_tag_t){.source=source,.stride=stride,.state=*state,.valid=true};
    else *tag=(tab5_ui_scroll_tag_t){0};
}
static inline bool tab5_ui_scroll_plan(const platform_display_ui_scroll_t *state,
    tab5_ui_scroll_plan_t *plan)
{
    if(!plan||!tab5_ui_scroll_transition_valid(state))return false;
    const int64_t delta=(int64_t)state->current_offset-state->previous_offset;
    const uint64_t distance=(uint64_t)(delta<0?-delta:delta);
    const platform_display_rgb565_region_t viewport=state->viewport;
    if(!distance||distance>=viewport.height)return false;
    const uint16_t shift=(uint16_t)distance;
    const platform_display_rgb565_region_t physical=tab5_ui_scroll_rotate(viewport);
    *plan=(tab5_ui_scroll_plan_t){
        .interior_source=physical,.interior_destination=physical,.exposed=viewport,
        .scrollbar_destination=tab5_ui_scroll_rotate(state->scrollbar),
    };
    plan->interior_source.width=(uint16_t)(physical.width-shift);
    plan->interior_destination.width=plan->interior_source.width;
    if(delta>0){
        plan->interior_source.x=(uint16_t)(physical.x+shift);
        plan->exposed.y=(uint16_t)(viewport.y+viewport.height-shift);
    }else plan->interior_destination.x=(uint16_t)(physical.x+shift);
    plan->exposed.height=shift;
    plan->exposed_destination=tab5_ui_scroll_rotate(plan->exposed);
    const platform_display_rgb565_region_t bar=plan->scrollbar_destination;
    const uint16_t x=physical.x<bar.x?physical.x:bar.x;
    const uint16_t y=physical.y<bar.y?physical.y:bar.y;
    const uint32_t right_a=(uint32_t)physical.x+physical.width,right_b=(uint32_t)bar.x+bar.width;
    const uint32_t bottom_a=(uint32_t)physical.y+physical.height,bottom_b=(uint32_t)bar.y+bar.height;
    plan->physical_damage=(platform_display_rgb565_region_t){x,y,
        (uint16_t)((right_a>right_b?right_a:right_b)-x),
        (uint16_t)((bottom_a>bottom_b?bottom_a:bottom_b)-y)};
    if(tab5_ui_scroll_patch_present(state)){
        plan->stationary_destination=tab5_ui_scroll_rotate(state->stationary_damage);
        const platform_display_rgb565_region_t a=plan->physical_damage,b=plan->stationary_destination;
        const uint16_t union_x=a.x<b.x?a.x:b.x,union_y=a.y<b.y?a.y:b.y;
        const uint32_t union_right_a=(uint32_t)a.x+a.width,union_right_b=(uint32_t)b.x+b.width;
        const uint32_t union_bottom_a=(uint32_t)a.y+a.height,union_bottom_b=(uint32_t)b.y+b.height;
        plan->physical_damage=(platform_display_rgb565_region_t){union_x,union_y,
            (uint16_t)((union_right_a>union_right_b?union_right_a:union_right_b)-union_x),
            (uint16_t)((union_bottom_a>union_bottom_b?union_bottom_a:union_bottom_b)-union_y)};
    }
    return true;
}
static inline bool tab5_ui_scroll_eligible(const tab5_ui_scroll_tag_t *selected,
    const tab5_ui_scroll_tag_t *destination,const uint16_t *source,size_t stride,
    const platform_display_ui_scroll_t *state,bool destination_dma_clean)
{
    return destination_dma_clean&&tab5_ui_scroll_transition_valid(state)&&
        tab5_ui_scroll_tag_matches_context(selected,source,stride,state,state->previous_context)&&
        tab5_ui_scroll_tag_matches(destination,source,stride,state)&&
        selected->state.current_offset==state->previous_offset;
}
/* Outside viewport/bar/patch, previous and new contexts are identical by the
 * explicit-patch contract. The entire moving content is reconstructed, so an
 * older target offset is immaterial. The patch must still be written. */
static inline bool tab5_ui_scroll_previous_context_reusable(const tab5_ui_scroll_tag_t *destination,
    const uint16_t *source,size_t stride,const platform_display_ui_scroll_t *state,
    bool destination_dma_clean)
{
    return destination_dma_clean&&tab5_ui_scroll_transition_valid(state)&&
        state->context!=state->previous_context&&tab5_ui_scroll_patch_present(state)&&
        tab5_ui_scroll_tag_matches_context(destination,source,stride,state,state->previous_context);
}
/* The bounding union must contain only viewport/scrollbar pixels. Otherwise
 * copying just its complement could leave a stationary gap stale. Production
 * Games/Files use a right-adjacent full-height scrollbar, making this exact. */
static inline bool tab5_ui_scroll_repair_plan(const platform_display_ui_scroll_t *state,
    tab5_ui_scroll_repair_t *repair)
{
    if(!repair||!tab5_ui_scroll_state_valid(state))return false;
    const platform_display_rgb565_region_t v=state->viewport,b=state->scrollbar;
    if((uint32_t)v.x+v.width!=b.x||v.y!=b.y||v.height!=b.height)return false;
    const platform_display_rgb565_region_t bounds=tab5_ui_scroll_rotate(
        (platform_display_rgb565_region_t){v.x,v.y,(uint16_t)(v.width+b.width),v.height});
    const uint16_t right=(uint16_t)(bounds.x+bounds.width);
    const uint16_t bottom=(uint16_t)(bounds.y+bounds.height);
    *repair=(tab5_ui_scroll_repair_t){0};
    if(bounds.y)repair->rectangles[repair->count++]=
        (platform_display_rgb565_region_t){0,0,720,bounds.y};
    if(bottom<1280U)repair->rectangles[repair->count++]=
        (platform_display_rgb565_region_t){0,bottom,720,(uint16_t)(1280U-bottom)};
    if(bounds.x)repair->rectangles[repair->count++]=
        (platform_display_rgb565_region_t){0,bounds.y,bounds.x,bounds.height};
    if(right<720U)repair->rectangles[repair->count++]=
        (platform_display_rgb565_region_t){right,bounds.y,(uint16_t)(720U-right),bounds.height};
    return true;
}
/* Selected source/offset and destination cleanliness are always mandatory.
 * A stale/untagged target is safe only if its whole stationary complement can
 * be repaired, while the entire moving viewport/scrollbar is reconstructed. */
static inline bool tab5_ui_scroll_composition_plan(const tab5_ui_scroll_tag_t *selected,
    const tab5_ui_scroll_tag_t *destination,const uint16_t *source,size_t stride,
    const platform_display_ui_scroll_t *state,bool destination_dma_clean,
    tab5_ui_scroll_repair_t *repair,bool *needs_repair)
{
    if(!repair||!needs_repair||!destination_dma_clean||!state||
        !tab5_ui_scroll_transition_valid(state)||
        !tab5_ui_scroll_tag_matches_context(selected,source,stride,state,state->previous_context)||
        selected->state.current_offset!=state->previous_offset)return false;
    *needs_repair=!tab5_ui_scroll_tag_matches(destination,source,stride,state)&&
        !tab5_ui_scroll_previous_context_reusable(destination,source,stride,state,destination_dma_clean);
    *repair=(tab5_ui_scroll_repair_t){0};
    return !*needs_repair||tab5_ui_scroll_repair_plan(state,repair);
}

#endif
