// SPDX-License-Identifier: MIT
#include "p4/doom_lockstep.h"
#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8U * i)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }

bool p4_doom_lockstep_init(p4_doom_lockstep_t *s, uint8_t slot, uint8_t count)
{
    if (!s || count < 2 || count > 4 || slot >= count) return false;
    memset(s, 0, sizeof(*s));
    s->slot = slot; s->count = count; s->mask = (uint8_t)((1U << count) - 1U);
    p4_doom_mp_tx_window_init(&s->local, 0);
    return p4_doom_mp_tic_queue_init(&s->input, count, 0);
}

bool p4_doom_lockstep_submit(p4_doom_lockstep_t *s, const p4_doom_mp_tic_t *tic)
{
    if (!s || !tic) return false;
    if (s->slot == 0) return p4_doom_mp_tic_queue_submit(&s->input, 0, tic);
    return p4_doom_mp_tx_window_track(&s->local, tic);
}

bool p4_doom_lockstep_ack(p4_doom_lockstep_t *s, uint8_t sender, uint32_t ack)
{
    if (!s || s->slot != 0 || sender == 0 || sender >= s->count ||
        !(s->mask & (1U << sender)) || ack < s->peer_ack[sender] ||
        ack > s->next_output) return false;
    s->peer_ack[sender] = ack;
    return true;
}

bool p4_doom_lockstep_input(p4_doom_lockstep_t *s, uint8_t sender,
                            const p4_doom_mp_tic_t *tic, uint32_t ack)
{
    if (!s || !tic || s->slot != 0 || sender == 0 || sender >= s->count ||
        !(s->mask & (1U << sender))) return false;
    if (!p4_doom_lockstep_ack(s, sender, ack)) return false;
    /* Retries of commands already committed are harmless. */
    if (tic->tick < s->next_output) return true;
    return p4_doom_mp_tic_queue_submit(&s->input, sender, tic);
}

void p4_doom_lockstep_pump(p4_doom_lockstep_t *s)
{
    if (!s || s->slot != 0) return;
    uint32_t oldest = s->next_read;
    for (uint8_t i = 1; i < s->count; ++i)
        if ((s->mask & (1U << i)) && s->peer_ack[i] < oldest) oldest = s->peer_ack[i];
    while (s->next_output != UINT32_MAX &&
           s->next_output - oldest < P4_DOOM_LOCKSTEP_HISTORY &&
           p4_doom_mp_tic_queue_ready(&s->input)) {
        p4_doom_lockstep_frame_t *f = &s->history[s->next_output % P4_DOOM_LOCKSTEP_HISTORY];
        f->tick = s->next_output;
        if (!p4_doom_mp_tic_queue_pop(&s->input, f->commands, &f->mask)) return;
        ++s->next_output;
    }
}

bool p4_doom_lockstep_pop(p4_doom_lockstep_t *s, p4_doom_lockstep_frame_t *out)
{
    if (!s || !out || s->next_read >= s->next_output) return false;
    *out = s->history[s->next_read % P4_DOOM_LOCKSTEP_HISTORY];
    ++s->next_read;
    return true;
}

bool p4_doom_lockstep_packet(const p4_doom_lockstep_t *s, uint8_t recipient,
                             uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES])
{
    if (!s || !bytes || s->slot != 0 || recipient == 0 || recipient >= s->count ||
        !(s->mask & (1U << recipient)) || s->peer_ack[recipient] >= s->next_output)
        return false;
    const p4_doom_lockstep_frame_t *f = &s->history[s->peer_ack[recipient] % P4_DOOM_LOCKSTEP_HISTORY];
    memset(bytes, 0, P4_DOOM_LOCKSTEP_BYTES);
    bytes[0]='G'; bytes[1]='C'; bytes[2]=1; bytes[3]=f->mask;
    put32(bytes+4, f->tick);
    for (uint8_t i = 0; i < s->count; ++i) {
        const p4_doom_mp_tic_t *t = &f->commands[i];
        uint8_t *p = bytes+8+8U*i;
        p[0]=(uint8_t)t->forward_move; p[1]=(uint8_t)t->side_move;
        p[2]=(uint8_t)t->angle_turn; p[3]=(uint8_t)((uint16_t)t->angle_turn>>8);
        p[4]=t->buttons; p[5]=t->consistency; p[6]=t->chat_char;
    }
    return true;
}

bool p4_doom_lockstep_receive(p4_doom_lockstep_t *s, const uint8_t *bytes, size_t n)
{
    if (!s || !bytes || s->slot == 0 || n != P4_DOOM_LOCKSTEP_BYTES ||
        bytes[0]!='G' || bytes[1]!='C' || bytes[2]!=1) return false;
    const uint8_t mask = bytes[3];
    const uint32_t tick = get32(bytes+4);
    /* Reject reorder rather than partially applying it: sender retries ack. */
    if (tick != s->next_output || tick == UINT32_MAX || !(mask & 1U) ||
        !(mask & (1U << s->slot)) || (mask & s->mask) != mask ||
        s->next_output - s->next_read >= P4_DOOM_LOCKSTEP_HISTORY) return false;
    p4_doom_lockstep_frame_t f = {.tick=tick, .mask=mask};
    for (uint8_t i = 0; i < P4_MP_MAX_PLAYERS; ++i) {
        const uint8_t *p = bytes+8+8U*i;
        if (p[7]) return false;
        if (!(mask & (1U << i))) {
            for (unsigned j = 0; j < 7; ++j) if (p[j]) return false;
        }
        f.commands[i] = (p4_doom_mp_tic_t){ .tick=tick,
            .forward_move=(int8_t)p[0], .side_move=(int8_t)p[1],
            .angle_turn=(int16_t)((uint16_t)p[2] | (uint16_t)((uint16_t)p[3]<<8)),
            .buttons=p[4], .consistency=p[5], .chat_char=p[6] };
    }
    /* Host can only commit our input after receiving it. */
    if (!p4_doom_mp_tx_window_acknowledge(&s->local, tick+1U)) return false;
    s->history[tick % P4_DOOM_LOCKSTEP_HISTORY] = f;
    s->mask = mask;
    ++s->next_output;
    return true;
}

bool p4_doom_lockstep_depart(p4_doom_lockstep_t *s, uint8_t slot)
{
    if (!s || s->slot != 0 || slot == 0 || slot >= s->count) return false;
    s->mask &= (uint8_t)~(1U << slot);
    return p4_doom_mp_tic_queue_disconnect(&s->input, slot);
}
