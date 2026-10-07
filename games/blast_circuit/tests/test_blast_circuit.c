// SPDX-License-Identifier: MIT
#include "blast_circuit_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);++failures;} } while(0)
static const uint8_t no_dirs[4]={0};
static const bool no_bombs[4]={false};
static void empty_arena(bc_world_t *w)
{
    bc_round(w,42U,true);w->phase=BC_PLAY;
    for(unsigned i=0;i<BC_CELLS;++i) if(w->tile[i]!=BC_WALL) w->tile[i]=BC_FLOOR;
}
static void rules(void)
{
    bc_world_t w={0};bc_round(&w,42U,true);
    CHECK(BC_PLAYERS==4 && BC_W==17 && BC_H==13);
    for(unsigned i=0;i<4U;++i) {
        const bc_player_t *p=&w.players[i];
        CHECK(p->alive && w.tile[p->y*BC_W+p->x]==BC_FLOOR);
    }
    for(unsigned i=0;i<60U;++i) (void)bc_step(&w,no_dirs,no_bombs);
    CHECK(w.phase==BC_PLAY);
    const uint8_t wall[4]={P4_BUTTON_UP,0,0,0};
    (void)bc_step(&w,wall,no_bombs); CHECK(w.players[0].y==1U);
    CHECK(bc_place(&w,0U));CHECK(!bc_place(&w,0U));CHECK(!bc_place(&w,4U));
    empty_arena(&w);
    w.players[0].x=7U;w.players[0].y=5U;
    w.bombs[0]=(bc_bomb_t){1,1,1,50,3};
    w.bombs[1]=(bc_bomb_t){3,1,1,1,3};
    w.bombs[2]=(bc_bomb_t){5,1,2,50,3};
    w.tile[1*BC_W+7]=BC_CRATE;
    const uint8_t events=bc_step(&w,no_dirs,no_bombs);
    CHECK(events & BC_EVENT_BLAST);
    CHECK(!w.bombs[0].fuse && !w.bombs[1].fuse && !w.bombs[2].fuse);
    CHECK(w.fire[1*BC_W+1] && w.fire[1*BC_W+6]);
    CHECK(w.tile[1*BC_W+7]!=BC_CRATE && !w.fire[1*BC_W+8]);
    CHECK(!w.fire[2*BC_W+2]);
    empty_arena(&w);w.players[1].alive=0;w.players[2].alive=0;w.players[3].alive=0;
    (void)bc_step(&w,no_dirs,no_bombs);CHECK(w.phase==BC_ROUND && w.winner==0 && w.score[0]==1);
    for(unsigned i=0;i<60U;++i) (void)bc_step(&w,no_dirs,no_bombs);
    CHECK(w.phase==BC_READY && w.round==2 && w.score[0]==1 && w.players[3].alive);
    empty_arena(&w);w.score[0]=2U;
    w.players[1].alive=0U;w.players[2].alive=0U;w.players[3].alive=0U;
    (void)bc_step(&w,no_dirs,no_bombs);
    CHECK(w.phase==BC_MATCH && w.score[0]==3U && w.winner==0U);
    empty_arena(&w);w.time_left=1U;
    (void)bc_step(&w,no_dirs,no_bombs);CHECK(w.phase==BC_ROUND && w.winner==255U);
    empty_arena(&w);w.players[0].capacity=3U;
    w.tile[BC_W+2]=BC_SPEED;
    const uint8_t right[4]={P4_BUTTON_RIGHT,0,0,0};
    (void)bc_step(&w,right,no_bombs);CHECK(w.players[0].speed && w.players[0].cooldown<=2U);
    uint8_t data[BC_SNAPSHOT];bc_encode(&w,data);bc_world_t decoded;
    CHECK(bc_decode(&decoded,data));
    const bc_world_t before=decoded;data[16]=255U;
    CHECK(!bc_decode(&decoded,data));CHECK(memcmp(&before,&decoded,sizeof(before))==0);
    empty_arena(&w);w.players[0].x=5;w.players[0].y=5;
    w.bombs[0]=(bc_bomb_t){5,5,0,1,2};
    (void)bc_step(&w,no_dirs,no_bombs);CHECK(!w.players[0].alive);
}
static void bots_and_roundtrips(void)
{
    unsigned blasts=0U,finished=0U;
    for(unsigned seed=1U;seed<=16U;++seed) {
        bc_world_t w={0};bc_round(&w,seed*991U,true);
        bc_level_t level;bc_level_preset(&level,seed%BC_THEMES,seed*991U);bc_level_apply(&w,&level);
        for(unsigned tick=0;tick<5000U;++tick) {
            uint8_t dirs[4];bool bombs[4];
            for(unsigned i=0;i<4U;++i) dirs[i]=bc_bot(&w,i,&bombs[i]);
            const uint8_t previous_round=w.round;
            const uint8_t event=bc_step(&w,dirs,bombs);
            if(w.round!=previous_round) bc_level_apply(&w,&level);
            if(event & BC_EVENT_BLAST) ++blasts;
            if(event & BC_EVENT_WIN) ++finished;
            uint8_t data[BC_SNAPSHOT],encoded[BC_SNAPSHOT];bc_world_t copy;
            bc_encode(&w,data);
            if(!bc_decode(&copy,data)) {CHECK(false);break;}
            bc_encode(&copy,encoded);CHECK(memcmp(data,encoded,sizeof(data))==0);
            if(w.phase==BC_MATCH) break;
        }
    }
    CHECK(blasts>100U && finished>=16U);
    printf("Bot soak: %u explosion ticks, %u completed rounds across 16 seeds\n",blasts,finished);
}

enum {QUEUE=128};
typedef struct link link_t;
typedef struct {
    link_t *link;unsigned slot,head,count;uint32_t sequence,generation;
    p4_game_multiplayer_state_t status;
    p4_game_multiplayer_message_t queue[QUEUE];
    bool fail_send;unsigned send_count,fail_after;
} endpoint_t;
struct link {unsigned players;endpoint_t endpoints[4];};
static bool status_read(void *ctx,p4_game_multiplayer_status_t *out)
{
    const endpoint_t *e=ctx;
    *out=(p4_game_multiplayer_status_t){.generation=e->generation,.session_seed=42U,.state=e->status,
        .role=e->slot?P4_GAME_MULTIPLAYER_ROLE_CLIENT:P4_GAME_MULTIPLAYER_ROLE_HOST,
        .local_player_slot=(uint8_t)e->slot,.player_count=(uint8_t)e->link->players};return true;
}
static void enqueue(endpoint_t *e,const p4_game_multiplayer_message_t *m)
{ CHECK(e->count<QUEUE);if(e->count<QUEUE) {e->queue[(e->head+e->count)%QUEUE]=*m;++e->count;} }
static bool send_packet(void *ctx,const uint8_t *data,size_t bytes)
{
    endpoint_t *e=ctx;
    if(e->fail_send || (e->fail_after && ++e->send_count>e->fail_after)) return false;
    CHECK(bytes<=60U);
    p4_game_multiplayer_message_t m={.sequence=++e->sequence,.player_slot=(uint8_t)e->slot,.bytes=(uint8_t)bytes};
    memcpy(m.data,data,bytes);
    for(unsigned i=0;i<e->link->players;++i) {
        if(i==e->slot || (e->slot && i)) continue;
        if(e->link->endpoints[i].count>=QUEUE) return false;
        enqueue(&e->link->endpoints[i],&m);
    }
    return true;
}
static bool receive_packet(void *ctx,p4_game_multiplayer_message_t *out)
{
    endpoint_t *e=ctx;if(!e->count) return false;
    *out=e->queue[e->head];e->head=(e->head+1U)%QUEUE;--e->count;return true;
}
static p4_game_multiplayer_profile_t profile={.schema=1U,.style=P4_GAME_MULTIPLAYER_STYLE_REALTIME,
    .min_players=2U,.max_players=4U,.tick_rate_hz=20U,.message_bytes=60U,.protocol=BC_PROTOCOL};
static void update(p4_game_instance_t *g,uint32_t held,uint32_t pressed,unsigned elapsed)
{
    const p4_game_input_t in={.held=held,.pressed=pressed};
    CHECK(p4_game_instance_update(g,&in,elapsed)==P4_GAME_CONTINUE);
}
static void network(unsigned count)
{
    link_t *link=calloc(1,sizeof(*link));CHECK(link!=NULL);if(!link) return;
    bc_state_t state[4];p4_game_instance_t game[4];memset(game,0,sizeof(game));
    link->players=count;
    for(unsigned i=0;i<count;++i) {
        endpoint_t *e=&link->endpoints[i];e->link=link;e->slot=i;e->generation=7U;
        e->status=P4_GAME_MULTIPLAYER_CONNECTED;
        const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_VIDEO_HIGH_RES|P4_GAME_CAP_MULTIPLAYER_SESSION,
            .multiplayer_context=e,.multiplayer_read_status=status_read,.multiplayer_send=send_packet,
            .multiplayer_receive=receive_packet,.multiplayer_profile=&profile};
        CHECK(p4_game_instance_start(&game[i],&p4_blast_circuit_game,&services,&state[i],sizeof(state[i])));
        CHECK(state[i].network && state[i].humans==count);
        p4_game_multiplayer_profile_t old=profile;old.protocol=2U;
        p4_game_services_t previous_services=services;previous_services.multiplayer_profile=&old;
        bc_state_t old_state={0};p4_game_context_t previous_context={.state=&old_state,.state_bytes=sizeof(old_state),.services=&previous_services};
        CHECK(bc_network_begin(&previous_context,&old_state) && old_state.world.phase==BC_LOST);
    }
    CHECK(state[0].world.phase==BC_SELECT);
    // The host may spend time editing; bounded heartbeats keep waiting clients alive.
    for(unsigned frame=0;frame<40U;++frame) {
        update(&game[0],0,0,100U);
        for(unsigned i=1;i<count;++i) update(&game[i],0,0,100U);
    }
    CHECK(state[1].world.phase==BC_WAIT);
    bc_select_level(&state[0],2U);
    for(unsigned y=5;y<=7;y+=2) for(unsigned x=7;x<=9;x+=2) state[0].level.tile[y*BC_W+x]=BC_ARMOR;
    bc_network_transfer(&state[0]);update(&game[0],0,0,100U);
    endpoint_t *receiver=&link->endpoints[1];
    CHECK(receiver->count==BC_LEVEL_PARTS);
    p4_game_multiplayer_message_t level_chunks[BC_LEVEL_PARTS];
    for(unsigned part=0;part<BC_LEVEL_PARTS;++part) CHECK(receive_packet(receiver,&level_chunks[part]));
    // A peer cannot author the arena, even if it supplies a valid checksum.
    for(unsigned part=0;part<BC_LEVEL_PARTS;++part) {
        p4_game_multiplayer_message_t fake=level_chunks[part];fake.player_slot=1U;enqueue(receiver,&fake);
    }
    update(&game[1],0,0,50U);CHECK(!state[1].level_ready);
    // Valid framing with one corrupted tile is rejected atomically.
    for(unsigned part=0;part<BC_LEVEL_PARTS;++part) {
        p4_game_multiplayer_message_t bad=level_chunks[part];if(part==2U) bad.data[15]^=1U;enqueue(receiver,&bad);
    }
    const bc_level_t untouched=state[1].level;
    update(&game[1],0,0,50U);CHECK(!state[1].level_ready && memcmp(&state[1].level,&untouched,sizeof(untouched))==0);
    // Reordered complete chunks recover, but no match starts without the last ACK.
    link->endpoints[count-1U].fail_send=true;
    p4_game_multiplayer_message_t wrong_ack={.player_slot=(uint8_t)(count-1U),.bytes=10U,.data={BC_PROTOCOL,4U}};
    for(unsigned byte=0;byte<4U;++byte) {
        wrong_ack.data[2U+byte]=(uint8_t)(state[0].level_id>>(byte*8U));
        wrong_ack.data[6U+byte]=(uint8_t)((state[0].level_hash^1U)>>(byte*8U));
    }
    enqueue(&link->endpoints[0],&wrong_ack);
    for(unsigned part=BC_LEVEL_PARTS;part>0U;--part) enqueue(receiver,&level_chunks[part-1U]);
    update(&game[1],0,0,50U);CHECK(state[1].level_ready && state[1].level.theme==2U);
    link->endpoints[0].fail_after=2U;
    for(unsigned frame=0;frame<10U;++frame) {
        link->endpoints[0].send_count=0U;
        update(&game[0],0,0,100U);
        for(unsigned i=1;i<count;++i) update(&game[i],0,0,100U);
        CHECK(state[0].world.phase==BC_TRANSFER);
    }
    for(unsigned i=1;i<count;++i) CHECK(state[i].level_ready && memcmp(&state[i].level,&state[0].level,sizeof(bc_level_t))==0);
    link->endpoints[count-1U].fail_send=false;
    for(unsigned frame=0;frame<8U;++frame) {
        link->endpoints[0].send_count=0U;
        update(&game[0],0,0,100U);
        for(unsigned i=1;i<count;++i) update(&game[i],0,0,100U);
    }
    CHECK(state[0].world.phase==BC_READY && state[1].received);
    // Two accepted packets per call must eventually deliver a full snapshot.
    CHECK(state[1].revision>0U && state[1].silence_ms<=400U);
    link->endpoints[0].fail_after=0U;
    for(unsigned i=0;i<count;++i) {link->endpoints[i].head=0U;link->endpoints[i].count=0U;}
    bc_select_level(&state[0],0U);
    empty_arena(&state[0].world);
    // Fairness rejects an empty/asymmetric host map without starting a transfer.
    const bc_level_t fair=state[0].level;
    state[0].level.tile[BC_W+4]=BC_FLOOR;
    state[0].world.phase=BC_SELECT;bc_network_transfer(&state[0]);
    CHECK(state[0].world.phase==BC_SELECT);state[0].level=fair;
    // Transfer a valid symmetric starting map; clear only the live test world below.
    bc_network_transfer(&state[0]);
    for(unsigned frame=0;frame<4U;++frame) {
        update(&game[0],0,0,100U);
        for(unsigned i=1;i<count;++i) update(&game[i],0,0,100U);
    }
    CHECK(state[0].world.phase==BC_READY);
    for(unsigned i=1;i<count;++i) CHECK(state[i].level_ready && memcmp(&state[i].level,&state[0].level,sizeof(bc_level_t))==0);
    empty_arena(&state[0].world);
    state[0].world.fire[5*BC_W+7]=BC_EMBER_FIRE;
    // Host publishes the longer ember timer to every real game instance.
    update(&game[0],0,0,100U);
    for(unsigned i=1;i<count;++i) update(&game[i],0,0,50U);
    for(unsigned i=1;i<count;++i) {
        uint8_t a[BC_SNAPSHOT],b[BC_SNAPSHOT];bc_encode(&state[0].world,a);bc_encode(&state[i].world,b);
        CHECK(state[i].received && memcmp(a,b,sizeof(a))==0);
    }
    const unsigned slot=count-1U;
    // Every human slot has its own input and bomb inventory.
    update(&game[slot],P4_BUTTON_A,P4_BUTTON_A,50U);
    update(&game[0],0,0,50U);
    unsigned owned=0U;
    for(unsigned i=0;i<BC_BOMBS;++i) if(state[0].world.bombs[i].fuse && state[0].world.bombs[i].owner==slot) ++owned;
    CHECK(owned==1U);
    const uint16_t accepted=state[0].action[slot];
    const uint32_t last_sequence=state[0].last_sequence[slot];
    p4_game_multiplayer_message_t forged={.sequence=0U,.player_slot=(uint8_t)slot,.bytes=10U,
        .data={BC_PROTOCOL,1U,1U,15U,255U,127U,1U,0U,0U,0U}};
    forged.data[6]=(uint8_t)state[0].level_id;
    enqueue(&link->endpoints[0],&forged);update(&game[0],0,0,50U);
    CHECK(state[0].action[slot]==accepted);
    forged.data[3]=P4_BUTTON_LEFT;
    forged.data[4]=(uint8_t)(accepted+1U);forged.data[5]=0U;
    forged.sequence=last_sequence;
    enqueue(&link->endpoints[0],&forged);update(&game[0],0,0,50U);
    CHECK(state[0].action[slot]==accepted); // Valid intent, replayed transport sequence.
    forged.sequence=last_sequence+1U;forged.data[2]=(uint8_t)(state[0].world.round+1U);
    enqueue(&link->endpoints[0],&forged);update(&game[0],0,0,50U);
    CHECK(state[0].action[slot]==accepted); // Wrong round cannot act on this arena.
    // Only the host can restart a completed match; all clients adopt the new arena.
    state[0].world.phase=BC_MATCH;state[0].world.winner=0U;state[0].world.score[0]=3U;
    for(unsigned i=1U;i<4U;++i) state[0].world.players[i].alive=0U;
    update(&game[0],0,0,100U);
    for(unsigned i=1U;i<count;++i) update(&game[i],0,0,50U);
    update(&game[1],P4_BUTTON_A,P4_BUTTON_A,50U);
    CHECK(state[1].world.phase==BC_MATCH);
    update(&game[0],P4_BUTTON_A,P4_BUTTON_A,100U);
    CHECK(state[0].world.phase==BC_TRANSFER);
    for(unsigned frame=0;frame<4U;++frame) {
        for(unsigned i=1;i<count;++i) update(&game[i],0,0,100U);
        update(&game[0],0,0,100U);
    }
    for(unsigned i=1U;i<count;++i) {
        update(&game[i],0,0,50U);
        CHECK(state[i].world.phase==BC_READY && state[i].world.score[0]==0U && state[i].world.round==1U);
    }
    // Partial snapshots do not expose mixed map/player state; a later complete one heals them.
    endpoint_t *host=&link->endpoints[0];host->fail_after=2U;host->send_count=0U;
    for(unsigned i=1;i<count;++i) {link->endpoints[i].head=0;link->endpoints[i].count=0;}
    const uint32_t revision=state[1].revision;
    update(&game[0],0,0,100U);update(&game[1],0,0,50U);
    CHECK(state[1].revision==revision);
    host->fail_after=0U;
    update(&game[0],0,0,100U);
    for(unsigned i=1;i<count;++i) update(&game[i],0,0,50U);
    CHECK(state[1].revision>revision);
    // A complete snapshot can arrive in reverse chunk order; peer clients cannot author it.
    for(unsigned i=1;i<count;++i) {link->endpoints[i].head=0U;link->endpoints[i].count=0U;}
    update(&game[0],0,0,100U);
    endpoint_t *client=&link->endpoints[1];
    CHECK(client->count==BC_PARTS);
    p4_game_multiplayer_message_t chunks[BC_PARTS];
    memcpy(chunks,client->queue,sizeof(chunks));client->count=0U;
    const uint32_t before_revision=state[1].revision;
    for(unsigned i=0;i<BC_PARTS;++i) {
        p4_game_multiplayer_message_t fake=chunks[i];fake.player_slot=1U;
        enqueue(client,&fake);
    }
    update(&game[1],0,0,50U);CHECK(state[1].revision==before_revision);
    for(unsigned i=BC_PARTS;i>0U;--i) enqueue(client,&chunks[i-1U]);
    update(&game[1],0,0,50U);CHECK(state[1].revision>before_revision);
    const uint32_t latest=state[1].revision;
    for(unsigned i=0;i<BC_PARTS;++i) enqueue(client,&chunks[i]);
    update(&game[1],0,0,50U);CHECK(state[1].revision==latest);
    // A stream of partial/invalid host traffic must not keep a client alive forever.
    client->count=0U;state[1].silence_ms=2999U;
    update(&game[1],0,0,50U);CHECK(state[1].world.phase==BC_LOST);
    state[1].world.phase=BC_PLAY;state[1].silence_ms=0U;
    // Input silence is neutralized and never replaced by CPU control of a human seat.
    state[0].direction[slot]=P4_BUTTON_LEFT;state[0].input_age[slot]=349U;
    link->endpoints[0].count=0U;
    update(&game[0],0,0,100U);CHECK(state[0].direction[slot]==0U);
    // Generation changes invalidate the session and discard pending actions.
    ++link->endpoints[1].generation;update(&game[1],0,0,50U);
    CHECK(state[1].world.phase==BC_LOST && state[1].received_mask==0U);
    update(&game[1],P4_BUTTON_A,P4_BUTTON_A,50U);
    CHECK(!state[1].network && state[1].humans==1U && state[1].world.phase==BC_READY);
    link->endpoints[0].status=P4_GAME_MULTIPLAYER_PEER_LEFT;
    update(&game[0],0,0,50U);CHECK(state[0].world.phase==BC_LOST);
    link->endpoints[0].status=P4_GAME_MULTIPLAYER_CONNECTED;
    state[0].world.phase=BC_SELECT;bc_network_transfer(&state[0]);state[0].transfer_ms=9999U;
    update(&game[0],0,0,100U);CHECK(state[0].world.phase==BC_LOST);
    for(unsigned i=0;i<count;++i) p4_game_instance_stop(&game[i]);
    free(link);
    printf("%u-instance session: input, snapshots, partial failure and disconnect passed\n",count);
}
static void malformed_snapshots(void)
{
    bc_world_t original={0};empty_arena(&original);
    uint8_t invalid[BC_SNAPSHOT];bc_encode(&original,invalid);
    const unsigned cells=16U+BC_PLAYERS*9U+BC_BOMBS*5U;
    invalid[cells+2U*(BC_W+1U)+1U]=BC_EMBER_FIRE+1U;
    bc_world_t retained=original;CHECK(!bc_decode(&retained,invalid));
    CHECK(memcmp(&retained,&original,sizeof(original))==0);
    uint32_t random=19U;
    for(unsigned i=0;i<8000U;++i) {
        uint8_t data[BC_SNAPSHOT];bc_encode(&original,data);
        random=random*1664525U+1013904223U;
        const unsigned where=random%BC_SNAPSHOT;
        random=random*1664525U+1013904223U;
        data[where]=(uint8_t)(random>>24U);
        bc_world_t candidate=original;
        if(!bc_decode(&candidate,data)) CHECK(memcmp(&candidate,&original,sizeof(original))==0);
        else {
            uint8_t again[BC_SNAPSHOT];bc_encode(&candidate,again);
            CHECK(memcmp(data,again,sizeof(data))==0);
        }
    }
}
static void capture(const p4_game_surface_t *surface,unsigned mode,const char *tag)
{
    const char *dir=getenv("BC_CAPTURE_DIR");if(!dir) return;
    char path[1024];const int n=snprintf(path,sizeof(path),"%s/%s-%u.ppm",dir,tag,mode?768U:320U);
    CHECK(n>0 && (size_t)n<sizeof(path));if(n<=0 || (size_t)n>=sizeof(path)) return;
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);if(!file) return;
    fprintf(file,"P6\n%u %u\n255\n",surface->width,surface->height);
    for(unsigned y=0;y<surface->height;++y) for(unsigned x=0;x<surface->width;++x) {
        const unsigned c=surface->pixels[(size_t)y*surface->stride_pixels+x];
        const uint8_t rgb[3]={(uint8_t)(((c>>11U)&31U)*255U/31U),
                             (uint8_t)(((c>>5U)&63U)*255U/63U),(uint8_t)((c&31U)*255U/31U)};
        CHECK(fwrite(rgb,1U,sizeof(rgb),file)==sizeof(rgb));
    }
    CHECK(fclose(file)==0);
}
static unsigned pcm_frames,stops;
static int peak;
static bool pcm(void *ctx,const int16_t *samples,size_t n)
{
    (void)ctx;CHECK(n>0 && n<=256U);pcm_frames+=(unsigned)n;
    for(size_t i=0;i<n*2U;++i) {int v=samples[i];if(v<0)v=-v;if(v>peak)peak=v;}
    return false; // Deliberately reject every block: game must remain responsive.
}
static void stop_audio(void *ctx) {(void)ctx;++stops;}
static void lifecycle_and_render(void)
{
    bc_state_t state;p4_game_instance_t game={0};
    const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_AUDIO_STREAM|P4_GAME_CAP_VIDEO_HIGH_RES,
        .submit_pcm16_stereo=pcm,.stop_audio=stop_audio};
    // Missing native video must refuse launch rather than quietly downgrade.
    p4_game_services_t legacy_services=services;
    legacy_services.available_capabilities&=~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
    CHECK(!p4_game_instance_start(&game,&p4_blast_circuit_game,&legacy_services,&state,sizeof(state)));
    CHECK(p4_game_instance_start(&game,&p4_blast_circuit_game,&services,&state,sizeof(state)));
    CHECK(state.world.phase==BC_TITLE && state.humans==1U);
    const p4_game_input_t title_touch={.touch_valid=true,.touch_count=1U,.touches={{205U,156U}}};
    CHECK(p4_game_instance_update(&game,&title_touch,50U)==P4_GAME_CONTINUE);
    CHECK(state.world.phase==BC_SELECT);
    update(&game,0,P4_BUTTON_A,50U);
    CHECK(state.world.phase==BC_READY);
    for(unsigned i=0;i<30U;++i) update(&game,0,0,100U);
    CHECK(state.world.phase==BC_PLAY);
    const unsigned x=state.world.players[0].x;
    update(&game,P4_BUTTON_RIGHT,P4_BUTTON_RIGHT,1U);
    update(&game,0,0,50U);CHECK(state.world.players[0].x==x+1U); // Very short tap survives tick boundary.
    update(&game,P4_BUTTON_START,P4_BUTTON_START,50U);
    const uint32_t tick=state.world.tick;
    update(&game,0,0,100U);CHECK(state.world.phase==BC_PAUSED && state.world.tick==tick);
    bc_cue(&state,BC_S_BLAST);bc_cue(&state,BC_S_GLASS);bc_cue(&state,BC_S_CHAMPION);
    update(&game,0,0,50U);
    CHECK(state.audio.effect_left[BC_S_BLAST]>0U && state.audio.effect_left[BC_S_GLASS]>0U && state.audio.effect_left[BC_S_CHAMPION]>0U);
    update(&game,P4_BUTTON_B,P4_BUTTON_B,50U);CHECK(state.audio.muted && stops>0U);
    for(unsigned voice=0;voice<BC_SOUNDS;++voice) CHECK(state.audio.effect_left[voice]==0U);
    const p4_game_input_t resume_touch={.touch_valid=true,.touch_count=1U,.touches={{160U,100U}}};
    CHECK(p4_game_instance_update(&game,&resume_touch,50U)==P4_GAME_CONTINUE);
    CHECK(state.world.phase==BC_PLAY);
    CHECK(peak>100 && peak<32000 && pcm_frames>0U);
    state.world.players[0].alive=0U;
    const uint32_t before_fast_forward=state.world.tick;
    update(&game,P4_BUTTON_A,P4_BUTTON_A,100U);
    CHECK(state.world.tick==before_fast_forward+16U);
    state.world.players[0].alive=1U;
    const bc_state_t render_state=state;
    // The legacy render path remains a clipping/stride compatibility test;
    // maintained Tab5 launch and presentation require the native mode above.
    for(unsigned mode=0;mode<2U;++mode) {
        state=render_state;
        if(mode) game.services.available_capabilities|=P4_GAME_CAP_VIDEO_HIGH_RES;
        else game.services.available_capabilities&=~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
        const unsigned width=mode?768U:320U,height=mode?480U:200U,stride=width+7U;
        const size_t count=(size_t)stride*height+32U;
        uint16_t *buffer=malloc(count*sizeof(*buffer));CHECK(buffer!=NULL);if(!buffer) continue;
        for(size_t i=0;i<count;++i) buffer[i]=0xdead;
        p4_game_surface_t surface={.pixels=buffer+16,.stride_pixels=stride,.width=(uint16_t)width,.height=(uint16_t)height};
        for(unsigned phase=BC_TITLE;phase<=BC_COPY;++phase) {
            state.world.phase=(uint8_t)phase;state.world.winner=3U;state.touch_seen=true;
            state.banner_ms=0U;state.shake_ms=0U;state.world.timer=60U;
            CHECK(p4_game_instance_render(&game,&surface));
            if(phase==BC_TITLE) capture(&surface,mode,"title");
            else if(phase==BC_READY) capture(&surface,mode,"ready");
            else if(phase==BC_PAUSED) capture(&surface,mode,"pause");
            else if(phase==BC_ROUND) capture(&surface,mode,"round");
            else if(phase==BC_MATCH) capture(&surface,mode,"results");
        }
        for(unsigned i=0;i<16U;++i) CHECK(buffer[i]==0xdead && buffer[count-1U-i]==0xdead);
        for(unsigned y=0;y<height;++y) for(unsigned xpad=width;xpad<stride;++xpad)
            CHECK(buffer[16U+(size_t)y*stride+xpad]==0xdead);
        state.world.phase=BC_PLAY;
        state.banner_ms=0U;state.shake_ms=0U;
        CHECK(p4_game_instance_render(&game,&surface));capture(&surface,mode,"arena");
        state.world.tile[5*BC_W+7]=BC_FLOOR;
        for(unsigned life=1U;life<=BC_EMBER_FIRE;++life) {
            state.world.fire[5*BC_W+7]=(uint8_t)life;
            CHECK(p4_game_instance_render(&game,&surface));
        }
        capture(&surface,mode,"embers");
        for(unsigned i=0;i<16U;++i) CHECK(buffer[i]==0xdead && buffer[count-1U-i]==0xdead);
        for(unsigned theme=0;theme<BC_THEMES;++theme) {
            bc_select_level(&state,theme);CHECK(p4_game_instance_render(&game,&surface));
            capture(&surface,mode,theme==0U?"greenhouse":theme==1U?"foundry":"prism");
        }
        bc_editor_enter(&state);state.brush=BC_ARMOR;
        CHECK(p4_game_instance_render(&game,&surface));capture(&surface,mode,"workshop");
        state.brush=BC_FLOOR;
        CHECK(p4_game_instance_render(&game,&surface));capture(&surface,mode,"floor-brush");
        state.world.phase=BC_EDITOR_MENU;
        CHECK(p4_game_instance_render(&game,&surface));capture(&surface,mode,"tools");
        // A deterministic active scene exposes every material and all three
        // upgrade symbols together, without relying on random pickup drops.
        empty_arena(&state.world);state.world.time_left=1800U;
        memset(state.debris_ms,0,sizeof(state.debris_ms));
        memset(state.death_ms,0,sizeof(state.death_ms));
        memset(state.particles,0,sizeof(state.particles));
        state.banner_ms=0U;state.shake_ms=0U;
        static const uint8_t materials[]={BC_CRATE,BC_ARMOR,BC_DAMAGED,BC_GLASS,BC_FUEL,BC_RANGE,BC_EXTRA,BC_SPEED};
        for(unsigned i=0;i<sizeof(materials);++i) state.world.tile[5U*BC_W+3U+i]=materials[i];
        state.world.bombs[0]=(bc_bomb_t){5,7,0,25,2};
        state.world.fire[7U*BC_W+9U]=BC_EMBER_FIRE;
        state.world.fire[7U*BC_W+10U]=BC_FIRE;
        CHECK(p4_game_instance_render(&game,&surface));capture(&surface,mode,"materials");
        for(unsigned i=0;i<16U;++i) CHECK(buffer[i]==0xdead && buffer[count-1U-i]==0xdead);
        for(unsigned y=0;y<height;++y) for(unsigned xpad=width;xpad<stride;++xpad)
            CHECK(buffer[16U+(size_t)y*stride+xpad]==0xdead);
        free(buffer);
    }
    const p4_game_input_t back={.pressed=P4_BUTTON_BACK};
    CHECK(p4_game_instance_update(&game,&back,16U)==P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&game);
}
int main(void)
{
    CHECK(sizeof(bc_state_t)<=P4_GAME_MAX_STATE_BYTES);
    rules();bots_and_roundtrips();malformed_snapshots();network(2U);network(3U);network(4U);lifecycle_and_render();
    printf("Blast Circuit: %u failures; state %zu bytes; snapshot %u bytes / %u chunks\n",failures,sizeof(bc_state_t),BC_SNAPSHOT,BC_PARTS);
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
