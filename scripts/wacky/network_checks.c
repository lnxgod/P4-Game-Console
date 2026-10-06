/* SPDX-License-Identifier: MIT */
#include "p4/game.h"
#include "network.h"
#include "port_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const p4_game_descriptor_t p4_wacky_probe_game;
extern const WwNet *ww_p4_probe_network(const void *);
extern WwRace *ww_p4_probe_race(void *);
extern const WwRenderer *ww_p4_probe_renderer(const void *);
/* Place a stopped car on an actual driveable checkpoint from the shareware
 * map, then let the real host physics/lap update evaluate the crossing. */
static void checkpoint(void *state, unsigned slot, int wanted) {
    WwNet *n=(WwNet *)ww_p4_probe_network(state);WwRace *r=ww_p4_probe_race(state);
    const WwRenderer *renderer=ww_p4_probe_renderer(state);
    for(uint16_t y=0x410;y<0xc00;y+=32)for(uint16_t x=0x410;x<0xc00;x+=32) {
        int16_t position;WwTrackSurfaceSample surface;
        assert(ww_track_position_sample(&r->track,x,y,&position));
        if(position!=wanted || !ww_track_surface_sample(&r->track,x,y,&surface) ||
           surface.sub_37afc_value==3 || surface.sub_37afc_value==10)continue;
        WwNetPlayer *p=&n->players[slot];p->motion=(WwPlayerMotion){0};p->buttons=0;p->jump_ticks=0;
        assert(ww_physics_offset_point(renderer->trig_data,WW_TRIG_BYTES,x,y,960,124,
            &p->motion.camera_x,&p->motion.camera_y));
        assert(ww_net_step(n,r,renderer));return;
    }
    fprintf(stderr,"No driveable checkpoint %d\n",wanted);abort();
}
typedef struct Endpoint {
    struct Endpoint *peer;
    p4_game_multiplayer_status_t status;
    p4_game_multiplayer_message_t queue[32],last;
    unsigned count, sent, dropped, receive_calls;
    bool reject, drop;
} Endpoint;
static bool read_status(void *c,p4_game_multiplayer_status_t *s){*s=((Endpoint*)c)->status;return true;}
static bool send_packet(void *c,const uint8_t *b,size_t n) {
    Endpoint *e=c;assert(n<=64);++e->sent;
    if(e->reject)return false;
    e->last=(p4_game_multiplayer_message_t){.sequence=e->sent,.player_slot=e->status.local_player_slot,.bytes=(uint8_t)n};
    memcpy(e->last.data,b,n);
    if(e->drop || e->sent%13==0){++e->dropped;return true;}
    if(e->peer->count==32)return false;
    e->peer->queue[e->peer->count++]=e->last;return true;
}
static bool receive_packet(void *c,p4_game_multiplayer_message_t *m) {
    Endpoint *e=c;++e->receive_calls;if(!e->count)return false;
    *m=e->queue[0];--e->count;memmove(e->queue,e->queue+1,e->count*sizeof(*m));return true;
}
static void update(p4_game_context_t *c,uint32_t held,uint32_t dt) {
    p4_game_input_t in={.held=held};assert(p4_wacky_probe_game.update(c,&in,dt)==P4_GAME_CONTINUE);
}
int main(void) {
    FILE *f=fopen(WW_DATA_PATH,"rb");assert(f);uint8_t *data=malloc(4398948);
    assert(data && fread(data,1,4398948,f)==4398948);fclose(f);
    Endpoint e[2]={0};p4_game_context_t c[2]={0};p4_game_services_t s[2]={0};
    p4_game_multiplayer_profile_t profile={.schema=1,.style=P4_GAME_MULTIPLAYER_STYLE_REALTIME,
        .min_players=2,.max_players=4,.tick_rate_hz=12,.message_bytes=64,.protocol=2};
    uint16_t *pixels=calloc(128000,2);assert(pixels);
    p4_game_surface_t surf[2]={{.pixels=pixels,.width=320,.height=200,.stride_pixels=320},
        {.pixels=pixels+64000,.width=320,.height=200,.stride_pixels=320}};
    for(unsigned i=0;i<2;++i) {
        e[i].peer=&e[1-i];e[i].status=(p4_game_multiplayer_status_t){.generation=7+i,.session_seed=123456789,
            .role=i?P4_GAME_MULTIPLAYER_ROLE_CLIENT:P4_GAME_MULTIPLAYER_ROLE_HOST,
            .state=P4_GAME_MULTIPLAYER_CONNECTED,.local_player_slot=(uint8_t)i,.player_count=2};
        s[i]=(p4_game_services_t){.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_STORAGE|P4_GAME_CAP_MULTIPLAYER_SESSION,
            .resource_data=data,.resource_bytes=4398948,.resource_format_version=1,.multiplayer_context=&e[i],
            .multiplayer_profile=&profile,.multiplayer_read_status=read_status,.multiplayer_send=send_packet,.multiplayer_receive=receive_packet};
        c[i]=(p4_game_context_t){.state=calloc(1,p4_wacky_probe_game.state_bytes),.state_bytes=p4_wacky_probe_game.state_bytes,.services=&s[i]};
        assert(c[i].state && p4_wacky_probe_game.start(&c[i]));
    }
    const WwNet *host=ww_p4_probe_network(c[0].state),*client=ww_p4_probe_network(c[1].state);
    uint16_t initial[2]={host->players[0].motion.camera_y,host->players[1].motion.camera_y};
    for(unsigned frame=0;frame<1800;++frame) {
        update(&c[1],P4_BUTTON_A|(frame%400<60?P4_BUTTON_RIGHT:0),16);
        update(&c[0],P4_BUTTON_A|(frame%500<50?P4_BUTTON_LEFT:0),16);
        if(frame%5==0)assert(p4_wacky_probe_game.render(&c[0],&surf[0]));
        assert(p4_wacky_probe_game.render(&c[1],&surf[1]));
        if(frame==300) {
            for(unsigned view=0;view<2;++view) {
                assert(p4_wacky_probe_game.render(&c[view],&surf[view]));
                FILE *shot=fopen(view?"wacky-client.rgb565":"wacky-host.rgb565","wb");assert(shot);
                assert(fwrite(surf[view].pixels,2,64000,shot)==64000);fclose(shot);
            }
        }
        assert(ww_p4_probe_race(c[0].state)->displayed_speed==host->players[0].motion.velocity*2);
        assert(ww_p4_probe_race(c[1].state)->displayed_speed==client->players[1].motion.velocity*2);
        assert(!host->ended && !client->ended);
        assert(client->tick<=host->tick && host->tick-client->tick<=3);
    }
    assert(host->players[0].motion.camera_y!=initial[0] && host->players[1].motion.camera_y!=initial[1]);
    assert(host->tick>300 && e[0].dropped && e[1].dropped);
    /* Drain and deliver a complete authoritative snapshot without a new step. */
    e[1].count=0;ww_net_publish((WwNet *)host,&c[0]);if(!e[1].count)ww_net_publish((WwNet *)host,&c[0]);
    update(&c[1],0,0);
    for(unsigned i=0;i<2;++i) {
        assert(client->players[i].motion.camera_x==host->players[i].motion.camera_x);
        assert(client->players[i].motion.camera_y==host->players[i].motion.camera_y);
        assert(client->players[i].lap.course_progress==host->players[i].lap.course_progress);
    }
    uint32_t revision=client->revision;
    /* Bad lengths, sender, version, generation, coordinates and stale packets. */
    for(unsigned fault=0;fault<6;++fault) {
        p4_game_multiplayer_message_t bad=e[0].last;
        if(fault==0)bad.bytes=3;
        if(fault==1)bad.player_slot=1;
        if(fault==2)bad.data[2]=99;
        if(fault==3)bad.data[4]^=1;
        if(fault==4){bad.data[8]+=1;bad.data[24]=255;bad.data[25]=255;}
        e[1].queue[e[1].count++]=bad;update(&c[1],0,0);assert(client->revision==revision);
    }
    /* Receive draining is bounded even with a full corrupt queue. */
    for(unsigned i=0;i<32;++i)e[1].queue[i]=(p4_game_multiplayer_message_t){.sequence=1,.bytes=24};
    e[1].count=32;e[1].receive_calls=0;update(&c[1],0,0);assert(e[1].receive_calls==8);e[1].count=0;
    /* Three real map circuits produce authoritative places; reversing back
     * across the line cannot manufacture another lap. */
    WwNet *mutable_host=(WwNet *)host;mutable_host->finish_count=0;
    for(unsigned i=0;i<2;++i)ww_lap_state_reset(&mutable_host->players[i].lap);
    int checkpoints=ww_p4_probe_race(c[0].state)->track.position_count;
    for(unsigned player=0;player<2;++player) {
        checkpoint(c[0].state,player,checkpoints);checkpoint(c[0].state,player,1);
        assert(host->players[player].lap.current_lap==1);
        checkpoint(c[0].state,player,checkpoints);checkpoint(c[0].state,player,1);
        assert(host->players[player].lap.current_lap==1);
        for(unsigned lap=0;lap<3;++lap) {
            for(int pos=2;pos<=checkpoints;++pos)checkpoint(c[0].state,player,pos);
            checkpoint(c[0].state,player,1);
        }
        assert(host->players[player].lap.finished && host->players[player].lap.finish_place==player+1);
    }
    e[1].count=0;ww_net_publish(mutable_host,&c[0]);if(!e[1].count)ww_net_publish(mutable_host,&c[0]);update(&c[1],0,0);
    assert(client->players[0].lap.finish_place==1 && client->players[1].lap.finish_place==2);
    /* Queue failure cannot block or replay acceleration forever. */
    e[0].reject=e[1].reject=true;e[0].count=e[1].count=0;
    for(unsigned i=0;i<40;++i){update(&c[0],P4_BUTTON_A,16);update(&c[1],P4_BUTTON_A,16);}
    assert(host->players[1].buttons==0);uint32_t frozen_tick=host->tick;
    for(unsigned i=0;i<200;++i){update(&c[0],P4_BUTTON_A,16);update(&c[1],P4_BUTTON_A,16);}
    assert(host->ended && client->ended && host->tick==frozen_tick);
    for(unsigned i=0;i<2;++i) {
        p4_game_input_t back={.pressed=P4_BUTTON_BACK};assert(p4_wacky_probe_game.update(&c[i],&back,16)==P4_GAME_EXIT_TO_LAUNCHER);
        p4_wacky_probe_game.stop(&c[i]);e[i].reject=false;e[i].count=0;
        assert(p4_wacky_probe_game.start(&c[i]));
        ++e[i].status.generation;update(&c[i],P4_BUTTON_A,16);assert(ww_p4_probe_network(c[i].state)->ended);
        p4_wacky_probe_game.stop(&c[i]);
        --e[i].status.generation;e[i].status.state=P4_GAME_MULTIPLAYER_PEER_LEFT;
        assert(!p4_wacky_probe_game.start(&c[i]));
        p4_wacky_probe_game.stop(&c[i]);free(c[i].state);
    }
    assert(ww_p4_heap_live()==0);free(data);free(pixels);
    puts("PASS: two real P4 game instances, independent controls/views, atomic snapshots, dropped/full queues, malformed/stale data, bounded draining, timeout, generation change, Back and leak-free relaunch");
}
