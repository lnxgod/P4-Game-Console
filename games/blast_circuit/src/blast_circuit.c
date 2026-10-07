// SPDX-License-Identifier: MIT
#include "blast_circuit_internal.h"
#include <string.h>

static bool game_start(p4_game_context_t *context)
{
    if(!context || !context->state || context->state_bytes!=sizeof(bc_state_t)) return false;
    bc_state_t *s=context->state;
    memset(s,0,sizeof(*s));
    s->humans=1U; s->host=true; s->audio.noise=0xc1ac017U;
    s->seed_counter=0xb1a57c17U;
    bc_levels_load(context,s);
    bc_select_level(s,0U);
    s->world.phase=BC_TITLE;
    (void)bc_network_begin(context,s);
    return true;
}
void bc_restart(bc_state_t *s)
{
    s->seed_counter=s->seed_counter*1664525U+1013904223U+s->visual_ms;
    bc_round(&s->world,s->seed_counter,true);
    bc_level_apply(&s->world,&s->level);bc_cue(s,BC_S_COUNT);
    memset(s->bomb_pressed,0,sizeof(s->bomb_pressed));
    memset(s->direction,0,sizeof(s->direction));
    memset(s->death_ms,0,sizeof(s->death_ms));
    memset(s->face,0,sizeof(s->face));
    memset(s->particles,0,sizeof(s->particles));
    memset(s->debris_ms,0,sizeof(s->debris_ms));
    s->accumulator=0U;s->banner_ms=0U;s->move_buffer_ms=0U;
}
static uint8_t direction(uint32_t held)
{
    // One axis per tick; opposite pairs cancel without an input-order bias.
    if((held & (P4_BUTTON_UP|P4_BUTTON_DOWN))==P4_BUTTON_UP) return P4_BUTTON_UP;
    if((held & (P4_BUTTON_UP|P4_BUTTON_DOWN))==P4_BUTTON_DOWN) return P4_BUTTON_DOWN;
    if((held & (P4_BUTTON_LEFT|P4_BUTTON_RIGHT))==P4_BUTTON_LEFT) return P4_BUTTON_LEFT;
    if((held & (P4_BUTTON_LEFT|P4_BUTTON_RIGHT))==P4_BUTTON_RIGHT) return P4_BUTTON_RIGHT;
    return 0U;
}
static bool touch_confirm(const p4_game_input_t *input, uint8_t phase)
{
    if(!input->touch_valid) return false;
    for(unsigned i=0;i<input->touch_count && i<5U;++i) {
        const int x=(int)input->touches[i].x*768/320;
        const int y=(int)input->touches[i].y*480/200;
        if(phase==BC_TITLE && x>=314 && x<672 && y>=352 && y<400) return true;
        if(x<164 || x>=604) continue;
        if(phase==BC_PAUSED && y>=218 && y<276) return true;
        if(phase==BC_MATCH && y>=280 && y<326) return true;
        if(phase==BC_LOST && y>=222 && y<282) return true;
    }
    return false;
}
static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,uint32_t elapsed)
{
    if(!context || !context->state || !input) return P4_GAME_ERROR;
    bc_state_t *s=context->state;
    if(input->pressed & P4_BUTTON_BACK) return P4_GAME_EXIT_TO_LAUNCHER;
    if(elapsed>100U) elapsed=100U;
    s->held=input->held;
    if(input->touch_valid && input->touch_count) s->touch_seen=true;
    bc_visual_step(s,elapsed);
    bc_save_poll(context,s);
    s->notice_ms=s->notice_ms>elapsed?(uint16_t)(s->notice_ms-elapsed):0U;
    const bool fresh_touch=!s->touch_down && input->touch_valid && input->touch_count;
    const bool menu=bc_menu_update(context,s,input,fresh_touch,elapsed);
    if(!menu && (input->pressed & P4_BUTTON_B)) {
        s->audio.muted=!s->audio.muted;
        if(s->audio.muted) p4_game_stop_audio(context);
    }
    const bool confirm=(input->pressed & (P4_BUTTON_A|P4_BUTTON_START))!=0U ||
        (fresh_touch && touch_confirm(input,s->world.phase));
    s->touch_down=input->touch_valid && input->touch_count>0U;
    bool transition=menu;
    if(!menu && s->testing && s->world.phase>=BC_READY && s->world.phase<=BC_MATCH && (input->pressed&P4_BUTTON_START)) {
        bc_round(&s->world,s->seed_counter,true);bc_level_apply(&s->world,&s->level);s->world.phase=BC_EDITOR;
        memset(s->debris_ms,0,sizeof(s->debris_ms));memset(s->death_ms,0,sizeof(s->death_ms));
        memset(s->particles,0,sizeof(s->particles));memset(s->face,0,sizeof(s->face));
        s->shake_ms=0U;s->banner_ms=0U;s->accumulator=0U;transition=true;bc_cue(s,BC_S_PAUSE);
    } else if(!menu && s->world.phase==BC_LOST && confirm) {
        s->network=false;s->host=true;s->humans=1U;s->local_slot=0U;
        bc_restart(s);transition=true;
    } else if(!menu && s->world.phase==BC_TITLE && confirm && (!s->network || s->host)) {
        bc_select_level(s,s->selected);bc_cue(s,BC_S_CONFIRM);transition=true;
    } else if(!menu && s->world.phase==BC_MATCH && (!s->network || s->host)) {
        if(input->pressed & P4_BUTTON_START) {bc_select_level(s,s->selected);transition=true;}
        else if(confirm) {bc_start_battle(s);transition=true;}
    }
    else if(!s->network && s->world.phase==BC_PAUSED && confirm) {
        s->world.phase=BC_PLAY;bc_cue(s,BC_S_CONFIRM);transition=true;
    } else if(!s->network && s->world.phase==BC_PLAY && (input->pressed & P4_BUTTON_START)) {
        s->world.phase=BC_PAUSED;
        bc_cue(s,BC_S_PAUSE);s->accumulator=0U;
        memset(s->bomb_pressed,0,sizeof(s->bomb_pressed)); transition=true;
    }
    s->move_buffer_ms=s->move_buffer_ms>elapsed?(uint16_t)(s->move_buffer_ms-elapsed):0U;
    const uint8_t tapped=direction(input->pressed);
    if(tapped) {s->move_buffer=tapped;s->move_buffer_ms=150U;}
    const uint8_t dir=transition?0U:s->move_buffer_ms?s->move_buffer:direction(input->held);
    const bool bomb=!transition && (input->pressed & P4_BUTTON_A) && s->world.phase==BC_PLAY;
    if(s->network) (void)bc_network_poll(context,s,dir,bomb,elapsed);
    else {
        s->direction[0]=dir;
        s->bomb_pressed[0]=s->bomb_pressed[0] || bomb;
    }
    if(s->world.phase>=BC_READY && s->world.phase<=BC_MATCH) {
        const unsigned speed=!s->network && !s->world.players[0].alive &&
            s->world.phase==BC_PLAY && (input->held & P4_BUTTON_A)?8U:1U;
        s->accumulator+=elapsed*speed;
        if(!s->network || s->host) {
            while(s->accumulator>=BC_TICK_MS) {
                s->accumulator-=BC_TICK_MS;
                for(unsigned i=s->humans;i<4U;++i)
                    s->direction[i]=bc_bot(&s->world,i,&s->bomb_pressed[i]);
                const bc_world_t previous=s->world;
                s->events|=bc_step(&s->world,s->direction,s->bomb_pressed);
                if(previous.round!=s->world.round) bc_level_apply(&s->world,&s->level);
                memset(s->bomb_pressed,0,sizeof(s->bomb_pressed));
                bc_effects(s,&previous);
                if(previous.players[s->local_slot].x!=s->world.players[s->local_slot].x ||
                   previous.players[s->local_slot].y!=s->world.players[s->local_slot].y)
                    s->move_buffer_ms=0U;
            }
        } else if(s->accumulator>100U) s->accumulator=100U;
    }
    bc_network_publish(context,s);
    bc_audio_update(context,s,elapsed);
    return P4_GAME_CONTINUE;
}
static void game_stop(p4_game_context_t *context)
{ p4_game_stop_audio(context); }

const p4_game_descriptor_t p4_blast_circuit_game={
    .api_version=P4_GAME_API_VERSION,
    .launcher_id=119U,.id="org.p4console.blast-circuit",
    .title="Blast Circuit",.subtitle="Four-player bomb battles",
    .accent_rgb565=0x5f79,
    .required_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_VIDEO_HIGH_RES,
    .optional_capabilities=P4_GAME_CAP_AUDIO_TONE|P4_GAME_CAP_AUDIO_STREAM|
        P4_GAME_CAP_MULTIPLAYER_SESSION|P4_GAME_CAP_SAVE,
    .state_bytes=sizeof(bc_state_t),.start=game_start,.update=game_update,
    .render=bc_render,.stop=game_stop,
};
