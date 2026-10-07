// SPDX-License-Identifier: MIT
/* Deterministic adversarial tests, through the same API as two cartridges. */
#include "tide_maze_internal.h"
#include "p4/input.h"
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { WIRE_CAPACITY = 128 };
typedef struct {
    p4_game_multiplayer_message_t message;
    uint32_t due;
    bool occupied;
} delivery;
typedef struct link link;
typedef struct {
    link *wire;
    unsigned slot;
    uint32_t sequence;
    delivery pending[WIRE_CAPACITY];
} endpoint;
struct link {
    endpoint ends[2];
    uint32_t now, random, transmitted, dropped, duplicated, reordered;
    uint32_t last_sequence[2];
    bool chaos, blackout;
};
typedef struct {
    link wire;
    tm_state state[2];
    p4_game_instance_t game[2];
} pair;
static uint32_t random_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}
static bool read_status(void *context, p4_game_multiplayer_status_t *out) {
    endpoint *e = context;
    *out = (p4_game_multiplayer_status_t){
        .generation = 11, .session_seed = 42,
        .state = P4_GAME_MULTIPLAYER_CONNECTED,
        .role = e->slot ? P4_GAME_MULTIPLAYER_ROLE_CLIENT : P4_GAME_MULTIPLAYER_ROLE_HOST,
        .local_player_slot = (uint8_t)e->slot, .player_count = 2
    };
    return true;
}
static void enqueue(endpoint *e, p4_game_multiplayer_message_t message, uint32_t due) {
    for (unsigned i = 0; i < WIRE_CAPACITY; ++i) {
        if (e->pending[i].occupied) continue;
        e->pending[i] = (delivery){message, due, true};
        return;
    }
    assert(0 && "test wire queue overflow");
}
static bool transmit(void *context, const uint8_t *data, size_t bytes) {
    endpoint *e = context;
    link *w = e->wire;
    ++w->transmitted;
    p4_game_multiplayer_message_t m = {
        .sequence = ++e->sequence, .player_slot = (uint8_t)e->slot,
        .bytes = (uint8_t)bytes
    };
    assert(bytes <= sizeof(m.data));
    memcpy(m.data, data, bytes);
    uint32_t r = random_next(&w->random);
    if (w->blackout || (w->chaos && r % 100 < 15)) {
        ++w->dropped;
        return true; /* Accepted by the OS but lost in transit. */
    }
    uint32_t delay = w->chaos ? (r >> 8) % 121 : 0;
    endpoint *peer = &w->ends[1 - e->slot];
    enqueue(peer, m, w->now + delay);
    if (w->chaos && (r >> 16) % 100 < 10) {
        enqueue(peer, m, w->now + delay + 100);
        ++w->duplicated;
    }
    return true;
}
static bool receive(void *context, p4_game_multiplayer_message_t *out) {
    endpoint *e = context;
    unsigned next = WIRE_CAPACITY;
    for (unsigned i = 0; i < WIRE_CAPACITY; ++i) {
        delivery *d = &e->pending[i];
        if (!d->occupied || d->due > e->wire->now) continue;
        if (next == WIRE_CAPACITY || d->due < e->pending[next].due) next = i;
    }
    if (next == WIRE_CAPACITY) return false;
    *out = e->pending[next].message;
    e->pending[next].occupied = false;
    uint32_t *last = &e->wire->last_sequence[e->slot];
    if (out->sequence <= *last) ++e->wire->reordered;
    if (out->sequence > *last) *last = out->sequence;
    return true;
}
static const p4_game_multiplayer_profile_t profile = {
    .schema = 1, .style = P4_GAME_MULTIPLAYER_STYLE_REALTIME,
    .min_players = 2, .max_players = 2, .tick_rate_hz = 20,
    .message_bytes = 64, .protocol = 1
};
static void begin(pair *p, uint32_t seed, bool chaos) {
    memset(p, 0, sizeof(*p));
    p->wire.random = seed;
    p->wire.chaos = chaos;
    for (unsigned i = 0; i < 2; ++i) {
        endpoint *e = &p->wire.ends[i];
        e->wire = &p->wire; e->slot = i;
        p4_game_services_t services = {
            .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                P4_GAME_CAP_VIDEO_HIGH_RES | P4_GAME_CAP_MULTIPLAYER_SESSION,
            .game_id = p4_tide_maze_game.id, .multiplayer_context = e,
            .multiplayer_read_status = read_status, .multiplayer_send = transmit,
            .multiplayer_receive = receive, .multiplayer_profile = &profile
        };
        assert(p4_game_instance_start(&p->game[i], &p4_tide_maze_game, &services,
            &p->state[i], sizeof(p->state[i])));
    }
}
static void step(pair *p, uint32_t host, uint32_t guest, uint32_t press, uint32_t ms) {
    p->wire.now += ms;
    p4_game_input_t in = {.held = guest};
    assert(p4_game_instance_update(&p->game[1], &in, ms) == P4_GAME_CONTINUE);
    in = (p4_game_input_t){.held = host, .pressed = press};
    assert(p4_game_instance_update(&p->game[0], &in, ms) == P4_GAME_CONTINUE);
}
static int volume(const tm_state *s) {
    int v = 0;
    for (unsigned i = 0; i < TM_CELLS; ++i) v += s->water[i];
    return v;
}
static void water_invariants(const tm_state *s, int original) {
    assert(volume(s) == original);
    for (unsigned i = 0; i < TM_CELLS; ++i) {
        assert(s->water[i] >= 0 && s->water[i] <= 1024);
        assert(s->flow_x[i] >= -40 && s->flow_x[i] <= 40);
        assert(s->flow_y[i] >= -40 && s->flow_y[i] <= 40);
        if (!s->wet[i]) assert(s->water[i] == 0);
        if (i % TM_W + 1 < TM_W && (!s->wet[i] || !s->wet[i + 1])) assert(s->flow_x[i] == 0);
        if (i / TM_W + 1 < TM_H && (!s->wet[i] || !s->wet[i + TM_W])) assert(s->flow_y[i] == 0);
    }
}
static void water_stress(void) {
    uint32_t rng = 0x28a39b4;
    for (unsigned level = 0; level < TM_LEVELS; ++level) {
        tm_state s = {0};
        tm_reset(&s, level); s.linked = true;
        int original = volume(&s);
        for (unsigned frame = 0; frame < 60000; ++frame) {
            if (frame % 37 == 0) {
                for (unsigned player = 0; player < 2; ++player) {
                    tm_intent *in = &s.intent[player];
                    in->x = (int16_t)((int)(random_next(&rng) % 2001) - 1000);
                    in->y = (int16_t)((int)(random_next(&rng) % 2001) - 1000);
                    in->spin = (int16_t)((int)(random_next(&rng) % 2001) - 1000);
                    in->jolt = (int16_t)((int)(random_next(&rng) % 2001) - 1000);
                    s.ball[player].x = (int32_t)(19 + random_next(&rng) % 202) * TM_Q;
                    s.ball[player].y = (int32_t)(19 + random_next(&rng) % 106) * TM_Q;
                    s.ball[player].vx = (int32_t)(random_next(&rng) % 561) - 280;
                    s.ball[player].vy = (int32_t)(random_next(&rng) % 561) - 280;
                }
            }
            tm_fluid(&s);
            water_invariants(&s, original);
        }
    }
    puts("water stress: 180000 steps / 60 simulated minutes, conserved and bounded");
}
static bool motion(void *context, p4_game_motion_t *out) {
    *out = *(p4_game_motion_t *)context;
    return true;
}
static void motion_stress(void) {
    const int32_t poses[][3] = {
        {0,0,1000}, {0,0,-1000}, {1000,0,0}, {-1000,0,0},
        {0,1000,0}, {0,-1000,0}, {577,577,577}, {-577,577,-577},
        {799,601,0}, {800,600,0}, {299,25,0}, {0,300,0}
    };
    uint32_t rng = 0x748929;
    for (unsigned pose = 0; pose < sizeof(poses) / sizeof(poses[0]); ++pose) {
        tm_state s = {0}; tm_reset(&s, 0);
        p4_game_motion_t m = {.sequence=1, .valid=true};
        memcpy(m.accel_mg, poses[pose], sizeof(m.accel_mg));
        p4_game_services_t svc = {.available_capabilities=P4_GAME_CAP_MOTION,
            .motion_context=&m, .read_motion=motion};
        p4_game_context_t ctx = {.services=&svc};
        p4_game_input_t in = {0};
        tm_controls(&ctx, &s, &in, 20, true);
        assert(s.motion_live && s.calibrated && s.intent[0].x == 0 && s.intent[0].y == 0);
        for (unsigned i = 0; i < 10000; ++i) {
            for (unsigned axis = 0; axis < 3; ++axis) {
                m.accel_mg[axis] = (int32_t)(random_next(&rng) % 8001) - 4000;
                m.gyro_mdps[axis] = (int32_t)(random_next(&rng) % 4000001) - 2000000;
            }
            ++m.sequence;
            s.orientation = (uint8_t)(i % 4);
            tm_controls(&ctx, &s, &in, i % 101, i % 97 == 0);
            assert(s.intent[0].x >= -1000 && s.intent[0].x <= 1000);
            assert(s.intent[0].y >= -1000 && s.intent[0].y <= 1000);
            assert(s.intent[0].spin >= -1000 && s.intent[0].spin <= 1000);
            assert(s.intent[0].jolt >= -1000 && s.intent[0].jolt <= 1000);
        }
        /* A fresh but near-zero-g sample must neutralize every motion channel. */
        memset(m.accel_mg, 0, sizeof(m.accel_mg)); ++m.sequence;
        tm_controls(&ctx, &s, &in, 20, false);
        assert(!s.motion_live && !s.intent[0].x && !s.intent[0].y && !s.intent[0].spin && !s.intent[0].jolt);
    }
    puts("motion stress: 120000 full-range six-axis samples, 12 neutral poses, four orientations");
}
/* Paths are chosen by tile connectivity; all movement is ordinary A+directions.
 * The guest steers from its delayed snapshot, never the host's hidden state. */
typedef struct { int path[1024]; unsigned count, next; bool rescuing; } route;
static void append_path(route *r, unsigned level, int start, int goal) {
    int prev[135], queue[135], head=0, tail=0, reverse[135], count=0;
    for (unsigned i=0; i<135; ++i) prev[i]=-1;
    queue[tail++]=start; prev[start]=start;
    while (head<tail && prev[goal]<0) {
        int cell=queue[head++], x=cell%15, y=cell/15;
        const int nx[4]={x-1,x+1,x,x}, ny[4]={y,y,y-1,y+1};
        for (unsigned n=0; n<4; ++n) {
            char t=tm_tile(level,nx[n],ny[n]); if(t=='#'||t=='~')continue;
            int j=ny[n]*15+nx[n]; if(prev[j]>=0)continue;
            prev[j]=cell; queue[tail++]=j;
        }
    }
    assert(prev[goal]>=0);
    for(int cell=goal;cell!=start;cell=prev[cell]) reverse[count++]=cell;
    while(count>0) {assert(r->count<1024);r->path[r->count++]=reverse[--count];}
}
static route make_route(unsigned level, unsigned player) {
    route r={0};int last=16;unsigned pearl=0;int exit=0;
    for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x){
        char t=tm_tile(level,x,y);
        if(t=='o' && pearl++%2==player){append_path(&r,level,last,y*15+x);last=y*15+x;}
        if(t=='E')exit=y*15+x;
    }
    append_path(&r,level,last,exit);
    return r;
}
static uint32_t steer(route *r, const tm_ball *b, unsigned level) {
    if (b->rescue && !r->rescuing) {
        route recovered = {0};
        int start=b->y/(16*TM_Q)*15+b->x/(16*TM_Q);
        append_path(&recovered,level,start,r->path[r->next]);
        for(unsigned i=r->next+1;i<r->count;++i){
            assert(recovered.count<1024);
            recovered.path[recovered.count++]=r->path[i];
        }
        recovered.rescuing=true; *r=recovered;
    }
    if (!b->rescue) r->rescuing=false;
    int cell=r->path[r->next], x=(cell%15*16+8)*TM_Q, y=(cell/15*16+8)*TM_Q;
    int dx=x-b->x,dy=y-b->y;
    if(dx>-2*TM_Q&&dx<2*TM_Q&&dy>-2*TM_Q&&dy<2*TM_Q&&r->next+1<r->count){
        ++r->next;return steer(r,b,level);
    }
    uint32_t held=P4_BUTTON_A;
    if(dx>TM_Q)held|=P4_BUTTON_RIGHT;else if(dx<-TM_Q)held|=P4_BUTTON_LEFT;
    if(dy>TM_Q)held|=P4_BUTTON_DOWN;else if(dy<-TM_Q)held|=P4_BUTTON_UP;
    return held;
}
static void converge(pair *p) {
    p->wire.chaos=false;p->wire.blackout=false;
    memset(p->wire.ends[0].pending,0,sizeof(p->wire.ends[0].pending));
    memset(p->wire.ends[1].pending,0,sizeof(p->wire.ends[1].pending));
    p->state[0].net_ms=50;
    tm_network_publish(&p->game[0].context,&p->state[0]);
    assert(tm_network_poll(&p->game[1].context,&p->state[1],0));
    const tm_state *h=&p->state[0], *c=&p->state[1];
    assert(c->received_revision==h->revision && c->phase==h->phase && c->level==h->level);
    assert(c->time_ms==h->time_ms && c->pearls==h->pearls && c->docked==h->docked && c->rescues==h->rescues);
    for(unsigned i=0;i<2;++i){
        assert(c->ball[i].x==h->ball[i].x && c->ball[i].y==h->ball[i].y);
        assert(c->ball[i].vx==h->ball[i].vx && c->ball[i].vy==h->ball[i].vy && c->ball[i].rescue==h->ball[i].rescue);
    }
}
static void cooperative_voyage(uint32_t seed) {
    pair p;begin(&p,seed,true);
    for(unsigned level=0;level<TM_LEVELS;++level){
        route routes[2]={make_route(level,0),make_route(level,1)};
        unsigned frames=0;
        while(p.state[0].phase==TM_PLAY && frames<10000){
            uint32_t host=steer(&routes[0],&p.state[0].ball[0],level);
            uint32_t guest=p.state[1].level==level&&p.state[1].snapshot_seen?
                steer(&routes[1],&p.state[1].ball[1],level):P4_BUTTON_A;
            /* Every 12 seconds, a 600 ms outage expires remote input but recovers. */
            p.wire.blackout=frames%600>=550 && frames%600<580;
            step(&p,host,guest,0,20);++frames;
            assert(p.state[0].linked && p.state[1].linked);
        }
        if(p.state[0].phase!=(level+1==TM_LEVELS?TM_WON:TM_CLEAR)){
            fprintf(stderr,"voyage seed=%u level=%u frames=%u phase=%d time=%u routes=%u/%u %u/%u balls=(%d,%d)(%d,%d) rescues=%u pearls=%u\n",
                seed,level,frames,p.state[0].phase,p.state[0].time_ms,
                routes[0].next,routes[0].count,routes[1].next,routes[1].count,
                p.state[0].ball[0].x/TM_Q,p.state[0].ball[0].y/TM_Q,
                p.state[0].ball[1].x/TM_Q,p.state[0].ball[1].y/TM_Q,p.state[0].rescues,p.state[0].pearls);
            assert(0 && "cooperative voyage did not finish");
        }
        converge(&p);
        printf("co-op seed=%u maze=%u completed in %u ms, rescues=%u\n",seed,level+1,150000-p.state[0].time_ms,p.state[0].rescues);
        if(level+1<TM_LEVELS){step(&p,0,0,P4_BUTTON_A,20);p.wire.chaos=true;}
    }
    assert(p.wire.dropped && p.wire.duplicated && p.wire.reordered);
    printf("link seed=%u sent=%u dropped=%u duplicated=%u late/duplicate=%u\n",
        seed,p.wire.transmitted,p.wire.dropped,p.wire.duplicated,p.wire.reordered);
}
static void start_solo(p4_game_instance_t *g, tm_state *s) {
    p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_VIDEO_HIGH_RES,
        .game_id=p4_tide_maze_game.id};
    assert(p4_game_instance_start(g,&p4_tide_maze_game,&services,s,sizeof(*s)));
    p4_game_input_t in={.pressed=P4_BUTTON_A};
    assert(p4_game_instance_update(g,&in,0)==P4_GAME_CONTINUE);
}
static p4_game_result_t touch_sample(p4_game_instance_t *g,p4_game_input_mapper_t *mapper,
                                     unsigned x,unsigned y,bool down) {
    p4_physical_touch_t physical={
        (uint16_t)(P4_INPUT_VIEWPORT_LEFT+(2*x+1)*P4_INPUT_VIEWPORT_WIDTH/640),
        (uint16_t)(P4_INPUT_VIEWPORT_TOP+(2*y+1)*P4_INPUT_VIEWPORT_HEIGHT/400)
    };
    p4_game_input_t in;
    p4_game_input_mapper_update(mapper,true,&physical,down?1:0,0,&in);
    if(down)assert(in.touch_count==1 && in.touches[0].x==x && in.touches[0].y==y);
    return p4_game_instance_update(g,&in,20);
}
static void touch_regions(void) {
    tm_state s={0};p4_game_instance_t g={0};start_solo(&g,&s);
    p4_game_input_mapper_t mapper={0};
    assert(touch_sample(&g,&mapper,285,187,true)==P4_GAME_CONTINUE);
    assert(s.phase==TM_PAUSE);
    assert(touch_sample(&g,&mapper,285,187,false)==P4_GAME_CONTINUE);
    assert(touch_sample(&g,&mapper,100,140,true)==P4_GAME_CONTINUE);
    assert(s.phase==TM_PLAY);
    assert(touch_sample(&g,&mapper,100,140,false)==P4_GAME_CONTINUE);
    assert(touch_sample(&g,&mapper,185,188,true)==P4_GAME_CONTINUE);
    assert(s.intent[0].brake);
    assert(touch_sample(&g,&mapper,185,188,false)==P4_GAME_CONTINUE);
    assert(!s.intent[0].brake);
    assert(touch_sample(&g,&mapper,100,56,true)==P4_GAME_CONTINUE);
    assert(s.intent[0].x>0);
    assert(touch_sample(&g,&mapper,100,56,false)==P4_GAME_CONTINUE);
    assert(touch_sample(&g,&mapper,300,5,true)==P4_GAME_EXIT_TO_LAUNCHER);
    puts("touch mapping: physical viewport to canonical steering, Brake, Pause, resume and Exit");
}
static void variable_frames(void) {
    tm_state a={0}, b={0};p4_game_instance_t ga={0},gb={0};
    start_solo(&ga,&a);start_solo(&gb,&b);
    p4_game_input_t in={.held=P4_BUTTON_RIGHT|P4_BUTTON_A};
    for(unsigned i=0;i<500;++i)assert(p4_game_instance_update(&ga,&in,20)==P4_GAME_CONTINUE);
    const uint32_t delta[]={0,1,7,16,33,64,100};
    uint32_t total=0;unsigned frame=0;
    while(total<10000){
        uint32_t ms=delta[frame++%7];if(ms>10000-total)ms=10000-total;
        assert(p4_game_instance_update(&gb,&in,ms)==P4_GAME_CONTINUE);total+=ms;
    }
    assert(a.time_ms==b.time_ms && a.accumulator==b.accumulator);
    assert(memcmp(a.ball,b.ball,sizeof(a.ball))==0);
    assert(memcmp(a.water,b.water,sizeof(a.water))==0);
    assert(memcmp(a.flow_x,b.flow_x,sizeof(a.flow_x))==0);
    uint32_t before=b.time_ms;
    assert(p4_game_instance_update(&gb,&in,UINT32_MAX)==P4_GAME_CONTINUE);
    assert(before-b.time_ms==100 && b.accumulator<TM_STEP);
    puts("frame timing: identical fixed-step physics at variable 0..100 ms; bounded UINT32_MAX catch-up");
}
static void local_presentation(void){
 tm_state s={0};tm_reset(&s,0);s.intent[0].x=600;
 tm_simulate(&s);int32_t start=s.previous_x[0],end=s.ball[0].x,last=start,x,y;
 assert(end>start);
 for(unsigned ms=0;ms<20;++ms){s.accumulator=ms;tm_visual_ball(&s,0,&x,&y);assert(x>=last&&x>=start&&x<=end);last=x;}
 assert(last>start&&last<end);
 s.phase=TM_PAUSE;tm_visual_ball(&s,0,&x,&y);assert(x==end);
 tm_reset(&s,0);s.ball[0].x=40*TM_Q;s.ball[0].y=120*TM_Q;tm_simulate(&s);
 assert(s.ball[0].rescue&&s.previous_x[0]==s.ball[0].x&&s.previous_y[0]==s.ball[0].y);
 /* Independent double-precision projection followed by integer touch inverse. */
 for(int py=8;py<144;py+=8)for(int px=8;px<240;px+=8){
  double xc=px-120,d=600-py-xc/10;
  int sx=(int)(156+(xc*530+(py-72)*60)/d+.5);
  int sy=(int)(42+(py*330+xc*45-1200)/d+.5),bx,by;
  assert(tm_screen_to_board(sx,sy,&bx,&by));assert(abs(bx-px)<=2&&abs(by-py)<=2);
 }
 puts("Local presentation: fractional motion, pause, respawn and perspective touch inversion PASS");
}
static int32_t visible_x(const tm_state *s, unsigned player) {
    return s->previous_x[player]+(s->ball[player].x-s->previous_x[player])*(int32_t)s->blend_ms/50;
}
static void network_timing(void) {
    pair p;begin(&p,0x871,false);
    for(unsigned i=0;i<100;++i)step(&p,P4_BUTTON_A,P4_BUTTON_A,0,20);
    /* A 50 Hz update loop must retain the fraction of the 20 Hz send period. */
    assert(p.wire.ends[0].sequence>=40 && p.wire.ends[0].sequence<=41);
    assert(p.wire.ends[1].sequence>=40 && p.wire.ends[1].sequence<=41);
    converge(&p);
    p.state[0].ball[0].x+=4*TM_Q;
    converge(&p);
    p.state[1].blend_ms=20;
    int32_t before=visible_x(&p.state[1],0);
    p.state[0].ball[0].x+=4*TM_Q;
    converge(&p);
    assert(visible_x(&p.state[1],0)==before);
    puts("network timing: 20 Hz publications at 50 Hz updates; continuous early-snapshot interpolation");
}
static void pause_timeout_retry(void) {
    pair p;begin(&p,0x5123,false);
    for(unsigned i=0;i<20;++i)step(&p,P4_BUTTON_RIGHT,P4_BUTTON_DOWN,0,20);
    step(&p,0,0,P4_BUTTON_START,20);
    for(unsigned i=0;i<200;++i)step(&p,P4_BUTTON_LEFT,P4_BUTTON_UP,0,20);
    assert(p.state[0].phase==TM_PAUSE && p.state[1].phase==TM_PAUSE);
    uint32_t time=p.state[0].time_ms;
    for(unsigned i=0;i<50;++i)step(&p,0,0,0,20);
    assert(p.state[0].time_ms==time);
    step(&p,0,0,P4_BUTTON_START,20);assert(p.state[0].phase==TM_PLAY);
    p.wire.blackout=true;
    for(unsigned i=0;i<170;++i)step(&p,0,0,0,20);
    assert(!p.state[0].linked&&!p.state[1].linked);
    assert(p.state[0].phase==TM_LINK_LOST&&p.state[1].phase==TM_LINK_LOST);
    step(&p,0,0,P4_BUTTON_A,20);assert(p.state[0].phase==TM_PLAY&&p.state[0].slot==0);
    /* Retry the same maze while still linked; the client's water must reset too. */
    begin(&p,0x5123,false);
    for(unsigned i=0;i<100;++i)step(&p,P4_BUTTON_RIGHT,P4_BUTTON_DOWN,0,20);
    p.state[0].time_ms=20;step(&p,0,0,0,20);converge(&p);
    assert(p.state[1].phase==TM_LOST);
    /* Retry with zero delta so there is no new fluid step before the snapshot. */
    step(&p,0,0,P4_BUTTON_A,0);converge(&p);
    assert(memcmp(p.state[0].water,p.state[1].water,sizeof(p.state[0].water))==0);
    assert(memcmp(p.state[0].flow_x,p.state[1].flow_x,sizeof(p.state[0].flow_x))==0);
    assert(memcmp(p.state[0].flow_y,p.state[1].flow_y,sizeof(p.state[0].flow_y))==0);
    /* A retry can occur before the lost-state snapshot was ever delivered. */
    begin(&p,0x5123,false);
    for(unsigned i=0;i<100;++i)step(&p,P4_BUTTON_RIGHT,P4_BUTTON_DOWN,0,20);
    p.state[0].time_ms=20;
    p.wire.blackout=true;step(&p,0,0,0,20);
    step(&p,0,0,P4_BUTTON_A,0);converge(&p);
    assert(memcmp(p.state[0].water,p.state[1].water,sizeof(p.state[0].water))==0);
    puts("lifecycle: pause heartbeats, silence timeout, solo fallback, same-maze co-op retry including missed result");
}
int main(int argc, char **argv) {
    setbuf(stdout,NULL);
    if(argc==1||strcmp(argv[1],"motion")==0)motion_stress();
    if(argc==1||strcmp(argv[1],"water")==0)water_stress();
    if(argc==1||strcmp(argv[1],"coop")==0){
        cooperative_voyage(0x713);cooperative_voyage(0x983af);cooperative_voyage(0x573341);
    }
    if(argc==1||strcmp(argv[1],"lifecycle")==0)pause_timeout_retry();
    if(argc==1||strcmp(argv[1],"timing")==0){variable_frames();network_timing();local_presentation();}
    if(argc==1||strcmp(argv[1],"touch")==0)touch_regions();
    puts("Tide Maze adversarial stress PASS");
    return 0;
}
