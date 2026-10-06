// SPDX-License-Identifier: MIT
#include "blast_circuit_internal.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8U); }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(v >> (i * 8U)); }
static uint16_t get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8U)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8U | (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U; }
static bool newer(uint32_t a, uint32_t b)
{ return a != b && (a - b) < UINT32_C(0x80000000); }

void bc_encode(const bc_world_t *w, uint8_t out[BC_SNAPSHOT])
{
    memset(out, 0, BC_SNAPSHOT);
    out[0] = w->phase; out[1] = w->winner; out[2] = w->round;
    put16(out + 3, w->timer); put16(out + 5, w->time_left);
    put32(out + 7, w->tick); memcpy(out + 11, w->score, 4U); out[15] = w->theme;
    unsigned at = 16U;
    for (unsigned i = 0; i < 4U; ++i) {
        const bc_player_t *p = &w->players[i];
        out[at++] = p->x; out[at++] = p->y;
        out[at++] = p->from_x; out[at++] = p->from_y;
        out[at++] = p->cooldown; out[at++] = p->alive;
        out[at++] = p->range; out[at++] = p->capacity; out[at++] = p->speed;
    }
    for (unsigned i = 0; i < BC_BOMBS; ++i) {
        const bc_bomb_t *b = &w->bombs[i];
        out[at++] = b->x; out[at++] = b->y; out[at++] = b->owner;
        out[at++] = b->fuse; out[at++] = b->range;
    }
    for (unsigned i = 0; i < BC_CELLS; ++i) {
        out[at++] = w->tile[i]; out[at++] = w->fire[i];
    }
}

bool bc_decode(bc_world_t *w, const uint8_t in[BC_SNAPSHOT])
{
    bc_world_t next = {0};
    next.phase = in[0]; next.winner = in[1]; next.round = in[2];
    next.timer = get16(in + 3); next.time_left = get16(in + 5);
    next.tick = get32(in + 7); memcpy(next.score, in + 11, 4U); next.theme = in[15];
    if (next.phase < BC_READY || next.phase > BC_MATCH ||
        (next.winner >= 4U && next.winner != 255U) || !next.round ||
        next.timer > 60U || next.time_left > 2400U || next.theme >= BC_THEMES) return false;
    unsigned at = 16U;
    for (unsigned i = 0; i < 4U; ++i) {
        bc_player_t *p = &next.players[i];
        p->x = in[at++]; p->y = in[at++];
        p->from_x = in[at++]; p->from_y = in[at++];
        p->cooldown = in[at++]; p->alive = in[at++];
        p->range = in[at++]; p->capacity = in[at++]; p->speed = in[at++];
        int dx = (int)p->x - p->from_x, dy = (int)p->y - p->from_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (bc_border(p->x, p->y) || bc_border(p->from_x, p->from_y) ||
            dx + dy > 1 || p->alive > 1U || p->range < 1U || p->range > 5U ||
            p->capacity < 1U || p->capacity > 3U || p->speed > 1U ||
            p->cooldown > (p->speed ? 2U : 3U) || next.score[i] > BC_WIN_SCORE)
            return false;
    }
    for (unsigned i = 0; i < BC_BOMBS; ++i) {
        bc_bomb_t *b = &next.bombs[i];
        b->x = in[at++]; b->y = in[at++]; b->owner = in[at++];
        b->fuse = in[at++]; b->range = in[at++];
        if (b->owner >= 4U || b->fuse > BC_FUSE || b->range > 5U ||
            b->x >= BC_W || b->y >= BC_H ||
            (b->fuse && (bc_border(b->x, b->y) || !b->range))) return false;
    }
    for (unsigned i = 0; i < BC_CELLS; ++i) {
        next.tile[i] = in[at++];
        next.fire[i] = in[at++];
        if (next.tile[i] > BC_FUEL || next.fire[i] > BC_EMBER_FIRE ||
            (bc_border((int)(i % BC_W), (int)(i / BC_W)) && next.tile[i] != BC_WALL) ||
            (next.tile[i] == BC_WALL && next.fire[i])) return false;
    }
    for (unsigned i = 0; i < 4U; ++i)
        if (bc_solid(next.tile[next.players[i].y * BC_W + next.players[i].x]) ||
            bc_solid(next.tile[next.players[i].from_y * BC_W + next.players[i].from_x]))
            return false;
    for (unsigned i = 0; i < BC_BOMBS; ++i) {
        const bc_bomb_t *b = &next.bombs[i];
        if (b->fuse && bc_solid(next.tile[b->y * BC_W + b->x])) return false;
        for (unsigned j = 0; j < i; ++j)
            if (b->fuse && next.bombs[j].fuse &&
                b->x == next.bombs[j].x && b->y == next.bombs[j].y) return false;
    }
    *w = next; // An invalid or partial message never mutates the live world.
    return true;
}

static bool status_ok(const p4_game_multiplayer_status_t *n)
{
    return n->state == P4_GAME_MULTIPLAYER_CONNECTED && n->player_count >= 2U &&
        n->player_count <= BC_PLAYERS && n->local_player_slot < n->player_count &&
        ((n->role == P4_GAME_MULTIPLAYER_ROLE_HOST && n->local_player_slot == 0U) ||
         (n->role == P4_GAME_MULTIPLAYER_ROLE_CLIENT && n->local_player_slot > 0U));
}
bool bc_network_begin(p4_game_context_t *c, bc_state_t *s)
{
    p4_game_multiplayer_status_t n;p4_game_multiplayer_profile_t p;
    if(!p4_game_multiplayer_read_status(c,&n) || n.state==P4_GAME_MULTIPLAYER_OFFLINE) return false;
    s->network=true;
    if(!status_ok(&n) || !p4_game_multiplayer_read_profile(c,&p) ||
       p.style!=P4_GAME_MULTIPLAYER_STYLE_REALTIME || p.protocol!=BC_PROTOCOL ||
       p.message_bytes<BC_PACKET || p.max_players<n.player_count || p.min_players>n.player_count || p.tick_rate_hz!=20U) {
        s->world.phase=BC_LOST;return true;
    }
    s->generation=n.generation;s->seed=n.session_seed;s->humans=n.player_count;s->local_slot=n.local_player_slot;
    s->host=n.role==P4_GAME_MULTIPLAYER_ROLE_HOST;
    s->world.phase=s->host?BC_SELECT:BC_WAIT;s->snapshot_ms=200U;s->input_ms=50U;
    return true;
}
static void lost(bc_state_t *s)
{
    s->world.phase=BC_LOST;memset(s->direction,0,sizeof(s->direction));
    memset(s->bomb_pressed,0,sizeof(s->bomb_pressed));s->received_mask=0U;s->level_mask=0U;
    bc_cue(s,BC_S_ERROR);
}
void bc_network_transfer(bc_state_t *s)
{
    if(!s->host || bc_level_match_validate(&s->level)!=BC_LEVEL_OK) return;
    uint8_t bytes[BC_LEVEL_BYTES];bc_level_encode(&s->level,bytes);
    if(++s->level_id==0U) ++s->level_id;
    s->level_hash=bc_hash(bytes,BC_LEVEL_BYTES);s->level_ack=1U;s->transfer_ms=0U;
    s->level_ready=true;s->level_send_part=0U;s->snapshot_pending=false;s->world.phase=BC_TRANSFER;s->snapshot_ms=200U;s->accumulator=0U;
    memset(s->direction,0,sizeof(s->direction));memset(s->bomb_pressed,0,sizeof(s->bomb_pressed));
    bc_cue(s,BC_S_TRANSFER);
}
static void accept_level(bc_state_t *s,const p4_game_multiplayer_message_t *m)
{
    if(m->player_slot || m->bytes<12U || m->data[7]) return;
    const unsigned part=m->data[6];if(part>=BC_LEVEL_PARTS) return;
    const unsigned count=part==BC_LEVEL_PARTS-1U?BC_LEVEL_BYTES-part*BC_CHUNK:BC_CHUNK;
    if(m->bytes!=count+12U) return;
    const uint32_t id=get32(m->data+2),hash=get32(m->data+8);
    if(!id || (id!=s->level_id && !newer(id,s->level_id))) return;
    if(id!=s->level_assembling) {
        if(s->level_assembling && !newer(id,s->level_assembling)) return;
        s->level_assembling=id;s->level_assembling_hash=hash;s->level_mask=0U;
    }
    if(hash!=s->level_assembling_hash) return;
    memcpy(s->level_assembly+part*BC_CHUNK,m->data+12,count);s->level_mask|=1U<<part;
    if(s->level_mask!=(1U<<BC_LEVEL_PARTS)-1U) return;
    s->level_mask=0U;
    bc_level_t next;
    if(hash!=bc_hash(s->level_assembly,BC_LEVEL_BYTES) || !bc_level_decode(&next,s->level_assembly) ||
       bc_level_match_validate(&next)!=BC_LEVEL_OK) return;
    // Duplicate validated transfers only refresh the acknowledgment; never rewind a live match.
    if(id!=s->level_id || !s->level_ready) {
        s->level=next;s->level_id=id;s->level_hash=hash;s->level_ready=true;s->received=false;
        s->received_mask=0U;s->assembling=0U;
        bc_level_apply(&s->world,&s->level);s->world.phase=BC_WAIT;bc_cue(s,BC_S_TRANSFER);
    }
    if(s->world.phase==BC_WAIT) s->silence_ms=0U;
}
static void accept_chunk(bc_state_t *s,const p4_game_multiplayer_message_t *m)
{
    if(m->player_slot || m->bytes<12U || m->data[7] || !s->level_ready || get32(m->data+8)!=s->level_id) return;
    const unsigned part=m->data[6];if(part>=BC_PARTS) return;
    const unsigned count=part==BC_PARTS-1U?BC_SNAPSHOT-part*BC_CHUNK:BC_CHUNK;
    if(m->bytes!=count+12U) return;
    const uint32_t rev=get32(m->data+2);
    if(!rev || !newer(rev,s->revision)) return;
    if(rev!=s->assembling) {
        if(s->assembling && !newer(rev,s->assembling)) return;
        s->assembling=rev;s->received_mask=0U;
    }
    memcpy(s->assembly+part*BC_CHUNK,m->data+12,count);s->received_mask|=1U<<part;
    if(s->received_mask!=(1U<<BC_PARTS)-1U) return;
    s->received_mask=0U;
    bc_world_t next;
    if(!bc_decode(&next,s->assembly) || next.theme!=s->level.theme) return;
    for(unsigned i=0;i<BC_CELLS;++i)
        if((next.tile[i]==BC_WALL)!=(s->level.tile[i]==BC_WALL)) return;
    const bc_world_t before=s->world;s->world=next;
    if(s->received) bc_effects(s,&before);
    else if(next.phase==BC_READY) bc_cue(s,BC_S_COUNT);
    s->received=true;s->revision=rev;s->silence_ms=0U;s->accumulator=0U;
}
bool bc_network_poll(p4_game_context_t *c,bc_state_t *s,uint8_t direction,bool bomb,uint32_t elapsed)
{
    if(!s->network || s->world.phase==BC_LOST) return false;
    p4_game_multiplayer_status_t n;
    if(!p4_game_multiplayer_read_status(c,&n) || !status_ok(&n) || n.generation!=s->generation ||
       n.session_seed!=s->seed || n.player_count!=s->humans || n.local_player_slot!=s->local_slot ||
       ((n.role==P4_GAME_MULTIPLAYER_ROLE_HOST)!=s->host)) {lost(s);return false;}
    s->input_ms+=elapsed;s->snapshot_ms+=elapsed;
    if(s->silence_ms<20000U) s->silence_ms+=elapsed;
    if(s->world.phase==BC_TRANSFER) {
        s->transfer_ms+=elapsed;
        if(s->transfer_ms>10000U) {lost(s);return false;}
    }
    for(unsigned i=1U;i<s->humans;++i) {
        if(s->input_age[i]<10000U) s->input_age[i]+=elapsed;
        if(s->input_age[i]>350U) {s->direction[i]=0U;s->bomb_pressed[i]=false;}
    }
    if(s->host) {
        s->direction[0]=direction;s->bomb_pressed[0]=s->bomb_pressed[0]||bomb;
    } else if(s->input_ms>=50U) {
        if(s->world.phase==BC_WAIT && s->level_ready) {
            uint8_t ack[10]={BC_PROTOCOL,4U};put32(ack+2,s->level_id);put32(ack+6,s->level_hash);
            (void)p4_game_multiplayer_send(c,ack,sizeof(ack));
        } else if(s->world.phase==BC_PLAY) {
            if(bomb) ++s->local_action;
            uint8_t data[10]={BC_PROTOCOL,1U,s->world.round,direction};
            put16(data+4,s->local_action);put32(data+6,s->level_id);
            (void)p4_game_multiplayer_send(c,data,sizeof(data));
        }
        s->input_ms%=50U;
    } else if(!s->host && bomb) ++s->local_action;
    for(unsigned count=0;count<24U;++count) {
        p4_game_multiplayer_message_t m;if(!p4_game_multiplayer_receive(c,&m)) break;
        if(m.bytes<2U || m.data[0]!=BC_PROTOCOL) continue;
        if(!s->host) {
            if(m.player_slot) continue;
            if(m.data[1]==2U) accept_chunk(s,&m);
            else if(m.data[1]==3U) accept_level(s,&m);
            else if(m.data[1]==5U && m.bytes==7U && newer(m.sequence,s->last_sequence[0]) &&
                    get32(m.data+2)==s->level_id &&
                    (m.data[6]==BC_SELECT || m.data[6]==BC_EDITOR || m.data[6]==BC_EDITOR_MENU || m.data[6]==BC_COPY || m.data[6]==BC_TITLE) &&
                    (s->world.phase==BC_WAIT || s->world.phase==BC_MATCH)) {
                s->world.phase=BC_WAIT;s->silence_ms=0U;
            }
            if(newer(m.sequence,s->last_sequence[0])) s->last_sequence[0]=m.sequence;
            continue;
        }
        const unsigned slot=m.player_slot;if(!slot || slot>=s->humans) continue;
        if(m.data[1]==4U && m.bytes==10U && s->world.phase==BC_TRANSFER &&
           get32(m.data+2)==s->level_id && get32(m.data+6)==s->level_hash) {
            s->level_ack|=(uint8_t)(1U<<slot);continue;
        }
        if(m.bytes!=10U || m.data[1]!=1U || s->world.phase!=BC_PLAY ||
           m.data[2]!=s->world.round || get32(m.data+6)!=s->level_id || m.data[3]>8U ||
           (m.data[3] && (m.data[3]&(m.data[3]-1U))) || !newer(m.sequence,s->last_sequence[slot])) continue;
        const uint16_t action=get16(m.data+4);s->last_sequence[slot]=m.sequence;
        s->direction[slot]=m.data[3];s->input_age[slot]=0U;
        const uint16_t delta=(uint16_t)(action-s->action[slot]);
        if(delta && delta<0x8000U) {s->bomb_pressed[slot]=true;s->action[slot]=action;}
    }
    if(s->host && s->world.phase==BC_TRANSFER && s->level_ack==(1U<<s->humans)-1U) {
        bc_restart(s);s->snapshot_ms=100U;
    }
    if(!s->host && s->silence_ms>3000U) {lost(s);return false;}
    return true;
}
void bc_network_publish(p4_game_context_t *c,bc_state_t *s)
{
    if(!s->network || !s->host || s->world.phase==BC_LOST) return;
    const bool transfer=s->world.phase==BC_TRANSFER;
    const bool setup=s->world.phase==BC_SELECT || s->world.phase==BC_EDITOR ||
        s->world.phase==BC_EDITOR_MENU || s->world.phase==BC_COPY || s->world.phase==BC_TITLE;
    const unsigned interval=setup?200U:transfer || s->snapshot_pending?50U:100U;
    if(s->snapshot_ms<interval) return;
    s->snapshot_ms%=interval;
    if(setup) {
        uint8_t data[7]={BC_PROTOCOL,5U};put32(data+2,s->level_id);data[6]=s->world.phase;
        (void)p4_game_multiplayer_send(c,data,sizeof(data));return;
    }
    uint8_t level_bytes[BC_LEVEL_BYTES];
    if(transfer) bc_level_encode(&s->level,level_bytes);
    else if(!s->snapshot_pending) {
        bc_encode(&s->world,s->outgoing_snapshot);
        if(++s->revision==0U) ++s->revision;
        s->snapshot_part=0U;s->snapshot_pending=true;
    }
    const uint8_t *payload=transfer?level_bytes:s->outgoing_snapshot;
    const unsigned size=transfer?BC_LEVEL_BYTES:BC_SNAPSHOT;
    const unsigned parts=transfer?BC_LEVEL_PARTS:BC_PARTS;
    for(unsigned sent=0;sent<parts;++sent) {
        const unsigned part=transfer?s->level_send_part:s->snapshot_part;
        uint8_t data[BC_PACKET]={BC_PROTOCOL,transfer?3U:2U};
        put32(data+2,transfer?s->level_id:s->revision);data[6]=(uint8_t)part;
        put32(data+8,transfer?s->level_hash:s->level_id);
        const unsigned count=part==parts-1U?size-part*BC_CHUNK:BC_CHUNK;
        memcpy(data+12,payload+part*BC_CHUNK,count);
        if(!p4_game_multiplayer_send(c,data,count+12U)) break;
        if(transfer) {
            s->level_send_part=(uint8_t)((part+1U)%parts);
            if(!s->level_send_part) break;
        } else {
            ++s->snapshot_part;
            if(s->snapshot_part==parts) {s->snapshot_pending=false;break;}
        }
    }
}
