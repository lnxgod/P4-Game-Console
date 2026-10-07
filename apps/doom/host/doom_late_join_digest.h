// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef P4_DOOM_REJOIN_DIGEST_H
#define P4_DOOM_REJOIN_DIGEST_H

/* Host-only observation. Hash explicit scalars in a fixed byte order, never
 * addresses, padding, renderer visitation counters, or UI/audio state. IDs are
 * one-based thinker-list ordinals (zero means NULL); execution and spatial-list
 * order have their own hash. Unknown references are reported, not dereferenced
 * or quietly treated as NULL. A proof must require valid=1. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "r_state.h"
#include "info.h"

extern int prndindex;
extern gameaction_t gameaction;
extern mobj_t *bodyque[32]; /* BODYQUESIZE is private to g_game.c. */
extern void T_FireFlicker(fireflicker_t *flick);
extern const p4_doom_arena_t *p4_doom_gc_test_state(void);

enum p4_rejoin_kind {
    P4_RJ_UNKNOWN, P4_RJ_ACTOR, P4_RJ_FLOOR, P4_RJ_CEILING,
    P4_RJ_DOOR, P4_RJ_PLAT, P4_RJ_FLASH, P4_RJ_STROBE, P4_RJ_GLOW,
    P4_RJ_FLICKER, P4_RJ_REMOVED, P4_RJ_CEILING_STOP, P4_RJ_PLAT_STOP
};
typedef struct {
    const void *pointer;
    uint32_t id;
    enum p4_rejoin_kind kind;
} p4_rejoin_id_t;
typedef struct {
    p4_rejoin_id_t *ids;
    size_t capacity;
    uint32_t count, actors, specials, removed, unresolved, unknown, malformed;
} p4_rejoin_digest_t;

static void p4_rejoin_hash_u64(uint64_t *hash, uint64_t value)
{
    for (unsigned byte_index = 0; byte_index < 8; ++byte_index) {
        *hash ^= value & UINT64_C(255);
        *hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
}
#define P4_RJ_HASH(hash, value) p4_rejoin_hash_u64(&(hash), (uint64_t)(value))

static size_t p4_rejoin_pointer_slot(const p4_rejoin_digest_t *digest,
                                    const void *pointer)
{
    /* Addresses are used only for local lookup, never in a digest. */
    const uintptr_t bits = (uintptr_t)pointer;
    return (size_t)((bits >> 4) * (uintptr_t)UINT64_C(11400714819323198485))
           & (digest->capacity - 1U);
}

static p4_rejoin_id_t *p4_rejoin_lookup(p4_rejoin_digest_t *digest,
                                       const void *pointer)
{
    size_t slot = p4_rejoin_pointer_slot(digest, pointer);
    for (size_t probe = 0; probe < digest->capacity; ++probe) {
        p4_rejoin_id_t *entry = &digest->ids[slot];
        if (!entry->pointer || entry->pointer == pointer) return entry;
        slot = (slot + 1U) & (digest->capacity - 1U);
    }
    return NULL;
}

static uint64_t p4_rejoin_ref(p4_rejoin_digest_t *digest, const void *pointer)
{
    if (!pointer) return 0;
    p4_rejoin_id_t *entry = p4_rejoin_lookup(digest, pointer);
    if (entry && entry->pointer) return entry->id;
    ++digest->unresolved;
    return UINT64_MAX;
}

/* Array membership is checked with equality; subtracting unrelated pointers
 * would be undefined behavior. Indices are one-based to distinguish NULL. */
#define P4_RJ_INDEX_FUNCTION(name, type, base, length) \
static uint64_t name(p4_rejoin_digest_t *digest, const type *pointer) \
{ \
    if (!pointer) return 0; \
    for (int array_index = 0; array_index < (int)(length); ++array_index) \
        if (pointer == &(base)[array_index]) return (uint64_t)array_index + 1U; \
    ++digest->unresolved; \
    return UINT64_MAX; \
}
P4_RJ_INDEX_FUNCTION(p4_rejoin_sector, sector_t, sectors, numsectors)
P4_RJ_INDEX_FUNCTION(p4_rejoin_line, line_t, lines, numlines)
P4_RJ_INDEX_FUNCTION(p4_rejoin_subsector, subsector_t, subsectors, numsubsectors)
P4_RJ_INDEX_FUNCTION(p4_rejoin_state, state_t, states, NUMSTATES)
P4_RJ_INDEX_FUNCTION(p4_rejoin_info, mobjinfo_t, mobjinfo, NUMMOBJTYPES)
P4_RJ_INDEX_FUNCTION(p4_rejoin_player, player_t, players, MAXPLAYERS)
#undef P4_RJ_INDEX_FUNCTION

static enum p4_rejoin_kind p4_rejoin_thinker_kind(const thinker_t *node)
{
    if (node->function.acv == (actionf_v)(-1)) return P4_RJ_REMOVED;
    if (node->function.acp1 == (actionf_p1)P_MobjThinker) return P4_RJ_ACTOR;
    if (node->function.acp1 == (actionf_p1)T_MoveFloor) return P4_RJ_FLOOR;
    if (node->function.acp1 == (actionf_p1)T_MoveCeiling) return P4_RJ_CEILING;
    if (node->function.acp1 == (actionf_p1)T_VerticalDoor) return P4_RJ_DOOR;
    if (node->function.acp1 == (actionf_p1)T_PlatRaise) return P4_RJ_PLAT;
    if (node->function.acp1 == (actionf_p1)T_LightFlash) return P4_RJ_FLASH;
    if (node->function.acp1 == (actionf_p1)T_StrobeFlash) return P4_RJ_STROBE;
    if (node->function.acp1 == (actionf_p1)T_Glow) return P4_RJ_GLOW;
    if (node->function.acp1 == (actionf_p1)T_FireFlicker) return P4_RJ_FLICKER;
    if (!node->function.acp1) {
        for (int slot = 0; slot < MAXCEILINGS; ++slot)
            if ((const void *)activeceilings[slot] == (const void *)node)
                return P4_RJ_CEILING_STOP;
        for (int slot = 0; slot < MAXPLATS; ++slot)
            if ((const void *)activeplats[slot] == (const void *)node)
                return P4_RJ_PLAT_STOP;
    }
    return P4_RJ_UNKNOWN;
}

static void p4_rejoin_hash_mapthing(uint64_t *hash, const mapthing_t *thing)
{
    p4_rejoin_hash_u64(hash, (uint64_t)thing->x);
    p4_rejoin_hash_u64(hash, (uint64_t)thing->y);
    p4_rejoin_hash_u64(hash, (uint64_t)thing->angle);
    p4_rejoin_hash_u64(hash, (uint64_t)thing->type);
    p4_rejoin_hash_u64(hash, (uint64_t)thing->options);
}

static void p4_rejoin_hash_actor(p4_rejoin_digest_t *digest, uint64_t *hash,
                                const mobj_t *actor, uint32_t id)
{
    uint64_t value = *hash;
    P4_RJ_HASH(value, id);
    P4_RJ_HASH(value, actor->x); P4_RJ_HASH(value, actor->y); P4_RJ_HASH(value, actor->z);
    P4_RJ_HASH(value, actor->angle); P4_RJ_HASH(value, actor->sprite); P4_RJ_HASH(value, actor->frame);
    P4_RJ_HASH(value, p4_rejoin_subsector(digest, actor->subsector));
    P4_RJ_HASH(value, actor->floorz); P4_RJ_HASH(value, actor->ceilingz);
    P4_RJ_HASH(value, actor->radius); P4_RJ_HASH(value, actor->height);
    P4_RJ_HASH(value, actor->momx); P4_RJ_HASH(value, actor->momy); P4_RJ_HASH(value, actor->momz);
    P4_RJ_HASH(value, actor->type); P4_RJ_HASH(value, p4_rejoin_info(digest, actor->info));
    P4_RJ_HASH(value, actor->tics); P4_RJ_HASH(value, p4_rejoin_state(digest, actor->state));
    P4_RJ_HASH(value, actor->flags); P4_RJ_HASH(value, actor->health);
    P4_RJ_HASH(value, actor->movedir); P4_RJ_HASH(value, actor->movecount);
    P4_RJ_HASH(value, p4_rejoin_ref(digest, actor->target));
    P4_RJ_HASH(value, actor->reactiontime); P4_RJ_HASH(value, actor->threshold);
    P4_RJ_HASH(value, p4_rejoin_player(digest, actor->player));
    P4_RJ_HASH(value, actor->lastlook);
    p4_rejoin_hash_mapthing(&value, &actor->spawnpoint);
    P4_RJ_HASH(value, p4_rejoin_ref(digest, actor->tracer));
    *hash = value;
}

static void p4_rejoin_hash_special(p4_rejoin_digest_t *digest, uint64_t *hash,
                                  const thinker_t *node, enum p4_rejoin_kind kind,
                                  uint32_t id)
{
    uint64_t value = *hash;
    P4_RJ_HASH(value, id); P4_RJ_HASH(value, kind);
    switch (kind) {
    case P4_RJ_FLOOR: {
        const floormove_t *special = (const floormove_t *)node;
        P4_RJ_HASH(value, p4_rejoin_sector(digest, special->sector));
        P4_RJ_HASH(value, special->type); P4_RJ_HASH(value, special->crush);
        P4_RJ_HASH(value, special->direction);
        if ((special->type == lowerAndChange && special->direction == -1) ||
            (special->type == donutRaise && special->direction == 1)) {
            P4_RJ_HASH(value, special->newspecial); P4_RJ_HASH(value, special->texture);
        }
        P4_RJ_HASH(value, special->floordestheight);
        P4_RJ_HASH(value, special->speed); break;
    }
    case P4_RJ_CEILING: case P4_RJ_CEILING_STOP: {
        const ceiling_t *special = (const ceiling_t *)node;
        P4_RJ_HASH(value, p4_rejoin_sector(digest, special->sector));
        P4_RJ_HASH(value, special->type);
        if (special->type != raiseToHighest) P4_RJ_HASH(value, special->bottomheight);
        if (special->type == raiseToHighest || special->type == crushAndRaise ||
            special->type == fastCrushAndRaise || special->type == silentCrushAndRaise)
            P4_RJ_HASH(value, special->topheight);
        P4_RJ_HASH(value, special->speed);
        P4_RJ_HASH(value, special->crush); P4_RJ_HASH(value, special->direction);
        P4_RJ_HASH(value, special->tag);
        if (special->direction == 0) P4_RJ_HASH(value, special->olddirection);
        break;
    }
    case P4_RJ_DOOR: {
        const vldoor_t *special = (const vldoor_t *)node;
        P4_RJ_HASH(value, p4_rejoin_sector(digest, special->sector));
        P4_RJ_HASH(value, special->type); P4_RJ_HASH(value, special->topheight);
        P4_RJ_HASH(value, special->speed); P4_RJ_HASH(value, special->direction);
        P4_RJ_HASH(value, special->topwait);
        if (special->direction == 0 || special->direction == 2)
            P4_RJ_HASH(value, special->topcountdown);
        break;
    }
    case P4_RJ_PLAT: case P4_RJ_PLAT_STOP: {
        const plat_t *special = (const plat_t *)node;
        P4_RJ_HASH(value, p4_rejoin_sector(digest, special->sector));
        P4_RJ_HASH(value, special->speed); P4_RJ_HASH(value, special->low);
        P4_RJ_HASH(value, special->high); P4_RJ_HASH(value, special->wait);
        P4_RJ_HASH(value, special->status);
        if (special->status == in_stasis) P4_RJ_HASH(value, special->oldstatus);
        if (special->status == waiting ||
            (special->status == in_stasis && special->oldstatus == waiting))
            P4_RJ_HASH(value, special->count);
        P4_RJ_HASH(value, special->crush);
        P4_RJ_HASH(value, special->tag); P4_RJ_HASH(value, special->type); break;
    }
    case P4_RJ_FLASH: {
        const lightflash_t *special = (const lightflash_t *)node;
        P4_RJ_HASH(value, p4_rejoin_sector(digest, special->sector));
        P4_RJ_HASH(value, special->count); P4_RJ_HASH(value, special->maxlight);
        P4_RJ_HASH(value, special->minlight); P4_RJ_HASH(value, special->maxtime);
        P4_RJ_HASH(value, special->mintime); break;
    }
    case P4_RJ_STROBE: {
        const strobe_t *special = (const strobe_t *)node;
        P4_RJ_HASH(value, p4_rejoin_sector(digest, special->sector));
        P4_RJ_HASH(value, special->count); P4_RJ_HASH(value, special->minlight);
        P4_RJ_HASH(value, special->maxlight); P4_RJ_HASH(value, special->darktime);
        P4_RJ_HASH(value, special->brighttime); break;
    }
    case P4_RJ_GLOW: {
        const glow_t *special = (const glow_t *)node;
        P4_RJ_HASH(value, p4_rejoin_sector(digest, special->sector));
        P4_RJ_HASH(value, special->minlight); P4_RJ_HASH(value, special->maxlight);
        P4_RJ_HASH(value, special->direction); break;
    }
    case P4_RJ_FLICKER: {
        const fireflicker_t *special = (const fireflicker_t *)node;
        P4_RJ_HASH(value, p4_rejoin_sector(digest, special->sector));
        P4_RJ_HASH(value, special->count); P4_RJ_HASH(value, special->maxlight);
        P4_RJ_HASH(value, special->minlight); break;
    }
    default: break;
    }
    *hash = value;
}

static uint64_t p4_rejoin_hash_players(p4_rejoin_digest_t *digest)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int slot = 0; slot < MAXPLAYERS; ++slot) {
        const player_t *player = &players[slot];
        P4_RJ_HASH(hash, slot); P4_RJ_HASH(hash, playeringame[slot]);
        P4_RJ_HASH(hash, p4_rejoin_ref(digest, player->mo));
        P4_RJ_HASH(hash, player->playerstate);
        P4_RJ_HASH(hash, player->cmd.forwardmove); P4_RJ_HASH(hash, player->cmd.sidemove);
        P4_RJ_HASH(hash, player->cmd.angleturn); P4_RJ_HASH(hash, player->cmd.chatchar);
        P4_RJ_HASH(hash, player->cmd.buttons);
        /* consistancy and the unused other-game cmd extensions are transport
         * bookkeeping, compared by the journal/runner rather than world state. */
        /* P_SetupLevel writes the local console's camera sentinel even when
         * that seat is absent. Rejoin spawns a new body and recomputes viewz. */
        if (player->mo) P4_RJ_HASH(hash, player->viewz);
        P4_RJ_HASH(hash, player->viewheight);
        P4_RJ_HASH(hash, player->deltaviewheight); P4_RJ_HASH(hash, player->bob);
        P4_RJ_HASH(hash, player->health); P4_RJ_HASH(hash, player->armorpoints);
        P4_RJ_HASH(hash, player->armortype);
        for (int index = 0; index < NUMPOWERS; ++index) P4_RJ_HASH(hash, player->powers[index]);
        for (int index = 0; index < NUMCARDS; ++index) P4_RJ_HASH(hash, player->cards[index]);
        P4_RJ_HASH(hash, player->backpack);
        for (int index = 0; index < MAXPLAYERS; ++index) P4_RJ_HASH(hash, player->frags[index]);
        P4_RJ_HASH(hash, player->readyweapon); P4_RJ_HASH(hash, player->pendingweapon);
        for (int index = 0; index < NUMWEAPONS; ++index) P4_RJ_HASH(hash, player->weaponowned[index]);
        for (int index = 0; index < NUMAMMO; ++index) {
            P4_RJ_HASH(hash, player->ammo[index]); P4_RJ_HASH(hash, player->maxammo[index]);
        }
        P4_RJ_HASH(hash, player->attackdown); P4_RJ_HASH(hash, player->usedown);
        P4_RJ_HASH(hash, player->cheats); P4_RJ_HASH(hash, player->refire);
        P4_RJ_HASH(hash, player->killcount); P4_RJ_HASH(hash, player->itemcount);
        P4_RJ_HASH(hash, player->secretcount); P4_RJ_HASH(hash, player->damagecount);
        P4_RJ_HASH(hash, player->bonuscount); P4_RJ_HASH(hash, p4_rejoin_ref(digest, player->attacker));
        P4_RJ_HASH(hash, player->extralight); P4_RJ_HASH(hash, player->fixedcolormap);
        P4_RJ_HASH(hash, player->colormap);
        for (int index = 0; index < NUMPSPRITES; ++index) {
            const pspdef_t *sprite = &player->psprites[index];
            P4_RJ_HASH(hash, p4_rejoin_state(digest, sprite->state));
            P4_RJ_HASH(hash, sprite->tics); P4_RJ_HASH(hash, sprite->sx); P4_RJ_HASH(hash, sprite->sy);
        }
        P4_RJ_HASH(hash, player->didsecret);
    }
    return hash;
}

static uint64_t p4_rejoin_hash_world(p4_rejoin_digest_t *digest)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    P4_RJ_HASH(hash, gamestate); P4_RJ_HASH(hash, gameaction);
    P4_RJ_HASH(hash, gameskill); P4_RJ_HASH(hash, gameepisode); P4_RJ_HASH(hash, gamemap);
    P4_RJ_HASH(hash, leveltime); P4_RJ_HASH(hash, levelstarttic); P4_RJ_HASH(hash, paused);
    P4_RJ_HASH(hash, deathmatch); P4_RJ_HASH(hash, netgame); P4_RJ_HASH(hash, respawnmonsters);
    P4_RJ_HASH(hash, nomonsters); P4_RJ_HASH(hash, respawnparm); P4_RJ_HASH(hash, fastparm);
    P4_RJ_HASH(hash, totalkills); P4_RJ_HASH(hash, totalitems); P4_RJ_HASH(hash, totalsecret);
    P4_RJ_HASH(hash, numsectors); P4_RJ_HASH(hash, numlines); P4_RJ_HASH(hash, numsides);
    for (int index = 0; index < numsectors; ++index) {
        const sector_t *sector = &sectors[index];
        P4_RJ_HASH(hash, sector->floorheight); P4_RJ_HASH(hash, sector->ceilingheight);
        P4_RJ_HASH(hash, sector->floorpic); P4_RJ_HASH(hash, sector->ceilingpic);
        P4_RJ_HASH(hash, sector->lightlevel); P4_RJ_HASH(hash, sector->special); P4_RJ_HASH(hash, sector->tag);
        P4_RJ_HASH(hash, p4_rejoin_ref(digest, sector->soundtarget));
        P4_RJ_HASH(hash, p4_rejoin_ref(digest, sector->specialdata));
    }
    for (int index = 0; index < numlines; ++index) {
        const line_t *line = &lines[index];
        P4_RJ_HASH(hash, line->flags); P4_RJ_HASH(hash, line->special); P4_RJ_HASH(hash, line->tag);
        P4_RJ_HASH(hash, p4_rejoin_ref(digest, line->specialdata));
    }
    for (int index = 0; index < numsides; ++index) {
        const side_t *side = &sides[index];
        P4_RJ_HASH(hash, side->textureoffset); P4_RJ_HASH(hash, side->rowoffset);
        P4_RJ_HASH(hash, side->toptexture); P4_RJ_HASH(hash, side->bottomtexture);
        P4_RJ_HASH(hash, side->midtexture); P4_RJ_HASH(hash, p4_rejoin_sector(digest, side->sector));
    }
    for (int index = 0; index < MAXPLAYERS; ++index) p4_rejoin_hash_mapthing(&hash, &playerstarts[index]);
    unsigned starts = 0;
    for (; starts <= MAX_DM_STARTS; ++starts)
        if (deathmatch_p == &deathmatchstarts[starts]) break;
    if (starts > MAX_DM_STARTS) { ++digest->malformed; starts = 0; }
    P4_RJ_HASH(hash, starts);
    for (unsigned index = 0; index < starts; ++index) p4_rejoin_hash_mapthing(&hash, &deathmatchstarts[index]);
    return hash;
}

static void p4_rejoin_hash_chain(p4_rejoin_digest_t *digest, uint64_t *hash,
                                const mobj_t *head, int block_chain)
{
    const mobj_t *actor = head;
    uint32_t length = 0;
    while (actor) {
        p4_rejoin_id_t *entry = p4_rejoin_lookup(digest, actor);
        if (!entry || !entry->pointer || entry->kind != P4_RJ_ACTOR || length >= digest->count) {
            ++digest->malformed;
            p4_rejoin_hash_u64(hash, UINT64_MAX);
            return;
        }
        p4_rejoin_hash_u64(hash, entry->id);
        actor = block_chain ? actor->bnext : actor->snext;
        ++length;
    }
    p4_rejoin_hash_u64(hash, 0);
    p4_rejoin_hash_u64(hash, length);
}

static uint64_t p4_rejoin_hash_arena(p4_rejoin_digest_t *digest)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const p4_doom_arena_t *state = p4_doom_gc_test_state();
    if (!state) { ++digest->malformed; return 0; }
    for (unsigned slot = 0; slot < P4_MP_MAX_PLAYERS; ++slot) {
        const p4_doom_arena_player_t *player = &state->players[slot];
        P4_RJ_HASH(hash, player->visit); P4_RJ_HASH(hash, player->kills);
        P4_RJ_HASH(hash, player->idle_tics); P4_RJ_HASH(hash, player->active);
        P4_RJ_HASH(hash, player->use_held);
        P4_RJ_HASH(hash, state->vote_opcode[slot]); P4_RJ_HASH(hash, state->vote_wait[slot]);
    }
    P4_RJ_HASH(hash, state->vote_tics); P4_RJ_HASH(hash, state->vote_cooldown);
    P4_RJ_HASH(hash, state->vote_map); P4_RJ_HASH(hash, state->vote_yes); P4_RJ_HASH(hash, state->vote_no);
    P4_RJ_HASH(hash, state->vote_generation); P4_RJ_HASH(hash, state->connected_mask);
    P4_RJ_HASH(hash, state->capacity);
    P4_RJ_HASH(hash, state->map_count); P4_RJ_HASH(hash, state->map_index);
    if (state->map_count > P4_DOOM_ARENA_MAX_MAPS) { ++digest->malformed; return hash; }
    for (unsigned index = 0; index < state->map_count; ++index) P4_RJ_HASH(hash, state->maps[index]);
    return hash;
}

static void p4_rejoin_write_state(FILE *stream, int tic)
{
    p4_rejoin_digest_t digest = {0};
    const thinker_t *node = thinkercap.next;
    /* Bound corrupt/cyclic thinker lists before allocating the lookup table. */
    while (node && node != &thinkercap && digest.count < 65536U) {
        ++digest.count;
        node = node->next;
    }
    if (!node || node != &thinkercap) {
        fprintf(stream, "STATE tick=%d valid=0 malformed=thinker-list\n", tic);
        return;
    }
    digest.capacity = 2;
    while (digest.capacity < (size_t)digest.count * 2U) digest.capacity *= 2U;
    digest.ids = calloc(digest.capacity, sizeof(*digest.ids));
    if (!digest.ids) {
        fprintf(stream, "STATE tick=%d valid=0 malformed=allocation\n", tic);
        return;
    }
    uint32_t ordinal = 0;
    for (node = thinkercap.next; node != &thinkercap; node = node->next) {
        p4_rejoin_id_t *entry = p4_rejoin_lookup(&digest, node);
        if (!entry || entry->pointer) { ++digest.malformed; break; }
        entry->pointer = node; entry->id = ++ordinal;
        entry->kind = p4_rejoin_thinker_kind(node);
        if (entry->kind == P4_RJ_ACTOR) ++digest.actors;
        else if (entry->kind == P4_RJ_REMOVED) ++digest.removed;
        else if (entry->kind == P4_RJ_UNKNOWN) ++digest.unknown;
        else ++digest.specials;
    }
    uint64_t actors_hash = UINT64_C(14695981039346656037);
    uint64_t specials_hash = UINT64_C(14695981039346656037);
    uint64_t order_hash = UINT64_C(14695981039346656037);
    P4_RJ_HASH(order_hash, digest.count);
    for (node = thinkercap.next; node != &thinkercap; node = node->next) {
        const p4_rejoin_id_t *entry = p4_rejoin_lookup(&digest, node);
        if (!entry || !entry->pointer) { ++digest.malformed; break; }
        P4_RJ_HASH(order_hash, entry->id); P4_RJ_HASH(order_hash, entry->kind);
        P4_RJ_HASH(order_hash, node->prev == &thinkercap ? 0 : p4_rejoin_ref(&digest, node->prev));
        P4_RJ_HASH(order_hash, node->next == &thinkercap ? 0 : p4_rejoin_ref(&digest, node->next));
        if (entry->kind == P4_RJ_ACTOR) {
            const mobj_t *actor = (const mobj_t *)node;
            p4_rejoin_hash_actor(&digest, &actors_hash, actor, entry->id);
            P4_RJ_HASH(order_hash, p4_rejoin_ref(&digest, actor->snext));
            P4_RJ_HASH(order_hash, p4_rejoin_ref(&digest, actor->sprev));
            P4_RJ_HASH(order_hash, p4_rejoin_ref(&digest, actor->bnext));
            P4_RJ_HASH(order_hash, p4_rejoin_ref(&digest, actor->bprev));
        } else {
            /* Tombstones are only read as thinker_t: their payload type is no
             * longer recoverable safely from the deleted function sentinel. */
            p4_rejoin_hash_special(&digest, &specials_hash, node, entry->kind, entry->id);
        }
    }
    for (int index = 0; index < numsectors; ++index) {
        P4_RJ_HASH(order_hash, index);
        p4_rejoin_hash_chain(&digest, &order_hash, sectors[index].thinglist, 0);
    }
    P4_RJ_HASH(order_hash, bmapwidth); P4_RJ_HASH(order_hash, bmapheight);
    P4_RJ_HASH(order_hash, bmaporgx); P4_RJ_HASH(order_hash, bmaporgy);
    if (bmapwidth < 0 || bmapheight < 0 ||
        (uint64_t)(unsigned)bmapwidth * (uint64_t)(unsigned)bmapheight > UINT64_C(1048576)) {
        ++digest.malformed;
    } else {
        const size_t blocks = (size_t)bmapwidth * (size_t)bmapheight;
        if (blocks && !blocklinks) ++digest.malformed;
        else for (size_t index = 0; index < blocks; ++index) {
            P4_RJ_HASH(order_hash, index);
            p4_rejoin_hash_chain(&digest, &order_hash, blocklinks[index], 1);
        }
    }
    P4_RJ_HASH(specials_hash, levelTimer);
    if (levelTimer) P4_RJ_HASH(specials_hash, levelTimeCount);
    for (int slot = 0; slot < MAXCEILINGS; ++slot) P4_RJ_HASH(specials_hash, p4_rejoin_ref(&digest, activeceilings[slot]));
    for (int slot = 0; slot < MAXPLATS; ++slot) P4_RJ_HASH(specials_hash, p4_rejoin_ref(&digest, activeplats[slot]));
    for (int slot = 0; slot < MAXBUTTONS; ++slot) {
        const button_t *button = &buttonlist[slot];
        P4_RJ_HASH(specials_hash, button->btimer);
        if (button->btimer) {
            P4_RJ_HASH(specials_hash, slot); P4_RJ_HASH(specials_hash, p4_rejoin_line(&digest, button->line));
            P4_RJ_HASH(specials_hash, button->where); P4_RJ_HASH(specials_hash, button->btexture);
            /* soundorg is an audio-only derived sector origin. */
        }
    }
    uint64_t queues_hash = UINT64_C(14695981039346656037);
    unsigned item_count = 0, body_count = 0;
    P4_RJ_HASH(queues_hash, iquehead); P4_RJ_HASH(queues_hash, iquetail);
    if (iquehead < 0 || iquehead >= ITEMQUESIZE || iquetail < 0 || iquetail >= ITEMQUESIZE) {
        ++digest.malformed;
    } else {
        for (int index = iquetail; index != iquehead; index = (index + 1) % ITEMQUESIZE) {
            p4_rejoin_hash_mapthing(&queues_hash, &itemrespawnque[index]);
            P4_RJ_HASH(queues_hash, itemrespawntime[index]); ++item_count;
        }
    }
    P4_RJ_HASH(queues_hash, item_count); P4_RJ_HASH(queues_hash, bodyqueslot);
    if (bodyqueslot < 0) ++digest.malformed;
    else {
        body_count = bodyqueslot < 32 ? (unsigned)bodyqueslot : 32U;
        const unsigned oldest = bodyqueslot < 32 ? 0U : (unsigned)bodyqueslot % 32U;
        for (unsigned index = 0; index < body_count; ++index)
            P4_RJ_HASH(queues_hash, p4_rejoin_ref(&digest, bodyque[(oldest + index) % 32U]));
    }
    const uint64_t world_hash = p4_rejoin_hash_world(&digest);
    const uint64_t players_hash = p4_rejoin_hash_players(&digest);
    const uint64_t arena_hash = p4_rejoin_hash_arena(&digest);
    const unsigned valid = digest.unresolved == 0 && digest.unknown == 0 && digest.malformed == 0 ? 1U : 0U;
    fprintf(stream, "STATE tick=%d world=%016" PRIx64 " actors=%016" PRIx64
            " players=%016" PRIx64 " specials=%016" PRIx64 " queues=%016" PRIx64
            " order=%016" PRIx64 " arena=%016" PRIx64
            " rng=%d,%d valid=%u thinkers=%" PRIu32 " actor_count=%" PRIu32
            " special_count=%" PRIu32 " removed=%" PRIu32 " items=%u bodies=%u"
            " unresolved=%" PRIu32 " unknown=%" PRIu32 " malformed=%" PRIu32 "\n",
            tic, world_hash, actors_hash, players_hash, specials_hash, queues_hash,
            order_hash, arena_hash, prndindex, rndindex, valid, digest.count, digest.actors,
            digest.specials, digest.removed, item_count, body_count, digest.unresolved,
            digest.unknown, digest.malformed);
    free(digest.ids);
}

#undef P4_RJ_HASH
#endif
