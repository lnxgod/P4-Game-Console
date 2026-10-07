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
    if (!s || !tic || (s->replaying && !s->live_input_prepared)) return false;
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
        !((s->mask | s->pending_mask) & (1U << sender))) return false;
    if (s->pending_mask & (1U << sender)) {
        if (ack != s->activate_at[sender] || tic->tick < s->activate_at[sender])
            return false;
        /* Keep future input while excluding this slot from readiness/history
         * until its exact activation tic. This component has one owner. */
        const uint8_t connected = s->input.connected_mask;
        s->input.connected_mask |= (uint8_t)(1U << sender);
        const bool accepted = p4_doom_mp_tic_queue_submit(&s->input, sender, tic);
        s->input.connected_mask = connected;
        return accepted;
    }
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
           s->next_output - oldest < P4_DOOM_LOCKSTEP_HISTORY) {
        /* Decide an admission only when existing players can commit this tic.
         * A returning console must never stall them with a missing first input. */
        if (!p4_doom_mp_tic_queue_ready(&s->input)) break;
        for (uint8_t i = 1; i < s->count; ++i) {
            const uint8_t bit = (uint8_t)(1U << i);
            if ((s->pending_mask & bit) && s->activate_at[i] == s->next_output) {
                const size_t index = s->next_output % P4_DOOM_MP_TIC_RING_SIZE;
                if (!s->input.valid[i][index] ||
                    s->input.tags[i][index] != s->next_output) {
                    (void)p4_doom_lockstep_depart(s, i);
                    continue;
                }
                s->pending_mask &= (uint8_t)~bit;
                s->mask |= bit;
                s->input.connected_mask |= bit;
                if (s->peer_ack[i] < oldest) oldest = s->peer_ack[i];
            }
        }
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

bool p4_doom_lockstep_packet_at(const p4_doom_lockstep_t *s, uint8_t recipient,
                                uint32_t tick, uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES])
{
    if (!s || !bytes || s->slot != 0 || s->count < 2 || s->count > P4_MP_MAX_PLAYERS ||
        recipient == 0 || recipient >= s->count || !(s->mask & (1U << recipient)) ||
        tick < s->peer_ack[recipient] || tick >= s->next_output ||
        s->next_output - tick > P4_DOOM_LOCKSTEP_HISTORY)
        return false;
    const p4_doom_lockstep_frame_t *f = &s->history[tick % P4_DOOM_LOCKSTEP_HISTORY];
    return f->tick == tick && p4_doom_lockstep_frame_encode(f, s->count, bytes);
}

bool p4_doom_lockstep_packet(const p4_doom_lockstep_t *s, uint8_t recipient,
                             uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES])
{
    if (!s || recipient >= P4_MP_MAX_PLAYERS) return false;
    return p4_doom_lockstep_packet_at(s, recipient, s->peer_ack[recipient], bytes);
}

bool p4_doom_lockstep_frame_encode(const p4_doom_lockstep_frame_t *f, uint8_t count,
                                  uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES])
{
    if (!f || !bytes || count < 2 || count > P4_MP_MAX_PLAYERS ||
        f->tick == UINT32_MAX || !(f->mask & 1U) ||
        (f->mask & (uint8_t)~((1U << count) - 1U))) return false;
    for (uint8_t i = 0; i < P4_MP_MAX_PLAYERS; ++i) {
        const p4_doom_mp_tic_t *t = &f->commands[i];
        if (t->tick != f->tick || (!(f->mask & (1U << i)) &&
            (t->forward_move || t->side_move || t->angle_turn || t->buttons ||
             t->consistency || t->chat_char))) return false;
    }
    memset(bytes, 0, P4_DOOM_LOCKSTEP_BYTES);
    bytes[0]='G'; bytes[1]='C'; bytes[2]=1; bytes[3]=f->mask;
    put32(bytes+4, f->tick);
    for (uint8_t i = 0; i < count; ++i) {
        const p4_doom_mp_tic_t *t = &f->commands[i];
        uint8_t *p = bytes+8+8U*i;
        p[0]=(uint8_t)t->forward_move; p[1]=(uint8_t)t->side_move;
        p[2]=(uint8_t)t->angle_turn; p[3]=(uint8_t)((uint16_t)t->angle_turn>>8);
        p[4]=t->buttons; p[5]=t->consistency; p[6]=t->chat_char;
    }
    return true;
}

bool p4_doom_lockstep_frame_decode(const uint8_t *bytes, size_t n, uint8_t count,
                                  p4_doom_lockstep_frame_t *out)
{
    if (!bytes || !out || count < 2 || count > P4_MP_MAX_PLAYERS ||
        n != P4_DOOM_LOCKSTEP_BYTES ||
        bytes[0]!='G' || bytes[1]!='C' || bytes[2]!=1) return false;
    const uint8_t mask = bytes[3];
    const uint32_t tick = get32(bytes+4);
    if (tick == UINT32_MAX || !(mask & 1U) ||
        (mask & (uint8_t)~((1U << count) - 1U))) return false;
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
    *out = f;
    return true;
}

static bool receive_frame(p4_doom_lockstep_t *s, const uint8_t *bytes, size_t n,
                          bool replay)
{
    if (!s || s->slot == 0 || s->replaying != replay) return false;
    p4_doom_lockstep_frame_t f;
    if (!p4_doom_lockstep_frame_decode(bytes, n, s->count, &f) ||
        f.tick != s->next_output ||
        (replay && s->live_input_prepared && f.tick >= s->live_input_at) ||
        (!replay && !(f.mask & (1U << s->slot))) ||
        s->next_output - s->next_read >= P4_DOOM_LOCKSTEP_HISTORY) return false;
    /* Live host can only commit our input after receiving it. Historical
     * commands were produced by the previous engine and need no local ack. */
    if (!replay && !p4_doom_mp_tx_window_acknowledge(&s->local, f.tick+1U))
        return false;
    s->history[f.tick % P4_DOOM_LOCKSTEP_HISTORY] = f;
    s->mask = f.mask;
    ++s->next_output;
    return true;
}

bool p4_doom_lockstep_receive(p4_doom_lockstep_t *s, const uint8_t *bytes, size_t n)
{ return receive_frame(s, bytes, n, false); }

bool p4_doom_lockstep_depart(p4_doom_lockstep_t *s, uint8_t slot)
{
    if (!s || s->slot != 0 || slot == 0 || slot >= s->count ||
        !((s->mask | s->pending_mask) & (1U << slot))) return false;
    const uint8_t bit = (uint8_t)(1U << slot);
    s->mask &= (uint8_t)~bit;
    s->pending_mask &= (uint8_t)~bit;
    s->activate_at[slot] = 0;
    /* Pending admission's queued commands must be cleared too. */
    s->input.connected_mask |= bit;
    return p4_doom_mp_tic_queue_disconnect(&s->input, slot);
}

bool p4_doom_lockstep_reactivate(p4_doom_lockstep_t *s, uint8_t slot, uint32_t tick)
{
    if (!s || s->slot != 0 || slot == 0 || slot >= s->count ||
        ((s->mask | s->pending_mask) & (1U << slot)) || tick == UINT32_MAX ||
        tick <= s->next_output || tick - s->next_output >= P4_DOOM_MP_TIC_RING_SIZE)
        return false;
    s->activate_at[slot] = tick;
    s->peer_ack[slot] = tick;
    s->pending_mask |= (uint8_t)(1U << slot);
    return true;
}

bool p4_doom_lockstep_replay_begin(p4_doom_lockstep_t *s)
{
    if (!s || s->slot == 0 || s->replaying || s->next_read || s->next_output ||
        s->local.next_local_tick || s->local.pending_count) return false;
    s->replaying = true;
    return true;
}

bool p4_doom_lockstep_replay_checkpoint(p4_doom_lockstep_t *s, uint32_t next_tick,
                                       uint8_t members)
{
    if (!s || s->count < 2 || s->count > P4_MP_MAX_PLAYERS ||
        s->slot == 0 || s->slot >= s->count || !s->replaying ||
        s->live_input_prepared || s->local.pending_count ||
        next_tick == UINT32_MAX || !(members & 1U) ||
        (members & (uint8_t)~((1U << s->count) - 1U))) return false;
    const uint8_t slot = s->slot, count = s->count;
    /* Validation above makes init infallible; no large temporary sync object
     * is needed on the embedded stack, and every rejection leaves s intact. */
    (void)p4_doom_lockstep_init(s, slot, count);
    s->next_output = s->next_read = next_tick;
    s->input.next_tick = next_tick;
    s->input.connected_mask = s->mask = members;
    p4_doom_mp_tx_window_init(&s->local, next_tick);
    s->replaying = true;
    return true;
}

bool p4_doom_lockstep_receive_replay(p4_doom_lockstep_t *s,
                                   const uint8_t *bytes, size_t n)
{ return receive_frame(s, bytes, n, true); }

bool p4_doom_lockstep_replay_finish(p4_doom_lockstep_t *s, uint32_t activation_tick)
{
    if (!s || !s->replaying || activation_tick == UINT32_MAX ||
        s->next_output != activation_tick || s->next_read != activation_tick ||
        (s->live_input_prepared && s->live_input_at != activation_tick))
        return false;
    if (!s->live_input_prepared)
        p4_doom_mp_tx_window_init(&s->local, activation_tick);
    s->live_input_prepared = false;
    s->live_input_at = 0;
    s->replaying = false;
    return true;
}

bool p4_doom_lockstep_prepare_live_input(p4_doom_lockstep_t *s, uint32_t activation_tick)
{
    if (!s || s->slot == 0 || !s->replaying || activation_tick == UINT32_MAX)
        return false;
    if (s->live_input_prepared) return s->live_input_at == activation_tick;
    if (activation_tick < s->next_output ||
        activation_tick - s->next_read >= P4_DOOM_MP_TIC_RING_SIZE) return false;
    p4_doom_mp_tx_window_init(&s->local, activation_tick);
    s->live_input_at = activation_tick;
    s->live_input_prepared = true;
    return true;
}
