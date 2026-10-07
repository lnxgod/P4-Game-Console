// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef P4_DOOM_CHECKPOINT_THINKERS_H
#define P4_DOOM_CHECKPOINT_THINKERS_H

/* Internal schema 2 thinker fields. The codec owns allocation, IDs and
 * execution-list restoration. Preserve conditional payload fields: some unused
 * vanilla fields are uninitialized and must never be read during capture. */
static void p4_ck_thinker_fields(p4_ck_t *c, thinker_t *node,
                                 enum p4_ck_kind kind)
{
    switch (kind) {
    case P4_CK_KIND_ACTOR: {
        mobj_t *actor = (mobj_t *)node;
        P4_CK_SCALAR(c, actor->x);
        P4_CK_SCALAR(c, actor->y);
        P4_CK_SCALAR(c, actor->z);
        P4_CK_SCALAR(c, actor->angle);
        P4_CK_SCALAR(c, actor->sprite);
        P4_CK_SCALAR(c, actor->frame);
        P4_CK_INDEX(c, actor->subsector, subsectors, numsubsectors);
        P4_CK_SCALAR(c, actor->floorz);
        P4_CK_SCALAR(c, actor->ceilingz);
        P4_CK_SCALAR(c, actor->radius);
        P4_CK_SCALAR(c, actor->height);
        P4_CK_SCALAR(c, actor->momx);
        P4_CK_SCALAR(c, actor->momy);
        P4_CK_SCALAR(c, actor->momz);
        P4_CK_SCALAR(c, actor->type);
        P4_CK_INDEX(c, actor->info, mobjinfo, NUMMOBJTYPES);
        P4_CK_SCALAR(c, actor->tics);
        P4_CK_INDEX(c, actor->state, states, NUMSTATES);
        P4_CK_SCALAR(c, actor->flags);
        P4_CK_SCALAR(c, actor->health);
        P4_CK_SCALAR(c, actor->movedir);
        P4_CK_SCALAR(c, actor->movecount);
        P4_CK_REF(c, actor->target, P4_CK_KIND_ACTOR);
        P4_CK_SCALAR(c, actor->reactiontime);
        P4_CK_SCALAR(c, actor->threshold);
        P4_CK_INDEX(c, actor->player, players, MAXPLAYERS);
        P4_CK_SCALAR(c, actor->lastlook);
        p4_ck_mapthing(c, &actor->spawnpoint);
        P4_CK_REF(c, actor->tracer, P4_CK_KIND_ACTOR);
        P4_CK_REF(c, actor->snext, P4_CK_KIND_ACTOR);
        P4_CK_REF(c, actor->sprev, P4_CK_KIND_ACTOR);
        P4_CK_REF(c, actor->bnext, P4_CK_KIND_ACTOR);
        P4_CK_REF(c, actor->bprev, P4_CK_KIND_ACTOR);
        if (c->reading) actor->validcount = 0;
        break;
    }
    case P4_CK_KIND_FLOOR: {
        floormove_t *special = (floormove_t *)node;
        P4_CK_INDEX(c, special->sector, sectors, numsectors);
        P4_CK_SCALAR(c, special->type);
        P4_CK_SCALAR(c, special->crush);
        P4_CK_SCALAR(c, special->direction);
        if ((special->type == lowerAndChange && special->direction == -1) ||
            (special->type == donutRaise && special->direction == 1)) {
            P4_CK_SCALAR(c, special->newspecial);
            P4_CK_SCALAR(c, special->texture);
        }
        P4_CK_SCALAR(c, special->floordestheight);
        P4_CK_SCALAR(c, special->speed);
        break;
    }
    case P4_CK_KIND_CEILING:
    case P4_CK_KIND_CEILING_STOP: {
        ceiling_t *special = (ceiling_t *)node;
        P4_CK_INDEX(c, special->sector, sectors, numsectors);
        P4_CK_SCALAR(c, special->type);
        if (special->type != raiseToHighest)
            P4_CK_SCALAR(c, special->bottomheight);
        if (special->type == raiseToHighest || special->type == crushAndRaise ||
            special->type == fastCrushAndRaise || special->type == silentCrushAndRaise)
            P4_CK_SCALAR(c, special->topheight);
        P4_CK_SCALAR(c, special->speed);
        P4_CK_SCALAR(c, special->crush);
        P4_CK_SCALAR(c, special->direction);
        P4_CK_SCALAR(c, special->tag);
        if (special->direction == 0)
            P4_CK_SCALAR(c, special->olddirection);
        break;
    }
    case P4_CK_KIND_DOOR: {
        vldoor_t *special = (vldoor_t *)node;
        P4_CK_INDEX(c, special->sector, sectors, numsectors);
        P4_CK_SCALAR(c, special->type);
        P4_CK_SCALAR(c, special->topheight);
        P4_CK_SCALAR(c, special->speed);
        P4_CK_SCALAR(c, special->direction);
        P4_CK_SCALAR(c, special->topwait);
        if (special->direction == 0 || special->direction == 2)
            P4_CK_SCALAR(c, special->topcountdown);
        break;
    }
    case P4_CK_KIND_PLAT:
    case P4_CK_KIND_PLAT_STOP: {
        plat_t *special = (plat_t *)node;
        P4_CK_INDEX(c, special->sector, sectors, numsectors);
        P4_CK_SCALAR(c, special->speed);
        P4_CK_SCALAR(c, special->low);
        P4_CK_SCALAR(c, special->high);
        P4_CK_SCALAR(c, special->wait);
        P4_CK_SCALAR(c, special->status);
        if (special->status == in_stasis)
            P4_CK_SCALAR(c, special->oldstatus);
        if (special->status == waiting ||
            (special->status == in_stasis && special->oldstatus == waiting))
            P4_CK_SCALAR(c, special->count);
        P4_CK_SCALAR(c, special->crush);
        P4_CK_SCALAR(c, special->tag);
        P4_CK_SCALAR(c, special->type);
        break;
    }
    case P4_CK_KIND_FLASH: {
        lightflash_t *special = (lightflash_t *)node;
        P4_CK_INDEX(c, special->sector, sectors, numsectors);
        P4_CK_SCALAR(c, special->count);
        P4_CK_SCALAR(c, special->maxlight);
        P4_CK_SCALAR(c, special->minlight);
        P4_CK_SCALAR(c, special->maxtime);
        P4_CK_SCALAR(c, special->mintime);
        break;
    }
    case P4_CK_KIND_STROBE: {
        strobe_t *special = (strobe_t *)node;
        P4_CK_INDEX(c, special->sector, sectors, numsectors);
        P4_CK_SCALAR(c, special->count);
        P4_CK_SCALAR(c, special->minlight);
        P4_CK_SCALAR(c, special->maxlight);
        P4_CK_SCALAR(c, special->darktime);
        P4_CK_SCALAR(c, special->brighttime);
        break;
    }
    case P4_CK_KIND_GLOW: {
        glow_t *special = (glow_t *)node;
        P4_CK_INDEX(c, special->sector, sectors, numsectors);
        P4_CK_SCALAR(c, special->minlight);
        P4_CK_SCALAR(c, special->maxlight);
        P4_CK_SCALAR(c, special->direction);
        break;
    }
    case P4_CK_KIND_FLICKER: {
        fireflicker_t *special = (fireflicker_t *)node;
        P4_CK_INDEX(c, special->sector, sectors, numsectors);
        P4_CK_SCALAR(c, special->count);
        P4_CK_SCALAR(c, special->maxlight);
        P4_CK_SCALAR(c, special->minlight);
        break;
    }
    case P4_CK_KIND_REMOVED:
        /* The removed sentinel no longer identifies the payload allocation.
         * Only the caller-owned thinker header is safe to reconstruct. */
        break;
    case P4_CK_KIND_UNKNOWN:
    default:
        /* The enclosing codec rejects unknown classes before this function. */
        break;
    }
}

#endif
