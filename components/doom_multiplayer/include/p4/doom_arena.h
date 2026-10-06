// SPDX-License-Identifier: MIT
#ifndef P4_DOOM_ARENA_H
#define P4_DOOM_ARENA_H
#include <stdbool.h>
#include <stdint.h>
#include "p4/doom_multiplayer.h"

enum { P4_DOOM_ARENA_IDLE_TICS = 15 * P4_DOOM_MP_TICK_RATE_HZ,
       P4_DOOM_ARENA_MAX_MAPS = 32, P4_DOOM_ARENA_SELECTIONS = 26,
       P4_DOOM_ARENA_VOTE_TICS = 20 * P4_DOOM_MP_TICK_RATE_HZ,
       P4_DOOM_ARENA_VOTE_COOLDOWN = 5 * P4_DOOM_MP_TICK_RATE_HZ };
typedef struct {
    uint32_t visit, kills;
    uint16_t idle_tics;
    bool active, use_held;
} p4_doom_arena_player_t;
typedef struct {
    p4_doom_arena_player_t players[P4_MP_MAX_PLAYERS];
    uint16_t vote_tics, vote_cooldown;
    uint8_t vote_map, vote_yes, vote_no, vote_generation;
    uint8_t vote_opcode[P4_MP_MAX_PLAYERS], vote_wait[P4_MP_MAX_PLAYERS];
    uint8_t connected_mask, maps[P4_DOOM_ARENA_MAX_MAPS], map_count, map_index;
} p4_doom_arena_t;
typedef struct { bool moving, firing, use; } p4_doom_arena_activity_t;
typedef struct { uint8_t entered_break, returned; } p4_doom_arena_transition_t;

bool p4_doom_arena_begin(p4_doom_arena_t *, uint8_t players,
                        const uint8_t *maps, uint8_t map_count);
/* Pure Hell v0.5: always start with shotguns, then rockets, repeating. */
bool p4_doom_arena_begin_default(p4_doom_arena_t *, uint8_t players);
/* Selection 1/2: Pure Hell; 3..26: DWANGO 5 MAP01..24. */
bool p4_doom_arena_begin_selected(p4_doom_arena_t *, uint8_t players, uint8_t selection);
bool p4_doom_arena_select(p4_doom_arena_t *, uint8_t selection);
uint8_t p4_doom_arena_map_number(uint8_t selection);
const char *p4_doom_arena_label(uint8_t selection);
uint8_t p4_doom_arena_active_mask(const p4_doom_arena_t *);
/* Two synchronized chat bytes: opcode then 0x80 | generation. Returns a
 * majority-approved selection, otherwise zero. Called once per game tic. */
uint8_t p4_doom_arena_vote_tick(p4_doom_arena_t *, const uint8_t chat[P4_MP_MAX_PLAYERS]);
/* Exactly one call per simulated, unpaused level tic on every engine. */
p4_doom_arena_transition_t p4_doom_arena_tick(p4_doom_arena_t *,
    uint8_t connected_mask, const p4_doom_arena_activity_t activity[P4_MP_MAX_PLAYERS]);
/* Player kills only; suicides/environment deaths are not positive kills. */
bool p4_doom_arena_kill(p4_doom_arena_t *, uint8_t killer, uint8_t victim);
/* Arena exits preserve active visits and scores, and grant a fresh idle window. */
uint8_t p4_doom_arena_next_map(p4_doom_arena_t *);
#endif
