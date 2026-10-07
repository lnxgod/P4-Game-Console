// SPDX-License-Identifier: MIT
#include "p4/doom_resume.h"
#include <string.h>

static uint32_t r32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t r64(const uint8_t *p) { return r32(p) | ((uint64_t)r32(p+4)<<32); }
static void w32(uint8_t *p, uint32_t n)
{ for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(8*i)); }
static void w64(uint8_t *p, uint64_t n)
{ w32(p,(uint32_t)n); w32(p+4,(uint32_t)(n>>32)); }
static bool nonzero(const uint8_t *p, size_t n)
{ uint8_t bits=0; for (size_t i=0;i<n;++i) bits|=p[i]; return bits!=0; }
static bool slot_valid(uint8_t slot, uint8_t count)
{ return count>=2 && count<=P4_MP_MAX_PLAYERS && slot>0 && slot<count; }
static bool mask_valid(uint8_t mask, uint8_t count)
{
    return count>=2 && count<=P4_MP_MAX_PLAYERS && (mask & 1U)!=0U &&
        (mask & ~((1U<<count)-1U))==0U;
}

static size_t control_length(const p4_doom_resume_control_t *c)
{
    if (!c || !nonzero(c->ticket,sizeof(c->ticket)) || c->slot==0 || c->slot>=P4_MP_MAX_PLAYERS)
        return 0;
    if (c->type==P4_DOOM_RESUME_UNAVAILABLE)
        return c->reason && c->nonce &&
            mask_valid(c->initial_player_mask,P4_MP_MAX_PLAYERS) ? 32U : 0U;
    if (!slot_valid(c->slot,c->player_count) ||
        !mask_valid(c->initial_player_mask,c->player_count)) return 0;
    if (c->type==P4_DOOM_RESUME_TICKET || c->type==P4_DOOM_RESUME_TICKET_ACK) return 24;
    if (!c->nonce) return 0;
    if (c->type==P4_DOOM_RESUME_REQUEST)
        return nonzero(c->compatibility_sha256,sizeof(c->compatibility_sha256)) ? 64U : 0U;
    if (c->type==P4_DOOM_RESUME_ACCEPTED && c->accept.assigned_player_slot==c->slot &&
        c->accept.player_count==c->player_count && c->accept.start_tic==0) return 56;
    return 0;
}

bool p4_doom_resume_control_encode(const p4_doom_resume_control_t *c,
    uint8_t output[P4_DOOM_RESUME_CONTROL_BYTES], size_t *length)
{
    if (!output || !length) return false;
    *length=0;
    const size_t n=control_length(c);
    if (!n) return false;
    memset(output,0,P4_DOOM_RESUME_CONTROL_BYTES);
    memcpy(output,"GCR2",4); output[4]=(uint8_t)c->type; output[5]=c->slot;
    output[6]=c->type==P4_DOOM_RESUME_UNAVAILABLE ? c->reason : c->player_count;
    output[7]=c->initial_player_mask;
    memcpy(output+8,c->ticket,sizeof(c->ticket));
    if (n>=32) w64(output+24,c->nonce);
    if (c->type==P4_DOOM_RESUME_REQUEST) memcpy(output+32,c->compatibility_sha256,32);
    if (c->type==P4_DOOM_RESUME_ACCEPTED &&
        p4_mp_lobby_accept_encode(&c->accept,output+32)!=P4_MP_OK) return false;
    *length=n; return true;
}

bool p4_doom_resume_control_decode(const uint8_t *p, size_t n, p4_doom_resume_control_t *c)
{
    if (!c) return false;
    memset(c,0,sizeof(*c));
    if (!p || n<24 || n>64 || memcmp(p,"GCR2",4)) return false;
    c->type=(p4_doom_resume_type_t)p[4]; c->slot=p[5];
    if (c->type==P4_DOOM_RESUME_UNAVAILABLE) c->reason=p[6]; else c->player_count=p[6];
    c->initial_player_mask=p[7];
    memcpy(c->ticket,p+8,16);
    if (n>=32) c->nonce=r64(p+24);
    if (c->type==P4_DOOM_RESUME_REQUEST && n==64) memcpy(c->compatibility_sha256,p+32,32);
    if (c->type==P4_DOOM_RESUME_ACCEPTED && (n!=56 ||
        p4_mp_lobby_accept_decode(p+32,24,&c->accept)!=P4_MP_OK)) return false;
    return control_length(c)==n;
}

static bool record_valid(const p4_doom_resume_record_t *r)
{
    return r && r->session_id && r->self_peer_id && r->host_peer_id &&
        r->self_peer_id!=r->host_peer_id && r->session_seed &&
        slot_valid(r->slot,r->player_count) &&
        mask_valid(r->initial_player_mask,r->player_count) &&
        nonzero(r->ticket,sizeof(r->ticket)) &&
        nonzero(r->compatibility_sha256,sizeof(r->compatibility_sha256));
}
bool p4_doom_resume_record_encode(const p4_doom_resume_record_t *r, uint8_t output[80])
{
    if (!output || !record_valid(r)) return false;
    memset(output,0,80); memcpy(output,"GCRT",4); output[4]=2;
    output[5]=r->slot; output[6]=r->player_count; output[7]=r->initial_player_mask;
    w32(output+8,r->session_id); w32(output+12,r->self_peer_id); w32(output+16,r->host_peer_id);
    w64(output+20,r->session_seed); memcpy(output+28,r->ticket,16);
    memcpy(output+44,r->compatibility_sha256,32); w32(output+76,p4_mp_crc32(output,76));
    return true;
}
bool p4_doom_resume_record_decode(const uint8_t input[80], p4_doom_resume_record_t *r)
{
    if (!r) return false;
    memset(r,0,sizeof(*r));
    if (!input || memcmp(input,"GCRT",4) || input[4]!=2 ||
        r32(input+76)!=p4_mp_crc32(input,76)) return false;
    r->slot=input[5]; r->player_count=input[6]; r->initial_player_mask=input[7];
    r->session_id=r32(input+8);
    r->self_peer_id=r32(input+12); r->host_peer_id=r32(input+16); r->session_seed=r64(input+20);
    memcpy(r->ticket,input+28,16); memcpy(r->compatibility_sha256,input+44,32);
    return record_valid(r);
}
