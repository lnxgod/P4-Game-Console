// SPDX-License-Identifier: MIT
#include "p4/doom_arena.h"
#include <assert.h>
#include <string.h>


static uint8_t command(p4_doom_arena_t *a,unsigned slot,uint8_t opcode,uint8_t generation)
{
    uint8_t chat[4]={0}; chat[slot]=opcode;
    assert(!p4_doom_arena_vote_tick(a,chat));
    chat[slot]=(uint8_t)(0xa0U+generation);
    return p4_doom_arena_vote_tick(a,chat);
}
static void wait_vote(p4_doom_arena_t *a,unsigned tics)
{
    const uint8_t chat[4]={0};
    for(unsigned i=0;i<tics;++i) assert(!p4_doom_arena_vote_tick(a,chat));
}
static void vote_tests(void)
{
    p4_doom_arena_t peers[4];
    for(unsigned i=0;i<4;++i) {
        p4_doom_arena_t *a=&peers[i];
        assert(p4_doom_arena_begin_selected(a,4,1));
        assert(p4_doom_arena_kill(a,0,1));
        uint8_t gen=a->vote_generation;
        assert(!command(a,0,0x86,gen)); /* DWANGO 5 MAP01 */
        assert(a->vote_map==6 && a->vote_yes==1);
        assert(!command(a,1,0xf0,gen)); /* A 2/4 tie is not sufficient. */
        assert(a->vote_map==6);
        assert(!command(a,1,0xf1,gen)); /* One ballot per visit. */
        assert(!a->vote_no);
        assert(!command(a,2,0xf0,(uint8_t)(gen+1U))); /* stale/future token */
        assert(a->vote_yes==3);
        assert(command(a,2,0xf0,gen)==6);
        assert(p4_doom_arena_select(a,6));
        assert(a->map_count==24 && a->players[0].kills==1 && a->players[0].visit==1);
        for(unsigned map=2;map<=24;++map) assert(p4_doom_arena_map_number(p4_doom_arena_next_map(a))==map);
        assert(p4_doom_arena_next_map(a)==6); /* never MAP25 or the commercial finale */
        assert(!command(a,0,0x81,a->vote_generation)); /* cooldown */
        assert(!a->vote_map); wait_vote(a,P4_DOOM_ARENA_VOTE_COOLDOWN);
        gen=a->vote_generation;
        assert(!command(a,0,0x81,gen));
        assert(!command(a,1,0xf1,gen)); assert(!command(a,2,0xf1,gen));
        assert(!a->vote_map); /* two NO ballots make >50% impossible */
        wait_vote(a,P4_DOOM_ARENA_VOTE_COOLDOWN);
        assert(!command(a,0,0x81,a->vote_generation));
        wait_vote(a,P4_DOOM_ARENA_VOTE_TICS); assert(!a->vote_map);
        wait_vote(a,P4_DOOM_ARENA_VOTE_COOLDOWN);
        /* Partial command expires, invalid op and range never start a vote. */
        uint8_t chat[4]={0x81,0,0,0}; assert(!p4_doom_arena_vote_tick(a,chat));
        wait_vote(a,4); chat[0]=(uint8_t)(0xa0U+a->vote_generation);
        assert(!p4_doom_arena_vote_tick(a,chat)); wait_vote(a,4);
        assert(!command(a,0,0x9e,a->vote_generation)); assert(!a->vote_map);
        gen=a->vote_generation; assert(!command(a,0,0x81,gen));
        assert(!command(a,1,0xf0,gen));
        /* A departure reduces eligible voters; all four simulations agree. */
        p4_doom_arena_activity_t activity[4]={{.moving=true},{.moving=true},{.moving=true},{0}};
        (void)p4_doom_arena_tick(a,7,activity);
        memset(chat,0,sizeof(chat)); assert(p4_doom_arena_vote_tick(a,chat)==1);
        assert(p4_doom_arena_select(a,1));
        assert(a->players[0].kills==1 && a->connected_mask==7 && a->map_count==5);
        wait_vote(a,P4_DOOM_ARENA_VOTE_COOLDOWN);
        /* Idle seats cannot propose, vote, or retain partial commands. */
        activity[0].moving=false;
        for(unsigned t=0;t<P4_DOOM_ARENA_IDLE_TICS;++t) (void)p4_doom_arena_tick(a,7,activity);
        assert(!command(a,0,0x86,a->vote_generation) && !a->vote_map);
        assert(!command(a,1,0x86,a->vote_generation));
        assert(command(a,2,0xf0,a->vote_generation)==6);
    }
    for(unsigned i=1;i<4;++i) assert(!memcmp(&peers[0],&peers[i],sizeof(peers[0])));
    assert(!p4_doom_arena_begin_selected(&peers[0],4,30));
}

static void rejoin_tests(void)
{
    p4_doom_arena_t peers[4];
    const p4_doom_arena_activity_t moving[4] = {
        {.moving=true}, {.moving=true}, {.moving=true}, {.moving=true}
    };
    for (unsigned i=0; i<4; ++i) {
        p4_doom_arena_t *a=&peers[i];
        assert(p4_doom_arena_begin_selected(a,4,7));
        assert(p4_doom_arena_kill(a,0,2));
        assert(p4_doom_arena_kill(a,1,2));
        assert(p4_doom_arena_kill(a,3,2));
        (void)p4_doom_arena_tick(a,7,moving);
        assert(a->players[3].visit==1 && !a->players[3].active);
        assert(a->players[3].kills==0 && a->connected_mask==7);
        assert(!command(a,0,0x88,a->vote_generation));
        const uint16_t vote_tics=a->vote_tics;
        const uint8_t vote_generation=a->vote_generation;
        const uint8_t map_index=a->map_index;
        assert(p4_doom_arena_rejoin(a,3));
        assert(a->players[3].visit==2 && a->players[3].active);
        assert(a->players[3].kills==0 && !a->players[3].idle_tics);
        assert(a->connected_mask==15 && a->players[0].kills==1);
        assert(a->players[1].kills==1 && a->players[0].visit==1);
        assert(a->vote_map==8 && a->vote_yes==1 && !a->vote_no);
        assert(a->vote_tics==vote_tics && a->vote_generation==vote_generation);
        assert(a->map_index==map_index && a->maps[map_index]==7);
        assert(!p4_doom_arena_rejoin(a,3));
        assert(!p4_doom_arena_rejoin(a,0));
        assert(!p4_doom_arena_rejoin(a,4));
        assert(!command(a,3,0xf0,a->vote_generation));
        assert(command(a,1,0xf0,a->vote_generation)==8);
        assert(p4_doom_arena_select(a,8));
        assert(a->players[0].kills==1 && a->players[1].kills==1);
        (void)p4_doom_arena_tick(a,7,moving);
        assert(p4_doom_arena_rejoin(a,3));
        assert(a->players[3].visit==3 && a->players[3].kills==0);
        a->players[3].visit=UINT32_MAX;
        (void)p4_doom_arena_tick(a,7,moving);
        assert(!p4_doom_arena_rejoin(a,3));
    }
    for (unsigned i=1; i<4; ++i)
        assert(!memcmp(&peers[0],&peers[i],sizeof(peers[0])));
    assert(!p4_doom_arena_rejoin(NULL,1));
    assert(p4_doom_arena_begin_selected(&peers[0],2,1));
    assert(!p4_doom_arena_rejoin(&peers[0],2)); /* Never an original seat. */
}

static void late_join_tests(void)
{
    p4_doom_arena_t a;
    assert(!p4_doom_arena_begin_selected_mask(&a, 1, 1, 1));
    assert(!p4_doom_arena_begin_selected_mask(&a, 5, 1, 1));
    assert(!p4_doom_arena_begin_selected_mask(&a, 4, 0, 1));
    assert(!p4_doom_arena_begin_selected_mask(&a, 4, 2, 1));
    assert(!p4_doom_arena_begin_selected_mask(&a, 3, 9, 1));
    assert(!p4_doom_arena_begin_selected_mask(&a, 4, 1, 30));
    assert(p4_doom_arena_begin_selected_mask(&a, 4, 1, 7));
    assert(a.capacity == 4 && a.connected_mask == 1);
    assert(p4_doom_arena_active_mask(&a) == 1 && a.players[0].visit == 1);
    for (unsigned i = 1; i < 4; ++i) {
        assert(!a.players[i].visit && !a.players[i].active);
        assert(!a.players[i].kills && !a.players[i].idle_tics);
        assert(!p4_doom_arena_rejoin(&a, (uint8_t)i));
    }
    const p4_doom_arena_activity_t moving[4] = {
        {.moving=true}, {.moving=true, .firing=true, .use=true},
        {.moving=true, .firing=true, .use=true}, {.moving=true}
    };
    for (unsigned tic = 0; tic < 2000; ++tic) (void)p4_doom_arena_tick(&a, 1, moving);
    assert(a.connected_mask == 1 && a.players[0].visit == 1);
    for (unsigned i = 1; i < 4; ++i)
        assert(!a.players[i].visit && !a.players[i].active && !a.players[i].idle_tics);
    /* A first admission preserves current match/host and current vote. */
    a.players[0].kills = 17;
    a.vote_map = 9; a.vote_tics = 314; a.vote_yes = 1;
    const uint8_t generation = a.vote_generation, map_index = a.map_index;
    assert(!p4_doom_arena_activate(&a, 0));
    assert(!p4_doom_arena_activate(&a, 4));
    assert(p4_doom_arena_activate(&a, 3));
    assert(a.players[3].visit == 1 && a.players[3].active);
    assert(a.connected_mask == 9 && a.players[0].kills == 17);
    assert(a.vote_map == 9 && a.vote_tics == 314 && a.vote_yes == 1);
    assert(a.vote_generation == generation && a.map_index == map_index);
    assert(!p4_doom_arena_activate(&a, 3));
    (void)p4_doom_arena_tick(&a, 1, moving);
    assert(p4_doom_arena_rejoin(&a, 3));
    assert(a.players[3].visit == 2 && a.connected_mask == 9);
    assert(p4_doom_arena_activate(&a, 1));
    assert(a.players[1].visit == 1 && a.connected_mask == 11);
    assert(a.players[0].kills == 17 && !a.players[2].visit);
    a.players[2].visit = UINT32_MAX;
    assert(!p4_doom_arena_activate(&a, 2));
    assert(p4_doom_arena_begin_selected_mask(&a, 3, 1, 7));
    assert(!p4_doom_arena_activate(&a, 3));
}

int main(void)
{
    /* An orphan generation token or invalid opcode must not swallow a new vote. */
    for (uint8_t gen=0; gen<64; ++gen) {
        p4_doom_arena_t a;
        assert(p4_doom_arena_begin_selected(&a,4,1)); a.vote_generation=gen;
        uint8_t chat[4]={(uint8_t)(0xa0U+gen),0,0,0};
        assert(!p4_doom_arena_vote_tick(&a,chat)); assert(!a.vote_wait[0]);
        assert(!command(&a,0,0x9e,gen)); assert(!a.vote_wait[0]);
        assert(!command(&a,0,0x9d,gen)); assert(a.vote_map==29);
        assert(!command(&a,1,0xf0,gen)); assert(command(&a,2,0xf0,gen)==29);
    }
    vote_tests();
    rejoin_tests();
    late_join_tests();
    p4_doom_arena_t peers[4];
    const uint8_t maps[] = {1, 7, 32};
    for (unsigned i = 0; i < 4; ++i) assert(p4_doom_arena_begin(&peers[i], 4, maps, 3));
    p4_doom_arena_activity_t activity[4] = {{0}};
    activity[1].firing = true; /* A stationary shooter is active. */
    activity[2].moving = true;
    activity[0].use = true; /* Held Use cannot prevent a break or auto-return. */
    for (unsigned i = 0; i < 4; ++i) assert(p4_doom_arena_kill(&peers[i], 0, 3));
    for (unsigned tick = 1; tick <= P4_DOOM_ARENA_IDLE_TICS; ++tick) {
        for (unsigned i = 0; i < 4; ++i) {
            p4_doom_arena_transition_t r = p4_doom_arena_tick(&peers[i], 15, activity);
            assert(r.returned == 0);
            assert(r.entered_break == (tick == P4_DOOM_ARENA_IDLE_TICS ? 9 : 0));
        }
        for (unsigned i = 1; i < 4; ++i) assert(!memcmp(&peers[0], &peers[i], sizeof(peers[0])));
    }
    assert(peers[0].players[0].kills == 0 && !peers[0].players[0].active);
    assert(peers[0].players[1].active && peers[0].players[2].active);
    assert(!p4_doom_arena_kill(&peers[0], 1, 0));
    assert(!p4_doom_arena_kill(&peers[0], 1, 1));
    assert(!p4_doom_arena_kill(&peers[0], 4, 1));
    assert(p4_doom_arena_tick(&peers[0], 15, activity).returned == 0);
    activity[0].use = false;
    (void)p4_doom_arena_tick(&peers[0], 15, activity);
    activity[0].use = true;
    assert(p4_doom_arena_tick(&peers[0], 15, activity).returned == 1);
    assert(peers[0].players[0].visit == 2 && peers[0].players[0].kills == 0);
    assert(p4_doom_arena_kill(&peers[0], 0, 1));
    assert(p4_doom_arena_next_map(&peers[0]) == 7);
    assert(p4_doom_arena_next_map(&peers[0]) == 32);
    assert(p4_doom_arena_next_map(&peers[0]) == 1);
    assert(peers[0].players[0].kills == 1 && peers[0].players[0].visit == 2);
    assert(!peers[0].players[3].active);
    (void)p4_doom_arena_tick(&peers[0], 14, activity);
    assert(peers[0].players[0].kills == 0);
    activity[0].use = false;
    (void)p4_doom_arena_tick(&peers[0], 15, activity);
    activity[0].use = true;
    assert(p4_doom_arena_tick(&peers[0], 15, activity).returned == 0);
    assert(!p4_doom_arena_begin(&peers[0], 5, maps, 3));
    const uint8_t bad[] = {1, 1};
    assert(!p4_doom_arena_begin(&peers[0], 4, bad, 2));
    for (unsigned i=0; i<4; ++i) {
        assert(p4_doom_arena_begin_default(&peers[i],4));
        assert(peers[i].maps[peers[i].map_index]==1 && peers[i].map_count==5);
        assert(p4_doom_arena_kill(&peers[i],1,2));
    }
    for (unsigned exit=0; exit<20; ++exit) {
        for (unsigned i=0; i<4; ++i) {
            assert(p4_doom_arena_next_map(&peers[i])==((exit+1U)%5U+1U));
            assert(peers[i].players[1].kills==1 && peers[i].players[1].visit==1);
            assert(peers[i].connected_mask==15);
        }
        for (unsigned i=1; i<4; ++i) assert(!memcmp(&peers[0],&peers[i],sizeof(peers[0])));
    }
    return 0;
}
