// SPDX-License-Identifier: MIT
#include "blast_circuit_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned failures;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FEATURE FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
static void level_rules(void)
{
    for(unsigned theme=0;theme<BC_THEMES;++theme) for(unsigned seed=0;seed<100U;++seed) {
        bc_level_t level;bc_level_preset(&level,theme,seed*991U);
        CHECK(bc_level_validate(&level)==BC_LEVEL_OK);
        unsigned armor=0U,walls=0U,wood=0U,fuel=0U;
        for(int y=0;y<BC_H;++y) for(int x=0;x<BC_W;++x) {
            const uint8_t tile=level.tile[y*BC_W+x];
            if(bc_border(x,y)) CHECK(tile==BC_WALL);
            else if(tile==BC_WALL) ++walls;
            if(bc_spawn_safe(x,y)) CHECK(tile==BC_FLOOR);
            if(tile==BC_ARMOR) ++armor;
            if(tile==BC_CRATE) ++wood;
            if(tile==BC_FUEL) ++fuel;
            CHECK(tile==level.tile[y*BC_W+BC_W-1-x]);
            CHECK(tile==level.tile[(BC_H-1-y)*BC_W+x]);
        }
        CHECK(armor>0U && walls>0U && wood>0U && fuel>0U);
        CHECK(bc_level_match_validate(&level)==BC_LEVEL_OK);
        for(unsigned from=0;from<4U;++from) for(unsigned to=from+1U;to<4U;++to) {
            const unsigned blocks=bc_level_route_blocks(&level,from,to);
            CHECK(blocks>=3U && blocks<=5U);
            CHECK(blocks==((from==0U && to==3U) || (from==1U && to==2U)?3U:4U));
        }
        uint8_t bytes[BC_LEVEL_BYTES];bc_level_encode(&level,bytes);
        bc_level_t decoded;CHECK(bc_level_decode(&decoded,bytes));CHECK(memcmp(&level,&decoded,sizeof(level))==0);
    }
    bc_state_t s={0};bc_select_level(&s,2U);bc_editor_enter(&s);
    const bc_level_t original=s.level;s.brush=BC_ARMOR;s.mirror=true;
    bc_editor_paint(&s,7,5);
    CHECK(s.level.tile[5*BC_W+7]==BC_ARMOR && s.level.tile[7*BC_W+9]==BC_ARMOR);
    CHECK(s.dirty && s.undo_valid);
    const bc_level_t edited=s.level;bc_editor_undo(&s);CHECK(memcmp(&s.level,&original,sizeof(original))==0);
    bc_editor_undo(&s);CHECK(memcmp(&s.level,&edited,sizeof(edited))==0);
    bc_editor_paint(&s,1,1);CHECK(memcmp(&s.level,&edited,sizeof(edited))==0 && s.notice==1U);
    bc_editor_paint(&s,0,6);CHECK(memcmp(&s.level,&edited,sizeof(edited))==0);
    s.mirror=false;s.brush=BC_GLASS;bc_editor_paint(&s,7,5);
    CHECK(s.level.tile[5*BC_W+7]==BC_GLASS && s.level.tile[7*BC_W+9]==BC_ARMOR);
    bc_level_t invalid=s.level;invalid.tile[BC_W+1]=BC_CRATE;
    CHECK(bc_level_validate(&invalid)==BC_LEVEL_SPAWN);
    invalid=s.level;for(int y=1;y<BC_H-1;++y) invalid.tile[y*BC_W+8]=BC_WALL;
    CHECK(bc_level_validate(&invalid)==BC_LEVEL_DISCONNECTED);
    invalid=s.level;invalid.tile[0]=BC_FLOOR;CHECK(bc_level_validate(&invalid)==BC_LEVEL_BAD_TILE);
    uint8_t encoded[BC_LEVEL_BYTES];bc_level_encode(&s.level,encoded);
    uint32_t random=89U;
    for(unsigned i=0;i<5000U;++i) {
        uint8_t bytes[BC_LEVEL_BYTES];memcpy(bytes,encoded,sizeof(bytes));
        random=random*1664525U+1013904223U;const unsigned at=random%BC_LEVEL_BYTES;
        random=random*1664525U+1013904223U;bytes[at]=(uint8_t)(random>>24U);
        bc_level_t candidate=s.level;
        if(!bc_level_decode(&candidate,bytes)) CHECK(memcmp(&candidate,&s.level,sizeof(candidate))==0);
        else CHECK(bc_level_validate(&candidate)==BC_LEVEL_OK);
    }
    bc_world_t w={0};bc_round(&w,9U,true);bc_level_apply(&w,&s.level);w.phase=BC_PLAY;
    // Two separate explosions dent then destroy metal; each ray stops at it.
    for(unsigned i=0;i<BC_CELLS;++i) if(w.tile[i]!=BC_WALL) w.tile[i]=BC_FLOOR;
    w.tile[5*BC_W+7]=BC_ARMOR;w.bombs[0]=(bc_bomb_t){6,5,0,1,3};
    const uint8_t directions[4]={0};const bool bombs[4]={false};
    (void)bc_step(&w,directions,bombs);
    CHECK(w.tile[5*BC_W+7]==BC_DAMAGED && !w.fire[5*BC_W+8]);
    memset(w.fire,0,sizeof(w.fire));w.bombs[0]=(bc_bomb_t){6,5,0,1,3};
    (void)bc_step(&w,directions,bombs);CHECK(!bc_destructible(w.tile[5*BC_W+7]) && !w.fire[5*BC_W+8]);
    w.tile[5*BC_W+7]=BC_GLASS;w.bombs[0]=(bc_bomb_t){6,5,0,1,3};
    (void)bc_step(&w,directions,bombs);CHECK(!bc_destructible(w.tile[5*BC_W+7]));
    // A custom floor at a classic pillar coordinate is really walkable.
    w.phase=BC_PLAY;memset(w.fire,0,sizeof(w.fire));memset(w.bombs,0,sizeof(w.bombs));
    w.players[0]=(bc_player_t){.x=7,.y=6,.from_x=7,.from_y=6,.alive=1,.range=2,.capacity=1};
    w.tile[6*BC_W+8]=BC_FLOOR;
    const uint8_t right[4]={P4_BUTTON_RIGHT,0,0,0};(void)bc_step(&w,right,bombs);
    CHECK(w.players[0].x==8U);
}
static void reinforced_and_permanent(void)
{
    const uint8_t directions[4]={0};const bool bombs[4]={false};
    for(unsigned theme=0;theme<BC_THEMES;++theme) {
        bc_world_t w={0};bc_round(&w,42U,true);
        bc_level_t level;bc_level_preset(&level,theme,42U);
        if(theme==0U) CHECK(memcmp(w.tile,level.tile,BC_CELLS)==0);
        bc_level_apply(&w,&level);w.phase=BC_PLAY;
        const unsigned at=3U*BC_W+4U;
        CHECK(w.tile[at]==BC_ARMOR);
        w.tile[3U*BC_W+3U]=BC_FLOOR;w.tile[3U*BC_W+5U]=BC_FLOOR;
        w.bombs[0]=(bc_bomb_t){3,3,0,1,2};
        (void)bc_step(&w,directions,bombs);
        CHECK(w.tile[at]==BC_DAMAGED && !w.fire[3U*BC_W+5U]);
        for(unsigned tick=0;tick<BC_FIRE+1U;++tick) (void)bc_step(&w,directions,bombs);
        CHECK(w.tile[at]==BC_DAMAGED && bc_solid(w.tile[at]));
        w.bombs[0]=(bc_bomb_t){3,3,0,1,2};
        (void)bc_step(&w,directions,bombs);
        CHECK(!bc_solid(w.tile[at]) && !w.fire[3U*BC_W+5U]);
        for(unsigned tick=0;tick<BC_FIRE+1U;++tick) (void)bc_step(&w,directions,bombs);
        w.players[0]=(bc_player_t){.x=3,.y=3,.from_x=3,.from_y=3,.alive=1,.range=2,.capacity=1};
        const uint8_t right[4]={P4_BUTTON_RIGHT,0,0,0};
        (void)bc_step(&w,right,bombs);CHECK(w.players[0].x==4U);
        // A true interior pillar is permanent, and blocks fire behind it.
        const unsigned px=theme==2U?4U:2U,pillar=2U*BC_W+px;
        CHECK(w.tile[pillar]==BC_WALL);
        w.players[0].x=1;w.players[0].y=1;
        for(unsigned hit=0;hit<3U;++hit) {
            memset(w.fire,0,sizeof(w.fire));w.tile[pillar-1U]=BC_FLOOR;
            w.bombs[0]=(bc_bomb_t){(uint8_t)(px-1U),2,0,1,2};
            (void)bc_step(&w,directions,bombs);
            CHECK(w.tile[pillar]==BC_WALL && !w.fire[pillar] && !w.fire[pillar+1U]);
        }
    }
}
static void ember_rules(void)
{
    bc_world_t w={0};bc_round(&w,42U,true);w.phase=BC_PLAY;
    for(unsigned i=0;i<BC_CELLS;++i) if(w.tile[i]!=BC_WALL) w.tile[i]=BC_FLOOR;
    const unsigned at=3U*BC_W+4U;
    w.tile[at]=BC_FUEL;w.bombs[0]=(bc_bomb_t){3,3,0,1,2};
    const uint8_t dirs[4]={0};const bool bombs[4]={false};
    (void)bc_step(&w,dirs,bombs);
    CHECK(w.tile[at]==BC_FLOOR && w.fire[at]==BC_EMBER_FIRE && !w.fire[at+1U]);
    for(unsigned tick=0;tick<BC_EMBER_FIRE;++tick) {
        uint8_t encoded[BC_SNAPSHOT];bc_world_t copy;
        bc_encode(&w,encoded);CHECK(bc_decode(&copy,encoded));
        CHECK(copy.fire[at]==w.fire[at]);
        if(tick==BC_FIRE) CHECK(!w.fire[at-1U] && w.fire[at]==20U);
        if(tick==BC_FIRE+1U) {
            // A new passing blast must never shorten the lingering fire.
            bc_world_t another=w;another.bombs[0]=(bc_bomb_t){3,3,0,1,2};
            (void)bc_step(&another,dirs,bombs);CHECK(another.fire[at]>=w.fire[at]-1U);
            // Crossing a still-burning former crate is lethal.
            another=w;another.players[0]=(bc_player_t){.x=3,.y=3,.from_x=3,.from_y=3,.alive=1,.range=2,.capacity=1};
            const uint8_t right[4]={P4_BUTTON_RIGHT,0,0,0};
            (void)bc_step(&another,right,bombs);CHECK(!another.players[0].alive);
            another=w;another.players[1]=(bc_player_t){.x=3,.y=3,.from_x=3,.from_y=3,.alive=1,.range=2,.capacity=1};
            another.tile[at+1U]=BC_RANGE;
            bool place=false;CHECK(bc_bot(&another,1U,&place)!=P4_BUTTON_RIGHT);
        }
        (void)bc_step(&w,dirs,bombs);
    }
    CHECK(!w.fire[at]);
    w.players[0]=(bc_player_t){.x=3,.y=3,.from_x=3,.from_y=3,.alive=1,.range=2,.capacity=1};
    const uint8_t right[4]={P4_BUTTON_RIGHT,0,0,0};
    (void)bc_step(&w,right,bombs);CHECK(w.players[0].alive && w.players[0].x==4U);
}
static void fairness_errors(void)
{
    bc_state_t s={0};bc_select_level(&s,0U);
    bc_level_t level=s.level;level.tile[BC_W+4]=BC_FLOOR;
    CHECK(bc_level_match_validate(&level)==BC_LEVEL_ASYMMETRIC);
    s.level=level;bc_start_battle(&s);CHECK(s.world.phase==BC_SELECT && s.notice==10U);
    bc_level_preset(&level,0U,42U);
    for(unsigned i=0;i<BC_CELLS;++i) if(bc_destructible(level.tile[i])) level.tile[i]=BC_FLOOR;
    CHECK(bc_level_match_validate(&level)==BC_LEVEL_SEPARATION);
    s.level=level;bc_start_battle(&s);CHECK(s.world.phase==BC_SELECT && s.notice==11U);
    for(int y=1;y<BC_H-1;++y) for(int x=1;x<BC_W-1;++x)
        if(!bc_spawn_safe(x,y) && level.tile[y*BC_W+x]!=BC_WALL) level.tile[y*BC_W+x]=BC_CRATE;
    CHECK(bc_level_match_validate(&level)==BC_LEVEL_SEPARATION);
    // Symmetric edits that isolate a room must still fail connectivity first.
    bc_level_preset(&level,0U,42U);
    for(int y=1;y<BC_H-1;++y) level.tile[y*BC_W+8]=BC_WALL;
    CHECK(bc_level_match_validate(&level)==BC_LEVEL_DISCONNECTED);
    CHECK(bc_level_route_blocks(&level,0U,2U)==BC_CELLS);
}
static void editor_strokes(void)
{
    bc_state_t s={0};bc_select_level(&s,0U);bc_editor_enter(&s);s.mirror=false;s.brush=BC_GLASS;
    p4_game_context_t context={.state=&s,.state_bytes=sizeof(s)};
    const bc_level_t original=s.level;
    p4_game_input_t in={.touch_valid=true,.touch_count=1,.touches={{148,90}}};
    CHECK(bc_menu_update(&context,&s,&in,true,16U)); // Cell 7,5.
    in.touches[0].x=195;CHECK(bc_menu_update(&context,&s,&in,false,16U)); // Jump to 11,5.
    for(unsigned x=7U;x<=11U;++x) CHECK(s.level.tile[5U*BC_W+x]==BC_GLASS);
    const bc_level_t stroke=s.level;
    bc_editor_undo(&s);CHECK(memcmp(&s.level,&original,sizeof(original))==0);
    bc_editor_undo(&s);CHECK(memcmp(&s.level,&stroke,sizeof(stroke))==0);
    // Painting the same material must not consume the previous undo.
    bc_editor_paint(&s,11,5);bc_editor_undo(&s);CHECK(memcmp(&s.level,&original,sizeof(original))==0);
    in=(p4_game_input_t){0};(void)bc_menu_update(&context,&s,&in,false,16U);
    CHECK(s.last_painted==-1 && !s.stroke_changed);
    // A diagonal stroke also fills intermediate cells and undoes as one edit.
    in=(p4_game_input_t){.touch_valid=true,.touch_count=1,.touches={{113,79}}}; // Cell 4,4.
    (void)bc_menu_update(&context,&s,&in,true,16U);
    in.touches[0].x=160;in.touches[0].y=125; // Cell 8,8.
    (void)bc_menu_update(&context,&s,&in,false,16U);
    for(unsigned i=4U;i<=8U;++i) CHECK(s.level.tile[i*BC_W+i]==BC_GLASS);
    bc_editor_undo(&s);CHECK(memcmp(&s.level,&original,sizeof(original))==0);
    // Crossing protected spawn lanes leaves their cells untouched.
    in=(p4_game_input_t){.touch_valid=true,.touch_count=1,.touches={{78,44}}}; // Cell 1,1.
    (void)bc_menu_update(&context,&s,&in,true,16U);
    in.touches[0].x=125;(void)bc_menu_update(&context,&s,&in,false,16U); // Cell 5,1.
    for(unsigned x=1U;x<=3U;++x) CHECK(s.level.tile[BC_W+x]==BC_FLOOR);
    CHECK(s.level.tile[BC_W+4]==BC_GLASS && s.level.tile[BC_W+5]==BC_GLASS);
    bc_editor_undo(&s);CHECK(memcmp(&s.level,&original,sizeof(original))==0);
}
typedef struct {uint8_t bytes[BC_SAVE_BYTES];uint32_t expected,sequence;unsigned requests;p4_game_save_status_t status;bool reject;} saves_t;
static bool queue_save(void *ctx,const char *slot,uint32_t schema,uint32_t expected,const uint8_t *bytes,size_t count,p4_game_save_ticket_t *ticket)
{
    saves_t *save=ctx;CHECK(strcmp(slot,"AUTO")==0 && schema==1U && count==BC_SAVE_BYTES);
    ++save->requests;if(save->reject) return false;
    memcpy(save->bytes,bytes,count);save->expected=expected;save->status=P4_GAME_SAVE_QUEUED;*ticket=17U;return true;
}
static bool save_status(void *ctx,p4_game_save_ticket_t ticket,p4_game_save_status_t *status,uint32_t *sequence)
{ saves_t *save=ctx;CHECK(ticket==17U);*status=save->status;*sequence=save->sequence;return true; }
static void save_and_editor(void)
{
    saves_t save={0};bc_state_t state;p4_game_instance_t game={0};
    p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_SAVE,
        .save_context=&save,.queue_save=queue_save,.read_save_status=save_status,.save_sequence=9U};
    CHECK(p4_game_instance_start(&game,&p4_blast_circuit_game,&services,&state,sizeof(state)));
    CHECK(state.save_sequence==9U && state.dirty);
    p4_game_input_t in={.pressed=P4_BUTTON_A};
    CHECK(p4_game_instance_update(&game,&in,16U)==P4_GAME_CONTINUE && state.world.phase==BC_SELECT);
    in.pressed=P4_BUTTON_B;(void)p4_game_instance_update(&game,&in,16U);CHECK(state.world.phase==BC_COPY);
    in.pressed=P4_BUTTON_A;(void)p4_game_instance_update(&game,&in,16U);CHECK(state.world.phase==BC_EDITOR);
    // A touch in the overlapping virtual-A region must open tools, not paint.
    const bc_level_t before=state.level;
    in=(p4_game_input_t){.touch_valid=true,.touch_count=1,.touches={{290,155}},.pressed=P4_BUTTON_A};
    (void)p4_game_instance_update(&game,&in,16U);CHECK(state.world.phase==BC_EDITOR_MENU && memcmp(&before,&state.level,sizeof(before))==0);
    in=(p4_game_input_t){0};(void)p4_game_instance_update(&game,&in,16U);
    // Touch Save Levels, then check queued != committed.
    in=(p4_game_input_t){.touch_valid=true,.touch_count=1,.touches={{160,76}}};
    (void)p4_game_instance_update(&game,&in,16U);CHECK(save.requests==1U && state.save_ticket && state.dirty && save.expected==9U);
    bc_levels_save(&game.context,&state);CHECK(save.requests==1U); // One outstanding ticket.
    state.brush=BC_GLASS;bc_editor_paint(&state,7,5);
    save.status=P4_GAME_SAVE_COMMITTED;save.sequence=10U;bc_save_poll(&game.context,&state);
    CHECK(state.save_sequence==10U && state.dirty && state.notice==4U);
    bc_levels_save(&game.context,&state);CHECK(save.expected==10U);
    save.status=P4_GAME_SAVE_COMMITTED;save.sequence=11U;bc_save_poll(&game.context,&state);
    CHECK(!state.dirty && state.notice==3U);
    // Launch snapshots round-trip all three slots atomically.
    bc_state_t restored={0};p4_game_context_t context={.state=&restored,.state_bytes=sizeof(restored),.services=&services};
    services.save_data=save.bytes;services.save_bytes=BC_SAVE_BYTES;services.save_schema_version=1U;services.save_sequence=11U;
    bc_levels_load(&context,&restored);CHECK(!restored.dirty && memcmp(restored.custom,state.custom,sizeof(state.custom))==0);
    save.bytes[30]^=1U;bc_levels_load(&context,&restored);CHECK(restored.dirty && restored.notice==9U && restored.save_sequence==11U);
    save.bytes[30]^=1U;
    state.brush=BC_RANGE;bc_editor_paint(&state,7,5);bc_levels_save(&game.context,&state);
    save.status=P4_GAME_SAVE_CONFLICT;bc_save_poll(&game.context,&state);
    CHECK(state.dirty && state.notice==6U && state.save_sequence==11U);
    save.reject=true;bc_levels_save(&game.context,&state);CHECK(!state.save_ticket && state.dirty);
    game.services.available_capabilities&=~(uint32_t)P4_GAME_CAP_SAVE;
    bc_levels_save(&game.context,&state);CHECK(state.notice==7U && state.dirty);
    // Every round restores the exact custom map and retains match scoring.
    bc_restart(&state);state.world.phase=BC_ROUND;state.world.timer=1U;state.world.score[2]=1U;
    in=(p4_game_input_t){0};(void)p4_game_instance_update(&game,&in,50U);
    CHECK(state.world.phase==BC_READY && state.world.score[2]==1U && state.world.theme==state.level.theme);
    CHECK(memcmp(state.world.tile,state.level.tile,BC_CELLS)==0);
    // Play-test returns to the exact editable layout, with no live hazards.
    state.testing=true;bc_restart(&state);state.world.phase=BC_PLAY;
    state.world.tile[5*BC_W+7]=BC_FLOOR;state.world.fire[5*BC_W+7]=8U;
    in=(p4_game_input_t){.pressed=P4_BUTTON_START};(void)p4_game_instance_update(&game,&in,16U);
    CHECK(state.world.phase==BC_EDITOR && state.world.tile[5*BC_W+7]==state.level.tile[5*BC_W+7] && !state.world.fire[5*BC_W+7]);
    in=(p4_game_input_t){.pressed=P4_BUTTON_BACK};CHECK(p4_game_instance_update(&game,&in,16U)==P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&game);
}
typedef struct {uint32_t hash;unsigned frames,peak;} audio_probe_t;
static bool audio_probe(void *ctx,const int16_t *data,size_t count)
{
    audio_probe_t *p=ctx;CHECK(count>0U && count<=256U);
    for(size_t i=0;i<count*2U;++i) {
        const int sample=data[i];const unsigned value=(unsigned)(sample<0?-sample:sample);
        if(value>p->peak) p->peak=value;
        p->hash=(p->hash^(uint16_t)data[i])*16777619U;
    }
    p->frames+=(unsigned)count;return false; // Exercise graceful FIFO rejection too.
}
static void sound_bank(void)
{
    uint32_t hashes[BC_SOUNDS];
    for(unsigned cue=0;cue<BC_SOUNDS;++cue) {
        bc_state_t s={0};s.world.phase=BC_PAUSED;s.audio.noise=17U;
        audio_probe_t probe={.hash=2166136261U};
        const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_AUDIO_STREAM,.audio_context=&probe,.submit_pcm16_stereo=audio_probe};
        p4_game_context_t context={.state=&s,.state_bytes=sizeof(s),.services=&services};
        bc_cue(&s,(bc_sound_t)cue);
        for(unsigned i=0;i<20U;++i) bc_audio_update(&context,&s,100U);
        CHECK(probe.frames==32000U && probe.peak>500U && probe.peak<30000U && !s.audio.effect_left[cue]);
        hashes[cue]=probe.hash;for(unsigned j=0;j<cue;++j) CHECK(hashes[j]!=hashes[cue]);
    }
    printf("%u distinct sound cues decoded at 16 kHz with bounded, non-clipping output\n",BC_SOUNDS);
}
int main(void)
{
    level_rules();reinforced_and_permanent();ember_rules();fairness_errors();editor_strokes();save_and_editor();sound_bank();
    printf("Arena/editor/save/audio: %u failures\n",failures);return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
