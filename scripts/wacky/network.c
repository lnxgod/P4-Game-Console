/* SPDX-License-Identifier: MIT
 * Two-to-four player racing. Only the host runs physics and lap rules.
 * No racing state is accepted from the client. Two bounded snapshot parts publish atomically.
 */
#include "network.h"
#include <string.h>
enum { LEFT=1, RIGHT=2, GAS=4, BRAKE=8, INPUT_BYTES=25, HEADER=24 };
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8); }
static bool newer(uint32_t a,uint32_t b) { uint32_t d=a-b;return d && d<0x80000000u; }
static uint8_t keys(uint32_t b) {
    return (uint8_t)(((b&P4_BUTTON_LEFT)?LEFT:0)|((b&P4_BUTTON_RIGHT)?RIGHT:0)|
        ((b&(P4_BUTTON_UP|P4_BUTTON_A))?GAS:0)|((b&P4_BUTTON_DOWN)?BRAKE:0));
}
static bool valid_keys(uint8_t b) { return !(b&0xf0) && (b&3)!=3 && (b&12)!=12; }
static void end(WwNet *n) {
    n->ended=true;
    for(unsigned i=0;i<n->count;++i)n->players[i].buttons=0;
}
bool ww_net_start(WwNet *n,p4_game_context_t *ctx,WwRace *race) {
    memset(n,0,sizeof(*n));
    p4_game_multiplayer_status_t st={0};
    if(!ctx->services || !(ctx->services->available_capabilities & P4_GAME_CAP_MULTIPLAYER_SESSION))return true;
    if(!p4_game_multiplayer_read_status(ctx,&st))return false;
    if(st.state==P4_GAME_MULTIPLAYER_OFFLINE)return true;
    p4_game_multiplayer_profile_t p;
    if(st.state!=P4_GAME_MULTIPLAYER_CONNECTED || (st.player_count<2 || st.player_count>4) ||
       (st.role!=P4_GAME_MULTIPLAYER_ROLE_HOST && st.role!=P4_GAME_MULTIPLAYER_ROLE_CLIENT) ||
       (st.local_player_slot>=st.player_count || (st.role==P4_GAME_MULTIPLAYER_ROLE_HOST)!=(st.local_player_slot==0)) ||
       !p4_game_multiplayer_read_profile(ctx,&p) || p.protocol!=2 ||
       p.style!=P4_GAME_MULTIPLAYER_STYLE_REALTIME || p.message_bytes!=WW_NET_BYTES ||
       p.min_players!=2 || p.max_players!=4 || p.tick_rate_hz!=12)return false;
    n->active=true;n->host=st.role==P4_GAME_MULTIPLAYER_ROLE_HOST;n->slot=st.local_player_slot;
    n->count=st.player_count;n->track=race->selection.track_number;n->heard_mask=(uint8_t)(1u<<n->slot);
    n->generation=st.generation;n->seed=st.session_seed;n->send_ms=88;
    for(unsigned i=0;i<n->count;++i) {
        n->players[i].motion=race->player;
        n->players[i].motion.camera_x=race->initial.grid_x[i];
        n->players[i].motion.camera_y=(uint16_t)(race->initial.grid_y[i]-0x7c);
        n->players[i].rank=(uint8_t)(i+1);n->players[i].vehicle=(uint8_t)i;
    }
    /* Sprint mode has no item combat or CPU cars. Static track scenery stays. */
    for(size_t i=0;i<race->track.spawn_record_count;++i) {
        WwSpawnRecord *r=&race->track.spawn_records[i];
        if(r->sprite_type>=0 && r->sprite_type<WW_WORLD_SPRITE_DESCRIPTORS &&
           race->world_sprites.descriptor[r->sprite_type].classification>0)r->state=-1;
    }
    return true;
}
static void header(const WwNet *n,uint8_t *b,uint8_t kind,uint32_t sequence) {
    memset(b,0,WW_NET_BYTES);b[0]='W';b[1]='W';b[2]=2;b[3]=kind;
    b[4]=n->count;b[6]=n->track;put32(b+8,sequence);put32(b+12,n->tick);
    put32(b+16,(uint32_t)n->seed);put32(b+20,(uint32_t)(n->seed>>32));
}
static bool match(const WwNet *n,const p4_game_multiplayer_message_t *m) {
    const uint8_t *b=m->data;
    return m->bytes>=HEADER && b[0]=='W' && b[1]=='W' && b[2]==2 &&
        b[4]==n->count && b[6]==n->track && b[7]==0 && get32(b+16)==(uint32_t)n->seed &&
        get32(b+20)==(uint32_t)(n->seed>>32);
}
static bool snapshot(WwNet *n,const uint8_t *b) {
    uint32_t revision=get32(b+8),tick=get32(b+12);unsigned part=b[5];
    if(!newer(revision,n->revision)||tick>1000000u||tick<n->tick||part>=(n->count+1u)/2u ||
       b[60]||b[61]||b[62]||b[63])return false;
    if(!n->pending_mask||revision!=n->pending_revision) {
        if(n->pending_mask&&!newer(revision,n->pending_revision))return false;
        n->pending_revision=revision;n->pending_tick=tick;n->pending_mask=0;
    }
    if(tick!=n->pending_tick)return false;
    WwNetPlayer p[2]={0};
    for(unsigned j=0;j<2;++j) {
        unsigned i=part*2+j;const uint8_t *v=b+HEADER+j*18;
        if(i>=n->count){for(unsigned k=0;k<18;++k)if(v[k])return false;continue;}
        uint16_t x=get16(v),y=get16(v+2),h=get16(v+4);
        uint8_t flags=v[7]&15,vehicle=v[7]>>4,lap=v[10],rank=v[11],place=v[12];
        if(x>=4096||y>=4096||h>=1920||v[6]>100||get16(v+8)>40||vehicle>7||lap>4||
           rank<1||rank>n->count||place>n->count||!valid_keys(v[13])||
           ((flags&1)!=0)!=(place!=0)||(place&&lap!=4)||get32(v+14)>5120)return false;
        p[j].motion=(WwPlayerMotion){.camera_x=x,.camera_y=y,.heading=h,.speed_index=v[6],
            .velocity=(int16_t)get16(v+8),.terrain_collision=(flags&4)!=0,.jump_active=(flags&8)!=0};
        p[j].lap=(WwLapState){.current_lap=lap,.finished=(flags&1)!=0,.wrong_way_active=(flags&2)!=0,
            .finish_place=place,.course_progress=get32(v+14),.show_lap_status=true};
        p[j].rank=rank;p[j].buttons=v[13];p[j].vehicle=vehicle;
    }
    for(unsigned j=0;j<2&&part*2+j<n->count;++j)n->pending[part*2+j]=p[j];
    n->pending_mask|=(uint8_t)(1u<<part);
    if(n->pending_mask!=(1u<<((n->count+1u)/2u))-1)return false;
    for(unsigned i=0;i<n->count;++i)for(unsigned j=0;j<i;++j)
        if(n->pending[i].rank==n->pending[j].rank||
           (n->pending[i].lap.finish_place&&n->pending[i].lap.finish_place==n->pending[j].lap.finish_place))return false;
    memcpy(n->players,n->pending,sizeof(n->players));n->revision=revision;n->tick=tick;n->pending_mask=0;return true;
}
bool ww_net_poll(WwNet *n,p4_game_context_t *ctx,uint32_t buttons,uint32_t ms) {
    if(!n->active || n->ended)return false;
    p4_game_multiplayer_status_t st;
    if(!p4_game_multiplayer_read_status(ctx,&st) || st.state!=P4_GAME_MULTIPLAYER_CONNECTED ||
       st.generation!=n->generation || st.session_seed!=n->seed || st.player_count!=n->count ||
       st.local_player_slot!=n->slot || st.role!=(n->host?P4_GAME_MULTIPLAYER_ROLE_HOST:P4_GAME_MULTIPLAYER_ROLE_CLIENT)) {
        end(n);return false;
    }
    for(unsigned i=0;i<n->count;++i)if(i!=n->slot)n->silent_ms[i]+=ms;
    n->send_ms+=ms;
    n->players[n->slot].buttons=keys(buttons);
    /* Opposing directions neutralize, matching the normalized input contract. */
    if((n->players[n->slot].buttons&3)==3)n->players[n->slot].buttons&=(uint8_t)~3u;
    if((n->players[n->slot].buttons&12)==12)n->players[n->slot].buttons&=(uint8_t)~12u;
    bool changed=false;p4_game_multiplayer_message_t m;
    for(unsigned budget=0;budget<8 && p4_game_multiplayer_receive(ctx,&m);++budget) {
        if(m.player_slot>=n->count||m.player_slot==n->slot||(!n->host&&m.player_slot!=0)||!match(n,&m))continue;
        bool accepted=false;
        if(n->host && m.bytes==INPUT_BYTES && m.data[3]==1 && valid_keys(m.data[24]) &&
           m.data[5]==0 && newer(get32(m.data+8),n->received_sequence[m.player_slot])) {
            n->received_sequence[m.player_slot]=get32(m.data+8);n->players[m.player_slot].buttons=m.data[24];accepted=true;
        } else if(!n->host && m.bytes==WW_NET_BYTES && m.data[3]==2 && snapshot(n,m.data)) {
            changed=true;accepted=true;
        }
        if(accepted){n->silent_ms[m.player_slot]=0;n->heard_mask|=(uint8_t)(1u<<m.player_slot);}
        n->heard_peer=n->host?n->heard_mask==(1u<<n->count)-1:(n->heard_mask&1)!=0;
    }
    for(unsigned i=0;i<n->count;++i)if(i!=n->slot&&(n->host||i==0)) {
        if(n->silent_ms[i]>250&&n->host)n->players[i].buttons=0;
        if(n->silent_ms[i]>=3000){end(n);return changed;}
    }
    if(n->send_ms>=88) {
        n->send_ms=0;
        if(n->host)ww_net_publish(n,ctx);
        else {
            uint8_t b[WW_NET_BYTES];header(n,b,1,++n->input_sequence);b[24]=n->players[n->slot].buttons;
            (void)p4_game_multiplayer_send(ctx,b,INPUT_BYTES);
        }
    }
    return changed;
}
void ww_net_publish(WwNet *n,p4_game_context_t *ctx) {
    if(!n->active || !n->host || n->ended)return;
    ++n->revision;
    for(unsigned part=0;part<(n->count+1u)/2u;++part) {
        uint8_t b[WW_NET_BYTES];header(n,b,2,n->revision);b[5]=(uint8_t)part;
        for(unsigned j=0;j<2&&part*2+j<n->count;++j) {
            const WwNetPlayer *p=&n->players[part*2+j];uint8_t *v=b+HEADER+j*18;
            put16(v,p->motion.camera_x);put16(v+2,p->motion.camera_y);put16(v+4,p->motion.heading);
            v[6]=(uint8_t)p->motion.speed_index;
            v[7]=(uint8_t)((p->lap.finished?1:0)|(p->lap.wrong_way_active?2:0)|(p->motion.terrain_collision?4:0)|(p->motion.jump_active?8:0)|(p->vehicle<<4));
            put16(v+8,(uint16_t)p->motion.velocity);v[10]=(uint8_t)p->lap.current_lap;
            v[11]=p->rank;v[12]=(uint8_t)p->lap.finish_place;v[13]=p->buttons;put32(v+14,p->lap.course_progress);
        }
        (void)p4_game_multiplayer_send(ctx,b,sizeof(b));
    }
}

bool ww_net_step(WwNet *n,WwRace *race,const WwRenderer *renderer) {
    if(!n->host || n->ended || !n->heard_peer)return true;
    for(unsigned i=1;i<n->count;++i)if(n->silent_ms[i]>500)return true;
    if(n->tick>=1000000u){end(n);return true;}
    ++n->tick;if(n->tick<WW_NET_COUNTDOWN)return true;
    for(unsigned i=0;i<n->count;++i) {
        WwNetPlayer *p=&n->players[i];if(p->lap.finished)continue;
        if(p->jump_ticks) --p->jump_ticks;
        p->motion.jump_active=p->jump_ticks!=0;
        uint8_t b=p->buttons;
        WwPlayerControls c={.steer_left=(b&LEFT)!=0,.steer_right=(b&RIGHT)!=0,.accelerate=(b&GAS)!=0,.brake=(b&BRAKE)!=0};
        if(!ww_physics_player_step(&p->motion,c,&race->track,renderer->trig_data,WW_TRIG_BYTES,
            renderer->ndist_data,WW_NDIST_BYTES,race->velocity_table,race->velocity_table_size,
            WW_ROAD_DETAIL_HIGH,NULL,NULL))return false;
        if(p->motion.collision_status==4 && !p->motion.jump_active) {
            p->jump_ticks=8;p->motion.jump_active=true;
        }
        uint16_t x,y;
        if(!ww_physics_player_anchor(&p->motion,renderer->trig_data,WW_TRIG_BYTES,&x,&y) ||
           !ww_lap_update_player(&p->lap,&race->track,x,y,3,p->motion.terrain_collision,n->tick,&n->finish_count))return false;
        if(p->lap.finished){p->motion.speed_index=0;p->motion.velocity=0;}
    }
    unsigned order[4]={0,1,2,3};
    for(unsigned i=1;i<n->count;++i) {
        unsigned value=order[i],j=i;
        while(j) {
            const WwNetPlayer *a=&n->players[value],*b=&n->players[order[j-1]];
            bool before=a->lap.finished!=b->lap.finished?a->lap.finished:
                a->lap.finished?a->lap.finish_place<b->lap.finish_place:
                a->lap.course_progress!=b->lap.course_progress?a->lap.course_progress>b->lap.course_progress:a->rank<b->rank;
            if(!before)break;
            order[j]=order[j-1];--j;
        }
        order[j]=value;
    }
    for(unsigned i=0;i<n->count;++i)n->players[order[i]].rank=(uint8_t)(i+1);
    return true;
}
bool ww_net_view(const WwNet *n,WwRace *race,const WwRenderer *renderer) {
    if(!n->active)return true;
    const WwNetPlayer *local=&n->players[n->slot];race->player=local->motion;
    race->displayed_speed=(int16_t)(local->motion.velocity*2);
    race->start_released=n->tick>=WW_NET_COUNTDOWN;
    race->start_light_frame=(uint8_t)(n->tick>=34?2:n->tick>=17?1:0);
    race->start_sequence_frame=(uint16_t)(n->tick>34?34:n->tick);
    race->elapsed_136_ticks=n->tick>34?(n->tick-34)*12:0;
    race->race_time_tenths=race->elapsed_136_ticks*10/136;
    race->horizon_source_offset=(uint16_t)((0x50u+local->motion.heading/8)%0xa0u);
    race->steering_frame=(uint8_t)((local->buttons&LEFT)?0:(local->buttons&RIGHT)?4:2);
    for(unsigned i=0;i<WW_RACER_COUNT;++i) {
        race->racers[i].active=i<n->count;
        if(i>=n->count)continue;
        const WwNetPlayer *p=&n->players[i==0?n->slot:(i<=n->slot?i-1:i)];
        WwRacerState *r=&race->racers[i];r->vehicle=p->vehicle;r->heading=p->motion.heading;
        r->jump_state=(uint16_t)(p->motion.jump_active?1:0);r->jump_height=(uint16_t)(p->motion.jump_active?8:0);
        if(!ww_physics_player_anchor(&p->motion,renderer->trig_data,WW_TRIG_BYTES,&r->world_x,&r->world_y))return false;
        race->racer_lap[i]=p->lap;race->racer_rank[i]=p->rank;
    }
    return true;
}
