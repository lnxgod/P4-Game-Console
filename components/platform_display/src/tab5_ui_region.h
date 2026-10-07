// SPDX-License-Identifier: MIT
#ifndef TAB5_UI_REGION_H
#define TAB5_UI_REGION_H

#include "tab5_frame_queue.h"
#include "tab5_ui_scroll.h"

/* Copying the latest selected physical image is cheaper than reconstructing
 * a missing or large retired history. This is only an ordinary authoritative
 * source operation: every logical pixel changed since selection is in damage.
 * Full bases stay cache-aligned/disjoint and both slots retain DMA-only cache
 * proof, so the private copy needs no framebuffer cache preparation. */
static inline bool tab5_ui_region_clone_eligible(const uint16_t *source,size_t stride,
    const platform_display_rgb565_region_t *damage,
    const platform_display_rgb565_region_t *replayed,bool replay_available,
    const tab5_frame_history_t *selected_history,const tab5_ui_scroll_tag_t *selected_scroll,
    uintptr_t physical_source,uintptr_t physical_destination,
    bool helper_available,bool selected_dma_clean,bool destination_dma_clean)
{
    const size_t frame_bytes=(size_t)1280U*720U*sizeof(uint16_t);
    if(!source||stride<1280U||!helper_available||!selected_dma_clean||!destination_dma_clean||
        !tab5_ui_scroll_region_valid(damage)||!tab5_ui_scroll_region_valid(replayed)||
        (damage->width==1280U&&damage->height==720U)||!physical_source||!physical_destination||
        physical_source%64U||physical_destination%64U||
        physical_source>UINTPTR_MAX-frame_bytes||physical_destination>UINTPTR_MAX-frame_bytes||
        (physical_source<physical_destination+frame_bytes&&
         physical_destination<physical_source+frame_bytes))return false;
    const bool source_matches_history=selected_history&&selected_history->source==source&&
        selected_history->stride==stride;
    const bool source_matches_scroll=selected_scroll&&selected_scroll->valid&&
        selected_scroll->source==source&&selected_scroll->stride==stride;
    if(!source_matches_history&&!source_matches_scroll)return false;
    const uint32_t replay_pixels=(uint32_t)replayed->width*replayed->height;
    const uint32_t current_pixels=(uint32_t)damage->width*damage->height;
    /* Equal current/replayed geometry has no historical work to avoid. */
    return !replay_available||(replay_pixels>1280U*720U/2U&&replay_pixels>current_pixels);
}

#endif
