// SPDX-License-Identifier: MIT
#include "p4/doom_arena.h"
#include <assert.h>
#include <string.h>


static uint8_t command(p4_doom_arena_t *a,unsigned slot,uint8_t opcode,uint8_t generation)
{
    uint8_t chat[4]={0}; chat[slot]=opcode;
    assert(!p4_doom_arena_vote_tick(a,chat));
    chat[slot]=(uint8_t)(0x80U|generation);
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
        assert(!command(a,0,0x83,gen)); /* DWANGO 5 MAP01 */
        assert(a->vote_map==3 && a->vote_yes==1);
        assert(!command(a,1,0xf0,gen)); /* A 2/4 tie is not sufficient. */
        assert(a->vote_map==3);
        assert(!command(a,1,0xf1,gen)); /* One ballot per visit. */
        assert(!a->vote_no);
        assert(!command(a,2,0xf0,(uint8_t)(gen+1U))); /* stale/future token */
        assert(a->vote_yes==3);
        assert(command(a,2,0xf0,gen)==3);
        assert(p4_doom_arena_select(a,3));
        assert(a->map_count==24 && a->players[0].kills==1 && a->players[0].visit==1);
        for(unsigned map=2;map<=24;++map) assert(p4_doom_arena_map_number(p4_doom_arena_next_map(a))==map);
        assert(p4_doom_arena_next_map(a)==3); /* never MAP25 or the commercial finale */
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
        wait_vote(a,4); chat[0]=(uint8_t)(0x80U|a->vote_generation);
        assert(!p4_doom_arena_vote_tick(a,chat)); wait_vote(a,4);
        assert(!command(a,0,0x9b,a->vote_generation)); assert(!a->vote_map);
        gen=a->vote_generation; assert(!command(a,0,0x81,gen));
        assert(!command(a,1,0xf0,gen));
        /* A departure reduces eligible voters; all four simulations agree. */
        p4_doom_arena_activity_t activity[4]={{.moving=true},{.moving=true},{.moving=true},{0}};
        (void)p4_doom_arena_tick(a,7,activity);
        memset(chat,0,sizeof(chat)); assert(p4_doom_arena_vote_tick(a,chat)==1);
        assert(p4_doom_arena_select(a,1));
        assert(a->players[0].kills==1 && a->connected_mask==7 && a->map_count==2);
        wait_vote(a,P4_DOOM_ARENA_VOTE_COOLDOWN);
        /* Idle seats cannot propose, vote, or retain partial commands. */
        activity[0].moving=false;
        for(unsigned t=0;t<P4_DOOM_ARENA_IDLE_TICS;++t) (void)p4_doom_arena_tick(a,7,activity);
        assert(!command(a,0,0x83,a->vote_generation) && !a->vote_map);
        assert(!command(a,1,0x83,a->vote_generation));
        assert(command(a,2,0xf0,a->vote_generation)==3);
    }
    for(unsigned i=1;i<4;++i) assert(!memcmp(&peers[0],&peers[i],sizeof(peers[0])));
    assert(!p4_doom_arena_begin_selected(&peers[0],4,27));
}
int main(void)
{
    vote_tests();
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
        assert(peers[i].maps[peers[i].map_index]==1 && peers[i].map_count==2);
        assert(p4_doom_arena_kill(&peers[i],1,2));
    }
    for (unsigned exit=0; exit<20; ++exit) {
        for (unsigned i=0; i<4; ++i) {
            assert(p4_doom_arena_next_map(&peers[i])==(exit%2?1:2));
            assert(peers[i].players[1].kills==1 && peers[i].players[1].visit==1);
            assert(peers[i].connected_mask==15);
        }
        for (unsigned i=1; i<4; ++i) assert(!memcmp(&peers[0],&peers[i],sizeof(peers[0])));
    }
    return 0;
}
