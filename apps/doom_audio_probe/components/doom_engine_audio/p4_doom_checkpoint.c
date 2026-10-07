// SPDX-License-Identifier: GPL-2.0-or-later
/* Schema 2 uses explicit little-endian 64-bit scalars and stable IDs, never
 * native structs or pointers. Captures represent the next unexecuted tic. */
#include "p4_doom_checkpoint.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "p_local.h"
#include "info.h"
#include "r_sky.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "z_zone.h"
#include "s_sound.h"
#include "r_main.h"
#include "r_state.h"
#include "d_items.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
extern int prndindex;
extern gameaction_t gameaction;
extern mobj_t *bodyque[32];
extern void T_FireFlicker(fireflicker_t *flick);
enum p4_ck_kind {
    P4_CK_KIND_UNKNOWN, P4_CK_KIND_ACTOR, P4_CK_KIND_FLOOR, P4_CK_KIND_CEILING,
    P4_CK_KIND_DOOR, P4_CK_KIND_PLAT, P4_CK_KIND_FLASH, P4_CK_KIND_STROBE, P4_CK_KIND_GLOW,
    P4_CK_KIND_FLICKER, P4_CK_KIND_REMOVED, P4_CK_KIND_CEILING_STOP, P4_CK_KIND_PLAT_STOP
};
static enum p4_ck_kind p4_ck_thinker_kind(const thinker_t *node)
{
    if (node->function.acv == (actionf_v)(-1)) return P4_CK_KIND_REMOVED;
    if (node->function.acp1 == (actionf_p1)P_MobjThinker) return P4_CK_KIND_ACTOR;
    if (node->function.acp1 == (actionf_p1)T_MoveFloor) return P4_CK_KIND_FLOOR;
    if (node->function.acp1 == (actionf_p1)T_MoveCeiling) return P4_CK_KIND_CEILING;
    if (node->function.acp1 == (actionf_p1)T_VerticalDoor) return P4_CK_KIND_DOOR;
    if (node->function.acp1 == (actionf_p1)T_PlatRaise) return P4_CK_KIND_PLAT;
    if (node->function.acp1 == (actionf_p1)T_LightFlash) return P4_CK_KIND_FLASH;
    if (node->function.acp1 == (actionf_p1)T_StrobeFlash) return P4_CK_KIND_STROBE;
    if (node->function.acp1 == (actionf_p1)T_Glow) return P4_CK_KIND_GLOW;
    if (node->function.acp1 == (actionf_p1)T_FireFlicker) return P4_CK_KIND_FLICKER;
    if (!node->function.acp1) {
        for (int slot = 0; slot < MAXCEILINGS; ++slot)
            if ((const void *)activeceilings[slot] == (const void *)node)
                return P4_CK_KIND_CEILING_STOP;
        for (int slot = 0; slot < MAXPLATS; ++slot)
            if ((const void *)activeplats[slot] == (const void *)node)
                return P4_CK_KIND_PLAT_STOP;
    }
    return P4_CK_KIND_UNKNOWN;
}

extern boolean D_P4CheckpointRebase(int, unsigned int);
enum { P4_CK_MAX_LINEANIMS = 64 }; /* p_spec.c private bound */
extern short numlinespecials;
extern line_t *linespeciallist[P4_CK_MAX_LINEANIMS];
extern gamestate_t oldgamestate;
extern int numtextures, numflats;
extern byte consistancy[MAXPLAYERS][BACKUPTICS];
enum { P4_CK_MAX_THINKERS = 4096, P4_CK_MAX_BYTES = 512 * 1024 };
typedef struct {
    uint8_t *bytes;
    size_t capacity, position;
    int reading, failed;
    uint32_t count;
    thinker_t *nodes[P4_CK_MAX_THINKERS];
    enum p4_ck_kind kinds[P4_CK_MAX_THINKERS];
    uint8_t sector_seen[P4_CK_MAX_THINKERS], block_seen[P4_CK_MAX_THINKERS];
} p4_ck_t;
static const char *p4_ck_error = "ok";
const char *P4_DoomCheckpointReason(void) { return p4_ck_error; }
static p4_ck_t *p4_ck_scratch;
static char p4_ck_content[P4_DOOM_CHECKPOINT_CONTENT_MAX + 1U];

bool P4_DoomCheckpointInit(void)
{
    if (p4_ck_scratch) return true;
#ifdef ESP_PLATFORM
    p4_ck_scratch = heap_caps_calloc(1, sizeof(*p4_ck_scratch),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    p4_ck_scratch = calloc(1, sizeof(*p4_ck_scratch));
#endif
    p4_ck_error = p4_ck_scratch ? "ok" : "allocation";
    return p4_ck_scratch != NULL;
}
void P4_DoomCheckpointShutdown(void)
{
#ifdef ESP_PLATFORM
    heap_caps_free(p4_ck_scratch);
#else
    free(p4_ck_scratch);
#endif
    p4_ck_scratch = NULL;
    memset(p4_ck_content, 0, sizeof(p4_ck_content));
    p4_ck_error = "not-initialized";
}
bool P4_DoomCheckpointSetContentIdentity(const char *identity)
{
    size_t length = 0;
    if (identity)
        while (length <= P4_DOOM_CHECKPOINT_CONTENT_MAX && identity[length]) ++length;
    if (!identity || !length || length > P4_DOOM_CHECKPOINT_CONTENT_MAX) {
        p4_ck_content[0] = 0;
        p4_ck_error = "content-identity-bound";
        return false;
    }
    memcpy(p4_ck_content, identity, length + 1U);
    p4_ck_error = "ok";
    return true;
}
static p4_ck_t *p4_ck_begin(void)
{
    if (!p4_ck_scratch) { p4_ck_error = "not-initialized"; return NULL; }
    if (!p4_ck_content[0]) { p4_ck_error = "content-identity-unset"; return NULL; }
    memset(p4_ck_scratch, 0, sizeof(*p4_ck_scratch));
    return p4_ck_scratch;
}
static void p4_ck_fail(p4_ck_t *c, const char *why) {
    if (!c->failed) p4_ck_error = why;
    c->failed = 1;
}
static uint64_t p4_ck_u64(p4_ck_t *c, uint64_t value) {
    if (c->failed) return 0;
    if (c->position > c->capacity || c->capacity - c->position < 8) {
        p4_ck_fail(c, c->reading ? "truncated" : "capacity"); return 0;
    }
    if (c->reading) {
        value = 0;
        for (unsigned i = 0; i < 8; ++i)
            value |= (uint64_t)c->bytes[c->position++] << (i * 8);
    } else {
        const uint64_t result = value;
        for (unsigned i = 0; i < 8; ++i) {
            c->bytes[c->position++] = (uint8_t)value; value >>= 8;
        }
        value = result;
    }
    return value;
}
#define P4_CK_SCALAR(c, field) do { \
    uint64_t p4_ck_raw_ = p4_ck_u64((c), (uint64_t)(field)); \
    if ((c)->reading && !(c)->failed) { \
        __typeof__(field) p4_ck_value_ = (__typeof__(field))p4_ck_raw_; \
        if ((uint64_t)p4_ck_value_ != p4_ck_raw_) p4_ck_fail((c), "scalar-range"); \
        else (field) = p4_ck_value_; \
    } \
} while (0)
static int p4_ck_kind_matches(enum p4_ck_kind actual, int expected) {
    if (!expected || actual == (enum p4_ck_kind)expected) return 1;
    return (expected == P4_CK_KIND_CEILING && actual == P4_CK_KIND_CEILING_STOP)
        || (expected == P4_CK_KIND_PLAT && actual == P4_CK_KIND_PLAT_STOP);
}
static void *p4_ck_ref(p4_ck_t *c, const void *pointer, int expected) {
    uint64_t id = 0;
    if (!c->reading && pointer) {
        for (uint32_t i = 0; i < c->count; ++i) if (c->nodes[i] == pointer) { id = i + 1U; break; }
        if (!id) { p4_ck_fail(c, "unresolved-reference"); return NULL; }
    }
    id = p4_ck_u64(c, id);
    if (id > c->count || (id && !p4_ck_kind_matches(c->kinds[id - 1], expected))) {
        p4_ck_fail(c, "reference-type-or-bound"); return NULL;
    }
    return id ? c->nodes[id - 1] : NULL;
}
#define P4_CK_REF(c,field,kind) do { \
    void *p4_ck_pointer_ = p4_ck_ref((c), (field), (kind)); \
    if ((c)->reading && !(c)->failed) (field) = (__typeof__(field))p4_ck_pointer_; \
} while (0)
static void *p4_ck_index(p4_ck_t *c, const void *pointer, void *base, size_t stride, size_t count) {
    uint64_t id = 0;
    if (!c->reading && pointer) {
        for (size_t i = 0; i < count; ++i)
            if (pointer == (uint8_t *)base + i * stride) { id = i + 1U; break; }
        if (!id) { p4_ck_fail(c, "unresolved-array-reference"); return NULL; }
    }
    id = p4_ck_u64(c, id);
    if (id > count) { p4_ck_fail(c, "array-reference-bound"); return NULL; }
    return id ? (uint8_t *)base + (id - 1U) * stride : NULL;
}
#define P4_CK_INDEX(c,field,base,count) do { \
    void *p4_ck_pointer_ = p4_ck_index((c), (field), (base), sizeof(*(base)), (size_t)(count)); \
    if ((c)->reading && !(c)->failed) (field) = (__typeof__(field))p4_ck_pointer_; \
} while (0)
static void p4_ck_mapthing(p4_ck_t *c, mapthing_t *thing) {
    P4_CK_SCALAR(c,thing->x); P4_CK_SCALAR(c,thing->y);
    P4_CK_SCALAR(c,thing->angle); P4_CK_SCALAR(c,thing->type); P4_CK_SCALAR(c,thing->options);
}
#include "p4_doom_checkpoint_thinkers.h"
static uint64_t p4_ck_hash(const uint8_t *bytes, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}
static void p4_ck_expect(p4_ck_t *c, uint64_t expected, const char *reason) {
    if (p4_ck_u64(c, expected) != expected) p4_ck_fail(c, reason);
}
static size_t p4_ck_kind_size(enum p4_ck_kind kind) {
    switch (kind) {
    case P4_CK_KIND_ACTOR: return sizeof(mobj_t);
    case P4_CK_KIND_FLOOR: return sizeof(floormove_t);
    case P4_CK_KIND_CEILING: case P4_CK_KIND_CEILING_STOP: return sizeof(ceiling_t);
    case P4_CK_KIND_DOOR: return sizeof(vldoor_t);
    case P4_CK_KIND_PLAT: case P4_CK_KIND_PLAT_STOP: return sizeof(plat_t);
    case P4_CK_KIND_FLASH: return sizeof(lightflash_t);
    case P4_CK_KIND_STROBE: return sizeof(strobe_t);
    case P4_CK_KIND_GLOW: return sizeof(glow_t);
    case P4_CK_KIND_FLICKER: return sizeof(fireflicker_t);
    default: return 0;
    }
}
static actionf_p1 p4_ck_kind_function(enum p4_ck_kind kind) {
    switch (kind) {
    case P4_CK_KIND_ACTOR: return (actionf_p1)P_MobjThinker;
    case P4_CK_KIND_FLOOR: return (actionf_p1)T_MoveFloor;
    case P4_CK_KIND_CEILING: return (actionf_p1)T_MoveCeiling;
    case P4_CK_KIND_DOOR: return (actionf_p1)T_VerticalDoor;
    case P4_CK_KIND_PLAT: return (actionf_p1)T_PlatRaise;
    case P4_CK_KIND_FLASH: return (actionf_p1)T_LightFlash;
    case P4_CK_KIND_STROBE: return (actionf_p1)T_StrobeFlash;
    case P4_CK_KIND_GLOW: return (actionf_p1)T_Glow;
    case P4_CK_KIND_FLICKER: return (actionf_p1)T_FireFlicker;
    default: return NULL;
    }
}
static void p4_ck_arena(p4_ck_t *c, p4_doom_arena_t *state) {
    for (unsigned i = 0; i < P4_MP_MAX_PLAYERS; ++i) {
        P4_CK_SCALAR(c,state->players[i].visit); P4_CK_SCALAR(c,state->players[i].kills);
        P4_CK_SCALAR(c,state->players[i].idle_tics); P4_CK_SCALAR(c,state->players[i].active);
        P4_CK_SCALAR(c,state->players[i].use_held); P4_CK_SCALAR(c,state->vote_opcode[i]);
        P4_CK_SCALAR(c,state->vote_wait[i]);
    }
    P4_CK_SCALAR(c,state->vote_tics); P4_CK_SCALAR(c,state->vote_cooldown);
    P4_CK_SCALAR(c,state->vote_map); P4_CK_SCALAR(c,state->vote_yes); P4_CK_SCALAR(c,state->vote_no);
    P4_CK_SCALAR(c,state->vote_generation); P4_CK_SCALAR(c,state->connected_mask); P4_CK_SCALAR(c,state->capacity);
    P4_CK_SCALAR(c,state->map_count); P4_CK_SCALAR(c,state->map_index);
    if (!state->map_count || state->map_count > P4_DOOM_ARENA_MAX_MAPS || state->map_index >= state->map_count ||
        !state->capacity || state->capacity > MAXPLAYERS || state->vote_generation > 63 ||
        (state->connected_mask & ~((1U << state->capacity) - 1U))) {
        p4_ck_fail(c,"arena-range"); return;
    }
    for (unsigned i = 0; i < state->map_count; ++i) {
        P4_CK_SCALAR(c,state->maps[i]);
        if (!state->maps[i] || state->maps[i] > P4_DOOM_ARENA_SELECTIONS) p4_ck_fail(c,"arena-map");
    }
}
static void p4_ck_players(p4_ck_t *c) {
    for (int slot = 0; slot < MAXPLAYERS; ++slot) {
        player_t *p = &players[slot];
        P4_CK_SCALAR(c,playeringame[slot]); P4_CK_REF(c,p->mo,P4_CK_KIND_ACTOR);
        P4_CK_SCALAR(c,p->playerstate);
        P4_CK_SCALAR(c,p->cmd.forwardmove); P4_CK_SCALAR(c,p->cmd.sidemove);
        P4_CK_SCALAR(c,p->cmd.angleturn); P4_CK_SCALAR(c,p->cmd.consistancy);
        P4_CK_SCALAR(c,p->cmd.chatchar); P4_CK_SCALAR(c,p->cmd.buttons);
        P4_CK_SCALAR(c,p->viewz); P4_CK_SCALAR(c,p->viewheight);
        P4_CK_SCALAR(c,p->deltaviewheight); P4_CK_SCALAR(c,p->bob);
        P4_CK_SCALAR(c,p->health); P4_CK_SCALAR(c,p->armorpoints); P4_CK_SCALAR(c,p->armortype);
        for (int i = 0; i < NUMPOWERS; ++i) P4_CK_SCALAR(c,p->powers[i]);
        for (int i = 0; i < NUMCARDS; ++i) P4_CK_SCALAR(c,p->cards[i]);
        P4_CK_SCALAR(c,p->backpack);
        for (int i = 0; i < MAXPLAYERS; ++i) P4_CK_SCALAR(c,p->frags[i]);
        P4_CK_SCALAR(c,p->readyweapon); P4_CK_SCALAR(c,p->pendingweapon);
        for (int i = 0; i < NUMWEAPONS; ++i) P4_CK_SCALAR(c,p->weaponowned[i]);
        for (int i = 0; i < NUMAMMO; ++i) { P4_CK_SCALAR(c,p->ammo[i]); P4_CK_SCALAR(c,p->maxammo[i]); }
        P4_CK_SCALAR(c,p->attackdown); P4_CK_SCALAR(c,p->usedown);
        P4_CK_SCALAR(c,p->cheats); P4_CK_SCALAR(c,p->refire);
        P4_CK_SCALAR(c,p->killcount); P4_CK_SCALAR(c,p->itemcount); P4_CK_SCALAR(c,p->secretcount);
        P4_CK_SCALAR(c,p->damagecount); P4_CK_SCALAR(c,p->bonuscount); P4_CK_REF(c,p->attacker,P4_CK_KIND_ACTOR);
        P4_CK_SCALAR(c,p->extralight); P4_CK_SCALAR(c,p->fixedcolormap); P4_CK_SCALAR(c,p->colormap);
        for (int i = 0; i < NUMPSPRITES; ++i) {
            P4_CK_INDEX(c,p->psprites[i].state,states,NUMSTATES);
            P4_CK_SCALAR(c,p->psprites[i].tics); P4_CK_SCALAR(c,p->psprites[i].sx); P4_CK_SCALAR(c,p->psprites[i].sy);
        }
        P4_CK_SCALAR(c,p->didsecret);
        if (c->reading) p->message = NULL;
    }
}
static void p4_ck_world(p4_ck_t *c) {
    P4_CK_SCALAR(c,gamestate); P4_CK_SCALAR(c,oldgamestate); P4_CK_SCALAR(c,gameaction);
    P4_CK_SCALAR(c,gameskill); P4_CK_SCALAR(c,gameepisode); P4_CK_SCALAR(c,gamemap);
    P4_CK_SCALAR(c,leveltime); P4_CK_SCALAR(c,levelstarttic); P4_CK_SCALAR(c,paused);
    P4_CK_SCALAR(c,deathmatch); P4_CK_SCALAR(c,netgame); P4_CK_SCALAR(c,respawnmonsters);
    P4_CK_SCALAR(c,nomonsters); P4_CK_SCALAR(c,respawnparm); P4_CK_SCALAR(c,fastparm);
    P4_CK_SCALAR(c,totalkills); P4_CK_SCALAR(c,totalitems); P4_CK_SCALAR(c,totalsecret);
    for (int i = 0; i < numsectors; ++i) {
        sector_t *s = &sectors[i];
        P4_CK_SCALAR(c,s->floorheight); P4_CK_SCALAR(c,s->ceilingheight);
        P4_CK_SCALAR(c,s->floorpic); P4_CK_SCALAR(c,s->ceilingpic); P4_CK_SCALAR(c,s->lightlevel);
        P4_CK_SCALAR(c,s->special); P4_CK_SCALAR(c,s->tag);
        P4_CK_REF(c,s->soundtarget,P4_CK_KIND_ACTOR); P4_CK_REF(c,s->specialdata,0);
        P4_CK_REF(c,s->thinglist,P4_CK_KIND_ACTOR);
        if (c->reading) { s->validcount = 0; s->soundtraversed = 0; }
    }
    for (int i = 0; i < numlines; ++i) {
        line_t *l = &lines[i];
        P4_CK_SCALAR(c,l->flags); P4_CK_SCALAR(c,l->special); P4_CK_SCALAR(c,l->tag);
        P4_CK_REF(c,l->specialdata,0);
        if (c->reading) l->validcount = 0;
    }
    for (int i = 0; i < numsides; ++i) {
        side_t *s = &sides[i];
        P4_CK_SCALAR(c,s->textureoffset); P4_CK_SCALAR(c,s->rowoffset);
        P4_CK_SCALAR(c,s->toptexture); P4_CK_SCALAR(c,s->bottomtexture); P4_CK_SCALAR(c,s->midtexture);
        P4_CK_INDEX(c,s->sector,sectors,numsectors);
    }
    for (size_t i = 0; i < (size_t)bmapwidth * (size_t)bmapheight; ++i)
        P4_CK_REF(c,blocklinks[i],P4_CK_KIND_ACTOR);
    for (int i = 0; i < MAXPLAYERS; ++i) p4_ck_mapthing(c,&playerstarts[i]);
    uint32_t starts = 0;
    if (!c->reading) {
        for (; starts <= MAX_DM_STARTS; ++starts) if (deathmatch_p == &deathmatchstarts[starts]) break;
    }
    P4_CK_SCALAR(c,starts);
    if (starts > MAX_DM_STARTS) { p4_ck_fail(c,"deathmatch-start-bound"); return; }
    for (uint32_t i = 0; i < starts; ++i) p4_ck_mapthing(c,&deathmatchstarts[i]);
    if (c->reading) deathmatch_p = &deathmatchstarts[starts];
    P4_CK_SCALAR(c,numlinespecials);
    if (numlinespecials < 0 || numlinespecials > P4_CK_MAX_LINEANIMS) { p4_ck_fail(c,"line-special-bound"); return; }
    for (int i = 0; i < numlinespecials; ++i) P4_CK_INDEX(c,linespeciallist[i],lines,numlines);
    P4_CK_SCALAR(c,levelTimer);
    if (levelTimer) P4_CK_SCALAR(c,levelTimeCount); else if (c->reading) levelTimeCount = 0;
    for (int i = 0; i < MAXCEILINGS; ++i) P4_CK_REF(c,activeceilings[i],P4_CK_KIND_CEILING);
    for (int i = 0; i < MAXPLATS; ++i) P4_CK_REF(c,activeplats[i],P4_CK_KIND_PLAT);
    for (int i = 0; i < MAXBUTTONS; ++i) {
        button_t *b = &buttonlist[i];
        if (c->reading) memset(b,0,sizeof(*b));
        P4_CK_SCALAR(c,b->btimer);
        if (b->btimer) {
            P4_CK_INDEX(c,b->line,lines,numlines); P4_CK_SCALAR(c,b->where); P4_CK_SCALAR(c,b->btexture);
            if (c->reading && !c->failed) {
                if (!b->line || b->where < top || b->where > bottom || b->btimer < 0) p4_ck_fail(c,"button-range");
                else b->soundorg = &b->line->frontsector->soundorg;
            }
        }
    }
    P4_CK_SCALAR(c,iquehead); P4_CK_SCALAR(c,iquetail);
    if (iquehead < 0 || iquehead >= ITEMQUESIZE || iquetail < 0 || iquetail >= ITEMQUESIZE) {
        p4_ck_fail(c,"item-queue-bound"); return;
    }
    for (int i = iquetail; i != iquehead; i = (i + 1) % ITEMQUESIZE) {
        p4_ck_mapthing(c,&itemrespawnque[i]); P4_CK_SCALAR(c,itemrespawntime[i]);
    }
    P4_CK_SCALAR(c,bodyqueslot);
    if (bodyqueslot < 0) { p4_ck_fail(c,"body-queue-bound"); return; }
    const unsigned body_count = bodyqueslot < 32 ? (unsigned)bodyqueslot : 32U;
    const unsigned oldest = bodyqueslot < 32 ? 0U : (unsigned)bodyqueslot % 32U;
    if (c->reading) memset(bodyque,0,sizeof(mobj_t *) * 32);
    for (unsigned i = 0; i < body_count; ++i) P4_CK_REF(c,bodyque[(oldest + i) % 32U],P4_CK_KIND_ACTOR);
    P4_CK_SCALAR(c,prndindex); P4_CK_SCALAR(c,rndindex);
    if (prndindex < 0 || prndindex > 255 || rndindex < 0 || rndindex > 255) p4_ck_fail(c,"rng-range");
    for (int i = 0; i < MAXPLAYERS; ++i)
        for (int j = 0; j < BACKUPTICS; ++j) P4_CK_SCALAR(c,consistancy[i][j]);
    if (c->reading) validcount = 1;
}
static void p4_ck_presentation(p4_ck_t *c) {
    /* Explicit tables preserve the exact last completed special-update phase. */
    if (numtextures < 1 || numtextures > 16384 || numflats < 1 || numflats > 16384) {
        p4_ck_fail(c,"presentation-count-bound"); return;
    }
    p4_ck_expect(c,(uint64_t)numtextures,"texture-count");
    p4_ck_expect(c,(uint64_t)numflats,"flat-count");
    for (int i = 0; i < numtextures; ++i) {
        P4_CK_SCALAR(c,texturetranslation[i]);
        if (texturetranslation[i] < 0 || texturetranslation[i] >= numtextures) p4_ck_fail(c,"texture-translation-bound");
    }
    for (int i = 0; i < numflats; ++i) {
        P4_CK_SCALAR(c,flattranslation[i]);
        if (flattranslation[i] < 0 || flattranslation[i] >= numflats) p4_ck_fail(c,"flat-translation-bound");
    }
    P4_CK_SCALAR(c,skytexture); P4_CK_SCALAR(c,skyflatnum); P4_CK_SCALAR(c,skytexturemid);
    if (skytexture < 0 || skytexture >= numtextures || skyflatnum < 0 || skyflatnum >= numflats)
        p4_ck_fail(c,"sky-bound");
}
static int p4_ck_chain(p4_ck_t *c, mobj_t *head, int block, sector_t *sector, size_t bucket) {
    mobj_t *previous = NULL;
    uint32_t length = 0;
    while (head && !c->failed) {
        uint32_t id = 0;
        for (uint32_t i = 0; i < c->count; ++i) if ((void *)c->nodes[i] == head) { id = i + 1; break; }
        if (!id || c->kinds[id - 1] != P4_CK_KIND_ACTOR || length++ >= c->count ||
            (block ? head->bprev : head->sprev) != previous ||
            (!block && (!head->subsector || head->subsector->sector != sector))) {
            p4_ck_fail(c,"spatial-chain"); return 0;
        }
        uint8_t *seen = block ? c->block_seen : c->sector_seen;
        if (seen[id - 1]++) { p4_ck_fail(c,"duplicate-spatial-member"); return 0; }
        if (block) {
            const int64_t x = ((int64_t)head->x - bmaporgx) >> MAPBLOCKSHIFT;
            const int64_t y = ((int64_t)head->y - bmaporgy) >> MAPBLOCKSHIFT;
            if ((head->flags & MF_NOBLOCKMAP) || x < 0 || y < 0 || x >= bmapwidth || y >= bmapheight ||
                (size_t)y * (size_t)bmapwidth + (size_t)x != bucket) {
                p4_ck_fail(c,"blockmap-membership"); return 0;
            }
        } else if (head->flags & MF_NOSECTOR) { p4_ck_fail(c,"sector-membership"); return 0; }
        previous = head; head = block ? head->bnext : head->snext;
    }
    return !c->failed;
}
static sector_t *p4_ck_special_sector(thinker_t *node, enum p4_ck_kind kind) {
    switch (kind) {
    case P4_CK_KIND_FLOOR: return ((floormove_t *)node)->sector;
    case P4_CK_KIND_CEILING: case P4_CK_KIND_CEILING_STOP: return ((ceiling_t *)node)->sector;
    case P4_CK_KIND_DOOR: return ((vldoor_t *)node)->sector;
    case P4_CK_KIND_PLAT: case P4_CK_KIND_PLAT_STOP: return ((plat_t *)node)->sector;
    case P4_CK_KIND_FLASH: return ((lightflash_t *)node)->sector;
    case P4_CK_KIND_STROBE: return ((strobe_t *)node)->sector;
    case P4_CK_KIND_GLOW: return ((glow_t *)node)->sector;
    case P4_CK_KIND_FLICKER: return ((fireflicker_t *)node)->sector;
    default: return NULL;
    }
}
/* These are the pinned vanilla engine's action families, not arbitrary
 * numeric clamps: a weapon action invoked with another readyweapon can index
 * ammo[am_noammo], and actor/psprite action callbacks have different ABIs. */
static int p4_ck_state_number(const state_t *state) {
    for (int i = 0; i < NUMSTATES; ++i) if (state == &states[i]) return i;
    return -1;
}
static int p4_ck_sprite_valid(int sprite, int frame) {
    return sprite >= 0 && sprite < numsprites && sprites &&
        sprites[sprite].spriteframes && frame >= 0 &&
        (frame & ~(FF_FRAMEMASK | FF_FULLBRIGHT)) == 0 &&
        (frame & FF_FRAMEMASK) < sprites[sprite].numframes;
}
static int p4_ck_flash_state(int state) {
    return state == S_LIGHTDONE || state == S_PISTOLFLASH ||
        (state >= S_SGUNFLASH1 && state <= S_SGUNFLASH2) ||
        (state >= S_DSGUNFLASH1 && state <= S_DSGUNFLASH2) ||
        (state >= S_CHAINFLASH1 && state <= S_CHAINFLASH2) ||
        (state >= S_MISSILEFLASH1 && state <= S_MISSILEFLASH4) ||
        (state >= S_PLASMAFLASH1 && state <= S_PLASMAFLASH2) ||
        (state >= S_BFGFLASH1 && state <= S_BFGFLASH2);
}
static void p4_ck_validate_numeric(p4_ck_t *c) {
    static const int weapon_first[NUMWEAPONS] = {
        S_PUNCH, S_PISTOL, S_SGUN, S_CHAIN, S_MISSILE, S_PLASMA, S_BFG, S_SAW, S_DSGUN
    };
    static const int weapon_last[NUMWEAPONS] = {
        S_PUNCH5, S_PISTOL4, S_SGUN9, S_CHAIN3, S_MISSILE3, S_PLASMA2, S_BFG4, S_SAW3, S_DSNR2
    };
    for (int slot = 0; slot < MAXPLAYERS && !c->failed; ++slot) {
        player_t *p = &players[slot];
        if ((playeringame[slot] != false && playeringame[slot] != true) ||
            p->playerstate < PST_LIVE || p->playerstate > PST_REBORN ||
            p->readyweapon < 0 || p->readyweapon >= NUMWEAPONS ||
            (p->pendingweapon != wp_nochange &&
             (p->pendingweapon < 0 || p->pendingweapon >= NUMWEAPONS)) ||
            (p->fixedcolormap != 0 && p->fixedcolormap != 1 && p->fixedcolormap != NUMCOLORMAPS) ||
            p->health < 0 || p->damagecount < 0 || p->damagecount > 100 ||
            p->bonuscount < 0 || p->bonuscount > INT_MAX - 7) {
            p4_ck_fail(c,"player-value-range"); break;
        }
        if (playeringame[slot] && (!p->mo || p->mo->player != p)) {
            p4_ck_fail(c,"active-player-body"); break;
        }
        for (int index = 0; index < NUMPSPRITES && !c->failed; ++index) {
            pspdef_t *psp = &p->psprites[index];
            if (!psp->state) continue;
            const int state = p4_ck_state_number(psp->state);
            if (state < S_LIGHTDONE || state >= S_BLOOD1 ||
                !p4_ck_sprite_valid((int)psp->state->sprite,psp->state->frame) ||
                (index == ps_weapon && (state < weapon_first[p->readyweapon] || state > weapon_last[p->readyweapon])) ||
                (index == ps_flash && !p4_ck_flash_state(state))) {
                p4_ck_fail(c,"psprite-state-family"); break;
            }
            if (index == ps_weapon && state == weaponinfo[p->readyweapon].downstate &&
                p->playerstate != PST_DEAD && p->health > 0 && p->pendingweapon == wp_nochange)
                p4_ck_fail(c,"lowering-without-pending-weapon");
        }
    }
    for (int i = 0; i < numsectors && !c->failed; ++i)
        if (sectors[i].floorpic < 0 || sectors[i].floorpic >= numflats ||
            sectors[i].ceilingpic < 0 || sectors[i].ceilingpic >= numflats)
            p4_ck_fail(c,"sector-flat-range");
    for (int i = 0; i < numsides && !c->failed; ++i)
        if (sides[i].toptexture < 0 || sides[i].toptexture >= numtextures ||
            sides[i].midtexture < 0 || sides[i].midtexture >= numtextures ||
            sides[i].bottomtexture < 0 || sides[i].bottomtexture >= numtextures)
            p4_ck_fail(c,"side-texture-range");
    for (int i = 0; i < MAXBUTTONS && !c->failed; ++i)
        if (buttonlist[i].btimer && (buttonlist[i].btexture < 0 || buttonlist[i].btexture >= numtextures))
            p4_ck_fail(c,"button-texture-range");
    for (uint32_t i = 0; i < c->count && !c->failed; ++i) {
        thinker_t *node = c->nodes[i];
        switch (c->kinds[i]) {
        case P4_CK_KIND_ACTOR: {
            mobj_t *m = (mobj_t *)node;
            const int state = p4_ck_state_number(m->state);
            if (state < S_BLOOD1 || !p4_ck_sprite_valid((int)m->sprite,m->frame) ||
                m->sprite != m->state->sprite || m->frame != m->state->frame ||
                m->movedir < 0 || m->movedir > 8 || m->lastlook < 0 || m->lastlook >= MAXPLAYERS)
                p4_ck_fail(c,"actor-state-or-render-range");
            break;
        }
        case P4_CK_KIND_FLOOR: {
            floormove_t *f = (floormove_t *)node;
            if (f->type < lowerFloor || f->type > raiseFloor512 || (f->direction != -1 && f->direction != 1) ||
                (f->crush != false && f->crush != true) ||
                (((f->type == lowerAndChange && f->direction == -1) || (f->type == donutRaise && f->direction == 1)) &&
                 (f->texture < 0 || f->texture >= numflats))) p4_ck_fail(c,"floor-value-range");
            break;
        }
        case P4_CK_KIND_CEILING: case P4_CK_KIND_CEILING_STOP: {
            ceiling_t *ceiling = (ceiling_t *)node;
            if (ceiling->type < lowerToFloor || ceiling->type > silentCrushAndRaise ||
                ceiling->direction < -1 || ceiling->direction > 1 ||
                (ceiling->crush != false && ceiling->crush != true) ||
                ((c->kinds[i] == P4_CK_KIND_CEILING_STOP) != (ceiling->direction == 0)) ||
                (!ceiling->direction && ceiling->olddirection != -1 && ceiling->olddirection != 1))
                p4_ck_fail(c,"ceiling-value-range");
            break;
        }
        case P4_CK_KIND_DOOR: {
            vldoor_t *door = (vldoor_t *)node;
            if (door->type < vld_normal || door->type > vld_blazeClose || door->direction < -1 || door->direction > 2)
                p4_ck_fail(c,"door-value-range");
            break;
        }
        case P4_CK_KIND_PLAT: case P4_CK_KIND_PLAT_STOP: {
            plat_t *plat = (plat_t *)node;
            if (plat->type < perpetualRaise || plat->type > blazeDWUS || plat->status < up || plat->status > in_stasis ||
                (plat->crush != false && plat->crush != true) ||
                ((c->kinds[i] == P4_CK_KIND_PLAT_STOP) != (plat->status == in_stasis)) ||
                (plat->status == in_stasis && (plat->oldstatus < up || plat->oldstatus > waiting)))
                p4_ck_fail(c,"plat-value-range");
            break;
        }
        case P4_CK_KIND_GLOW: {
            glow_t *glow = (glow_t *)node;
            if (glow->direction != -1 && glow->direction != 1) p4_ck_fail(c,"glow-direction-range");
            break;
        }
        default: break;
        }
    }
}
static void p4_ck_validate(p4_ck_t *c) {
    p4_ck_validate_numeric(c);
    memset(c->sector_seen,0,sizeof(c->sector_seen)); memset(c->block_seen,0,sizeof(c->block_seen));
    for (uint32_t i = 0; i < c->count && !c->failed; ++i) {
        if (c->kinds[i] == P4_CK_KIND_ACTOR) {
            mobj_t *m = (mobj_t *)c->nodes[i];
            if (m->type < 0 || m->type >= NUMMOBJTYPES || m->info != &mobjinfo[m->type] ||
                !m->state || !m->subsector || m->sprite < 0 || m->sprite >= NUMSPRITES)
                p4_ck_fail(c,"actor-invariant");
            /* Corpse actors retain player pointers after that player respawns. */
        } else {
            sector_t *sector = p4_ck_special_sector(c->nodes[i],c->kinds[i]);
            if (!sector) p4_ck_fail(c,"null-special-sector");
            else if ((c->kinds[i] == P4_CK_KIND_FLOOR || c->kinds[i] == P4_CK_KIND_CEILING ||
                      c->kinds[i] == P4_CK_KIND_CEILING_STOP || c->kinds[i] == P4_CK_KIND_DOOR ||
                      c->kinds[i] == P4_CK_KIND_PLAT || c->kinds[i] == P4_CK_KIND_PLAT_STOP) &&
                     sector->specialdata != c->nodes[i]) p4_ck_fail(c,"special-sector-backlink");
        }
    }
    /* Both arrays are execution state: a duplicate or missing entry can
     * leave a removed special reachable or make its eventual removal fail. */
    for (int slot = 0; slot < MAXCEILINGS && !c->failed; ++slot) if (activeceilings[slot]) {
        uint32_t id = 0;
        for (uint32_t i = 0; i < c->count; ++i)
            if ((void *)c->nodes[i] == activeceilings[slot]) { id = i + 1; break; }
        if (!id || !p4_ck_kind_matches(c->kinds[id - 1],P4_CK_KIND_CEILING))
            p4_ck_fail(c,"active-ceiling-reference");
        for (int earlier = 0; earlier < slot; ++earlier)
            if (activeceilings[earlier] == activeceilings[slot]) p4_ck_fail(c,"duplicate-active-ceiling");
    }
    for (int slot = 0; slot < MAXPLATS && !c->failed; ++slot) if (activeplats[slot]) {
        uint32_t id = 0;
        for (uint32_t i = 0; i < c->count; ++i)
            if ((void *)c->nodes[i] == activeplats[slot]) { id = i + 1; break; }
        if (!id || !p4_ck_kind_matches(c->kinds[id - 1],P4_CK_KIND_PLAT))
            p4_ck_fail(c,"active-plat-reference");
        for (int earlier = 0; earlier < slot; ++earlier)
            if (activeplats[earlier] == activeplats[slot]) p4_ck_fail(c,"duplicate-active-plat");
    }
    for (uint32_t i = 0; i < c->count && !c->failed; ++i) {
        int members = 0;
        if (p4_ck_kind_matches(c->kinds[i],P4_CK_KIND_CEILING)) {
            for (int slot = 0; slot < MAXCEILINGS; ++slot)
                if ((void *)c->nodes[i] == activeceilings[slot]) ++members;
            if (members != 1) p4_ck_fail(c,"missing-active-ceiling");
        } else if (p4_ck_kind_matches(c->kinds[i],P4_CK_KIND_PLAT)) {
            for (int slot = 0; slot < MAXPLATS; ++slot)
                if ((void *)c->nodes[i] == activeplats[slot]) ++members;
            if (members != 1) p4_ck_fail(c,"missing-active-plat");
        }
    }
    for (int i = 0; i < numsectors && !c->failed; ++i) {
        sector_t *sector = &sectors[i];
        if (sector->specialdata) {
            uint32_t id = 0;
            for (uint32_t j = 0; j < c->count; ++j)
                if (c->nodes[j] == sector->specialdata) { id = j + 1; break; }
            const enum p4_ck_kind kind = id ? c->kinds[id - 1] : P4_CK_KIND_UNKNOWN;
            if (!id || (kind != P4_CK_KIND_FLOOR && kind != P4_CK_KIND_CEILING && kind != P4_CK_KIND_CEILING_STOP &&
                        kind != P4_CK_KIND_DOOR && kind != P4_CK_KIND_PLAT && kind != P4_CK_KIND_PLAT_STOP) ||
                p4_ck_special_sector(c->nodes[id - 1],kind) != sector)
                p4_ck_fail(c,"sector-special-owner");
        }
        p4_ck_chain(c,sector->thinglist,0,sector,0);
    }
    if (iquehead < 0 || iquehead >= ITEMQUESIZE || iquetail < 0 || iquetail >= ITEMQUESIZE)
        p4_ck_fail(c,"item-queue-bound");
    for (int i = iquetail; !c->failed && i != iquehead; i = (i + 1) % ITEMQUESIZE) {
        int type = 0;
        while (type < NUMMOBJTYPES && mobjinfo[type].doomednum != itemrespawnque[i].type) ++type;
        if (type == NUMMOBJTYPES) p4_ck_fail(c,"item-respawn-type");
    }
    for (size_t i = 0; i < (size_t)bmapwidth * (size_t)bmapheight && !c->failed; ++i)
        p4_ck_chain(c,blocklinks[i],1,NULL,i);
    for (uint32_t i = 0; i < c->count && !c->failed; ++i) if (c->kinds[i] == P4_CK_KIND_ACTOR) {
        mobj_t *m = (mobj_t *)c->nodes[i];
        const int64_t x = ((int64_t)m->x - bmaporgx) >> MAPBLOCKSHIFT;
        const int64_t y = ((int64_t)m->y - bmaporgy) >> MAPBLOCKSHIFT;
        const int block_expected = !(m->flags & MF_NOBLOCKMAP) && x >= 0 && y >= 0 && x < bmapwidth && y < bmapheight;
        if (c->sector_seen[i] != !(m->flags & MF_NOSECTOR) || c->block_seen[i] != block_expected)
            p4_ck_fail(c,"missing-spatial-member");
    }
    for (int i = 0; i < numsides; ++i) if (!sides[i].sector) p4_ck_fail(c,"null-side-sector");
    for (int i = 0; i < numlinespecials; ++i) if (!linespeciallist[i]) p4_ck_fail(c,"null-scroll-line");
    if (gamestate != GS_LEVEL || gameaction != ga_nothing || !nomonsters || deathmatch != 2 ||
        !netgame || paused || ticdup != 1 || fastparm) p4_ck_fail(c,"unsupported-engine-state");
}
/* Header fields are independently checked before reload. The payload checksum
 * checks storage integrity; caller verifies SHA-256 and session authentication. */
bool P4_DoomCheckpointCapture(uint8_t *buffer, size_t capacity, size_t *length,
                              uint32_t *next_tic, uint8_t *mask) {
    p4_ck_error = "ok";
    if (length) *length = 0;
    if (!buffer || !length || !next_tic || !mask || capacity > P4_CK_MAX_BYTES || gametic < 0 ||
        gamestate != GS_LEVEL || gameaction != ga_nothing || paused || ticdup != 1 ||
        !nomonsters || deathmatch != 2 || !netgame || fastparm) { p4_ck_error = "unstable-or-unsupported-boundary"; return 0; }
    p4_ck_t *c = p4_ck_begin();
    if (!c) return false;
    c->bytes = buffer; c->capacity = capacity;
    for (thinker_t *node = thinkercap.next; node != &thinkercap && !c->failed; node = node->next) {
        if (!node || c->count == P4_CK_MAX_THINKERS) { p4_ck_fail(c,"thinker-list-bound"); break; }
        enum p4_ck_kind kind = p4_ck_thinker_kind(node);
        if (kind == P4_CK_KIND_REMOVED) { p4_ck_fail(c,"defer-tombstone"); break; }
        if (!p4_ck_kind_size(kind)) { p4_ck_fail(c,"unknown-thinker-class"); break; }
        c->nodes[c->count] = node; c->kinds[c->count++] = kind;
    }
    if (bmapwidth <= 0 || bmapheight <= 0 || (uint64_t)bmapwidth * (uint64_t)bmapheight > 1048576 || !blocklinks)
        p4_ck_fail(c,"blockmap-bound");
    p4_ck_validate(c);
    p4_ck_u64(c,UINT64_C(0x3154504b43443450)); /* P4DCKPT1 */
    p4_ck_u64(c,2); p4_ck_u64(c,0); p4_ck_u64(c,0);
    p4_ck_u64(c,p4_ck_hash((const uint8_t *)p4_ck_content,strlen(p4_ck_content)));
    p4_ck_u64(c,(uint64_t)gametic); p4_ck_u64(c,c->count);
    p4_ck_u64(c,(uint64_t)gameskill); p4_ck_u64(c,(uint64_t)gameepisode); p4_ck_u64(c,(uint64_t)gamemap);
    p4_ck_u64(c,(uint64_t)fastparm); p4_ck_u64(c,(uint64_t)respawnparm);
    p4_ck_u64(c,(uint64_t)numsectors); p4_ck_u64(c,(uint64_t)numlines); p4_ck_u64(c,(uint64_t)numsides);
    p4_ck_u64(c,(uint64_t)numsubsectors); p4_ck_u64(c,(uint64_t)bmapwidth); p4_ck_u64(c,(uint64_t)bmapheight);
    p4_ck_u64(c,(uint64_t)bmaporgx); p4_ck_u64(c,(uint64_t)bmaporgy);
    p4_doom_arena_t arena = {0}; p4_doom_gc_checkpoint_get_arena(&arena); p4_ck_arena(c,&arena);
    for (uint32_t i = 0; i < c->count; ++i) p4_ck_u64(c,c->kinds[i]);
    for (uint32_t i = 0; i < c->count && !c->failed; ++i) p4_ck_thinker_fields(c,c->nodes[i],c->kinds[i]);
    p4_ck_world(c); p4_ck_presentation(c); p4_ck_players(c);
    p4_ck_u64(c,UINT64_C(0x454e44434b503431));
    if (!c->failed) {
        const size_t size = c->position;
        const uint64_t checksum = p4_ck_hash(buffer + 32,size - 32);
        c->position = 16; p4_ck_u64(c,size); p4_ck_u64(c,checksum); *length = size;
    }
    const int result = !c->failed;
    if (result) {
        unsigned members = 0;
        for (unsigned i = 0; i < MAXPLAYERS; ++i) if (playeringame[i]) members |= 1U << i;
        *next_tic = (uint32_t)gametic; *mask = (uint8_t)members;
    }
    return result != 0;
}
bool P4_DoomCheckpointRestore(const uint8_t *buffer, size_t length,
                              uint32_t *next_tic, uint8_t *mask_out) {
    p4_ck_error = "ok";
    if (!buffer || !next_tic || !mask_out || length < 160 || length > P4_CK_MAX_BYTES || length % 8) {
        p4_ck_error = "length-bound"; return 0;
    }
    p4_ck_t *c = p4_ck_begin();
    if (!c) return false;
    c->bytes = (uint8_t *)(uintptr_t)buffer; c->capacity = length; c->reading = 1;
    p4_ck_expect(c,UINT64_C(0x3154504b43443450),"magic"); p4_ck_expect(c,2,"schema");
    p4_ck_expect(c,length,"length-mismatch");
    p4_ck_expect(c,p4_ck_hash(buffer + 32,length - 32),"checksum");
    p4_ck_expect(c,p4_ck_hash((const uint8_t *)p4_ck_content,strlen(p4_ck_content)),"content-identity");
    if (fastparm) p4_ck_fail(c,"unsupported-fast-config");
    const uint64_t tick = p4_ck_u64(c,0), count = p4_ck_u64(c,0);
    const uint64_t skill = p4_ck_u64(c,0), episode = p4_ck_u64(c,0), map = p4_ck_u64(c,0);
    p4_ck_expect(c,(uint64_t)fastparm,"fast-config"); p4_ck_expect(c,(uint64_t)respawnparm,"respawn-config");
    uint64_t geometry[8];
    for (unsigned i = 0; i < 8; ++i) geometry[i] = p4_ck_u64(c,0);
    if (tick > INT_MAX || count > P4_CK_MAX_THINKERS || skill > sk_nightmare ||
        episode != 1 || map < 1 || map > 32) p4_ck_fail(c,"header-range");
    p4_doom_arena_t arena = {0}; p4_ck_arena(c,&arena);
    if (!c->failed && p4_doom_arena_map_number(arena.maps[arena.map_index]) != map) p4_ck_fail(c,"map-selection-mismatch");
    c->count = count <= P4_CK_MAX_THINKERS ? (uint32_t)count : 0;
    for (uint32_t i = 0; i < c->count && !c->failed; ++i) {
        uint64_t kind = p4_ck_u64(c,0);
        if (kind > P4_CK_KIND_PLAT_STOP || !p4_ck_kind_size((enum p4_ck_kind)kind)) p4_ck_fail(c,"thinker-class");
        else c->kinds[i] = (enum p4_ck_kind)kind;
    }
    if (c->failed) return false;
    /* From here failure requires the owner to abort this cold guest. */
    const int local_console = consoleplayer, local_display = displayplayer;
    if (!p4_doom_gc_checkpoint_set_arena(&arena)) p4_ck_fail(c,"arena-rejected");
    if (!c->failed) {
        /* Clear cached music references before map preparation purges the zone. */
        S_StopMusic();
        G_InitNew((skill_t)skill,(int)episode,(int)map);
    }
    consoleplayer = local_console; displayplayer = local_display;
    if (geometry[0] != (uint64_t)numsectors || geometry[1] != (uint64_t)numlines ||
        geometry[2] != (uint64_t)numsides || geometry[3] != (uint64_t)numsubsectors ||
        geometry[4] != (uint64_t)bmapwidth || geometry[5] != (uint64_t)bmapheight ||
        geometry[6] != (uint64_t)bmaporgx || geometry[7] != (uint64_t)bmaporgy)
        p4_ck_fail(c,"map-geometry-identity");
    if (c->failed) return false;
    S_Start();
    for (thinker_t *node = thinkercap.next; node != &thinkercap;) {
        thinker_t *next = node->next; Z_Free(node); node = next;
    }
    P_InitThinkers();
    for (uint32_t i = 0; i < c->count; ++i) {
        const size_t size = p4_ck_kind_size(c->kinds[i]);
        c->nodes[i] = Z_Malloc((int)size,PU_LEVEL,NULL);
        memset(c->nodes[i],0,size);
        c->nodes[i]->function.acp1 = p4_ck_kind_function(c->kinds[i]);
        P_AddThinker(c->nodes[i]);
    }
    for (uint32_t i = 0; i < c->count && !c->failed; ++i) p4_ck_thinker_fields(c,c->nodes[i],c->kinds[i]);
    p4_ck_world(c); p4_ck_presentation(c); p4_ck_players(c);
    p4_ck_expect(c,UINT64_C(0x454e44434b503431),"end-marker");
    if (c->position != length) p4_ck_fail(c,"trailing-bytes");
    if (!c->failed) p4_ck_validate(c);
    if (gameskill != (skill_t)skill || gameepisode != (int)episode || gamemap != (int)map)
        p4_ck_fail(c,"payload-map-mismatch");
    if (!c->failed && !p4_doom_gc_checkpoint_set_arena(&arena)) p4_ck_fail(c,"arena-rejected");
    unsigned mask = 0;
    for (unsigned i = 0; i < MAXPLAYERS; ++i) if (playeringame[i]) mask |= 1U << i;
    if (!c->failed && !D_P4CheckpointRebase((int)tick,mask)) p4_ck_fail(c,"loop-rebase-rejected");
    if (!c->failed) {
        /* players[] was decoded after temporary map setup. Rebind UI fields. */
        ST_Start(); HU_Start();
        *next_tic = (uint32_t)tick; *mask_out = (uint8_t)mask;
    }
    const int result = !c->failed;
    return result != 0;
}
#undef P4_CK_SCALAR
#undef P4_CK_REF
#undef P4_CK_INDEX
