// SPDX-License-Identifier: MIT
#include "p4/doom_arena.h"
#include <limits.h>
#include <string.h>

static bool begin_mask(p4_doom_arena_t *arena, uint8_t players, uint8_t mask,
                       const uint8_t *maps, uint8_t count)
{
    if (!arena || !maps || players < 2 || players > P4_MP_MAX_PLAYERS ||
        !(mask & 1U) || (mask & ~((1U << players) - 1U)) ||
        count == 0 || count > P4_DOOM_ARENA_MAX_MAPS) return false;
    for (uint8_t i = 0; i < count; ++i) {
        if (maps[i] == 0 || maps[i] > 32) return false;
        for (uint8_t j = 0; j < i; ++j) if (maps[j] == maps[i]) return false;
    }
    memset(arena, 0, sizeof(*arena));
    arena->capacity = players;
    arena->connected_mask = mask;
    arena->map_count = count;
    memcpy(arena->maps, maps, count);
    for (uint8_t i = 0; i < players; ++i) {
        if (!(mask & (1U << i))) continue;
        arena->players[i].active = true;
        arena->players[i].visit = 1;
    }
    return true;
}

bool p4_doom_arena_begin(p4_doom_arena_t *arena, uint8_t players,
                        const uint8_t *maps, uint8_t count)
{
    if (players < 2 || players > P4_MP_MAX_PLAYERS) return false;
    return begin_mask(arena, players, (uint8_t)((1U << players) - 1U), maps, count);
}

p4_doom_arena_transition_t p4_doom_arena_tick(p4_doom_arena_t *arena,
    uint8_t mask, const p4_doom_arena_activity_t activity[P4_MP_MAX_PLAYERS])
{
    p4_doom_arena_transition_t result = {0};
    if (!arena || !activity || !arena->map_count) return result;
    /* The connected-break fallback never admits a new network member. */
    arena->connected_mask &= mask;
    for (uint8_t i = 0; i < P4_MP_MAX_PLAYERS; ++i) {
        p4_doom_arena_player_t *p = &arena->players[i];
        const uint8_t bit = (uint8_t)(1U << i);
        const bool connected = (arena->connected_mask & bit) != 0;
        const bool use_edge = activity[i].use && !p->use_held;
        p->use_held = activity[i].use;
        if (!connected) {
            if (p->active) result.entered_break |= bit;
            p->active = false;
            p->kills = 0;
            p->idle_tics = 0;
        } else if (!p->active) {
            if (use_edge && p->visit != UINT32_MAX) {
                ++p->visit;
                p->kills = 0;
                p->idle_tics = 0;
                p->active = true;
                result.returned |= bit;
            }
        } else if (activity[i].moving || activity[i].firing) {
            p->idle_tics = 0;
        } else if (++p->idle_tics >= P4_DOOM_ARENA_IDLE_TICS) {
            p->active = false;
            p->kills = 0;
            result.entered_break |= bit;
        }
    }
    return result;
}

bool p4_doom_arena_kill(p4_doom_arena_t *arena, uint8_t killer, uint8_t victim)
{
    if (!arena || killer >= P4_MP_MAX_PLAYERS || victim >= P4_MP_MAX_PLAYERS ||
        killer == victim || !arena->players[killer].active ||
        !arena->players[victim].active) return false;
    if (arena->players[killer].kills != UINT32_MAX) ++arena->players[killer].kills;
    return true;
}

bool p4_doom_arena_activate(p4_doom_arena_t *arena, uint8_t slot)
{
    if (!arena || !arena->map_count || slot == 0 || slot >= arena->capacity ||
        arena->capacity > P4_MP_MAX_PLAYERS)
        return false;
    p4_doom_arena_player_t *p = &arena->players[slot];
    const uint8_t bit = (uint8_t)(1U << slot);
    if (p->visit == UINT32_MAX || (arena->connected_mask & bit))
        return false;
    ++p->visit;
    p->kills = 0;
    p->idle_tics = 0;
    p->active = true;
    p->use_held = false;
    arena->connected_mask |= bit;
    arena->vote_yes &= (uint8_t)~bit;
    arena->vote_no &= (uint8_t)~bit;
    arena->vote_opcode[slot] = arena->vote_wait[slot] = 0;
    return true;
}

bool p4_doom_arena_rejoin(p4_doom_arena_t *arena, uint8_t slot)
{
    if (!arena || slot >= P4_MP_MAX_PLAYERS || !arena->players[slot].visit)
        return false;
    return p4_doom_arena_activate(arena, slot);
}

uint8_t p4_doom_arena_next_map(p4_doom_arena_t *arena)
{
    if (!arena || !arena->map_count || arena->map_count > P4_DOOM_ARENA_MAX_MAPS)
        return 0;
    arena->vote_map=0; arena->vote_tics=0;
    arena->vote_generation=(uint8_t)((arena->vote_generation+1U)&63U);
    memset(arena->vote_wait,0,sizeof(arena->vote_wait));
    arena->map_index = (uint8_t)((arena->map_index + 1U) % arena->map_count);
    for (uint8_t i = 0; i < P4_MP_MAX_PLAYERS; ++i) arena->players[i].idle_tics = 0;
    return arena->maps[arena->map_index];
}
bool p4_doom_arena_begin_default(p4_doom_arena_t *arena, uint8_t players)
{
    static const uint8_t maps[] = {1, 2, 3, 4, 5};
    return p4_doom_arena_begin(arena, players, maps, sizeof(maps));
}

uint8_t p4_doom_arena_map_number(uint8_t selection)
{
    return selection>=1 && selection<=P4_DOOM_ARENA_SELECTIONS ? (selection<=5 ? selection : (uint8_t)(selection-5U)) : 0;
}

const char *p4_doom_arena_label(uint8_t selection)
{
    static const char *const labels[]={"INVALID ARENA", "PURE HADES: SHOTGUNS", "PURE HADES: ROCKETS",
        "PURE HADES: PLASMA", "PURE HADES: PURE CHAOS", "PURE HADES: DOUBLE BARREL",
        "DWANGO 5 MAP01", "DWANGO 5 MAP02", "DWANGO 5 MAP03", "DWANGO 5 MAP04",
        "DWANGO 5 MAP05", "DWANGO 5 MAP06", "DWANGO 5 MAP07", "DWANGO 5 MAP08",
        "DWANGO 5 MAP09", "DWANGO 5 MAP10", "DWANGO 5 MAP11", "DWANGO 5 MAP12",
        "DWANGO 5 MAP13", "DWANGO 5 MAP14", "DWANGO 5 MAP15", "DWANGO 5 MAP16",
        "DWANGO 5 MAP17", "DWANGO 5 MAP18", "DWANGO 5 MAP19", "DWANGO 5 MAP20",
        "DWANGO 5 MAP21", "DWANGO 5 MAP22", "DWANGO 5 MAP23", "DWANGO 5 MAP24"};
    return labels[selection<=P4_DOOM_ARENA_SELECTIONS ? selection : 0];
}

bool p4_doom_arena_select(p4_doom_arena_t *a,uint8_t selection)
{
    if (!a || !p4_doom_arena_map_number(selection)) return false;
    a->map_count=selection<=5 ? 5 : 24;
    for (uint8_t i=0;i<a->map_count;++i) a->maps[i]=(uint8_t)(i+(selection<=5 ? 1U : 6U));
    a->map_index=(uint8_t)(selection-(selection<=5 ? 1U : 6U));
    a->vote_map=0; a->vote_tics=0;
    a->vote_generation=(uint8_t)((a->vote_generation+1U)&63U);
    memset(a->vote_wait,0,sizeof(a->vote_wait));
    for (unsigned i=0;i<P4_MP_MAX_PLAYERS;++i) a->players[i].idle_tics=0;
    return true;
}

bool p4_doom_arena_begin_selected(p4_doom_arena_t *a,uint8_t players,uint8_t selection)
{
    if (!p4_doom_arena_map_number(selection) || !p4_doom_arena_begin_default(a,players)) return false;
    return p4_doom_arena_select(a,selection);
}

bool p4_doom_arena_begin_selected_mask(p4_doom_arena_t *a, uint8_t capacity,
                                     uint8_t mask, uint8_t selection)
{
    static const uint8_t maps[] = {1, 2, 3, 4, 5};
    if (!p4_doom_arena_map_number(selection) ||
        !begin_mask(a, capacity, mask, maps, sizeof(maps))) return false;
    return p4_doom_arena_select(a, selection);
}

uint8_t p4_doom_arena_active_mask(const p4_doom_arena_t *a)
{
    uint8_t mask=0;
    for (unsigned i=0;i<P4_MP_MAX_PLAYERS;++i)
        if (a->players[i].active && (a->connected_mask & (1U<<i))) mask|=(uint8_t)(1U<<i);
    return mask;
}

static unsigned bit_count(uint8_t mask)
{
    unsigned count=0;
    for (;mask;mask>>=1) count+=mask&1U;
    return count;
}

uint8_t p4_doom_arena_vote_tick(p4_doom_arena_t *a,const uint8_t chat[P4_MP_MAX_PLAYERS])
{
    if (!a || !chat || !a->map_count) return 0;
    if (a->vote_cooldown) --a->vote_cooldown;
    const uint8_t active=p4_doom_arena_active_mask(a);
    /* Lost/break seats cannot retain a vote or partial command. */
    a->vote_yes &= active; a->vote_no &= active;
    for (unsigned i=0;i<P4_MP_MAX_PLAYERS;++i) {
        const uint8_t bit=(uint8_t)(1U<<i), c=chat[i];
        if (!(active&bit)) { a->vote_wait[i]=0; continue; }
        if (a->vote_wait[i]) {
            --a->vote_wait[i];
            if (!c) continue;
            const uint8_t op=a->vote_opcode[i];
            a->vote_wait[i]=0;
            if (c!=(uint8_t)(0xa0U+a->vote_generation)) continue;
            if (op>=0x81 && op<=0x9d && !a->vote_map && !a->vote_cooldown) {
                const uint8_t selection=(uint8_t)(op-0x80U);
                if (selection==a->maps[a->map_index]) continue;
                a->vote_map=selection; a->vote_tics=P4_DOOM_ARENA_VOTE_TICS;
                a->vote_yes=bit; a->vote_no=0;
            } else if (a->vote_map && (op==0xf0 || op==0xf1) && !((a->vote_yes|a->vote_no)&bit)) {
                if (op==0xf0) a->vote_yes|=bit; else a->vote_no|=bit;
            }
        } else if ((c>=0x81 && c<=0x9d) || c==0xf0 || c==0xf1) {
            a->vote_opcode[i]=c; a->vote_wait[i]=3;
        }
    }
    if (!a->vote_map) return 0;
    const unsigned eligible=bit_count(active), yes=bit_count(a->vote_yes), no=bit_count(a->vote_no);
    const bool passed=eligible>0 && yes>eligible/2U;
    if (passed || !eligible || no>=(eligible+1U)/2U || --a->vote_tics==0) {
        const uint8_t result=passed ? a->vote_map : 0;
        a->vote_map=0; a->vote_tics=0; a->vote_cooldown=P4_DOOM_ARENA_VOTE_COOLDOWN;
        a->vote_generation=(uint8_t)((a->vote_generation+1U)&63U);
        memset(a->vote_wait,0,sizeof(a->vote_wait));
        return result;
    }
    return 0;
}
