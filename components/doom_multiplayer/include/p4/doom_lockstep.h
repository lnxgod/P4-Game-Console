// SPDX-License-Identifier: MIT
#ifndef P4_DOOM_LOCKSTEP_H
#define P4_DOOM_LOCKSTEP_H
#include "p4/doom_multiplayer.h"

/* Host relays canonical command sets; guests never authenticate another guest.
 * Acknowledgments are next-required tics, independent of transport sequences.
 * Output is retained until every remaining guest acknowledges it. */
enum { P4_DOOM_LOCKSTEP_BYTES = 40, P4_DOOM_LOCKSTEP_HISTORY = 32 };
typedef struct {
    p4_doom_mp_tic_t commands[P4_MP_MAX_PLAYERS];
    uint32_t tick;
    uint8_t mask;
} p4_doom_lockstep_frame_t;
typedef struct {
    p4_doom_mp_tic_queue_t input;
    p4_doom_mp_tx_window_t local;
    p4_doom_lockstep_frame_t history[P4_DOOM_LOCKSTEP_HISTORY];
    uint32_t peer_ack[P4_MP_MAX_PLAYERS], next_output, next_read;
    uint32_t activate_at[P4_MP_MAX_PLAYERS], live_input_at;
    uint8_t slot, count, mask, pending_mask;
    bool replaying, live_input_prepared;
} p4_doom_lockstep_t;

bool p4_doom_lockstep_init(p4_doom_lockstep_t *, uint8_t slot, uint8_t count);
bool p4_doom_lockstep_submit(p4_doom_lockstep_t *, const p4_doom_mp_tic_t *);
/* INPUT is accepted only by slot zero from its authenticated sender slot. */
bool p4_doom_lockstep_input(p4_doom_lockstep_t *, uint8_t sender,
                            const p4_doom_mp_tic_t *, uint32_t ack);
/* Client accepts a complete ordered frame only from authenticated slot zero. */
bool p4_doom_lockstep_receive(p4_doom_lockstep_t *, const uint8_t *, size_t);
bool p4_doom_lockstep_ack(p4_doom_lockstep_t *, uint8_t sender, uint32_t ack);
/* Produce bounded canonical frames on host; no packet I/O. */
void p4_doom_lockstep_pump(p4_doom_lockstep_t *);
bool p4_doom_lockstep_pop(p4_doom_lockstep_t *, p4_doom_lockstep_frame_t *);
/* Retry the oldest frame needed by this client; encoder fits GAME_MESSAGE. */
bool p4_doom_lockstep_packet(const p4_doom_lockstep_t *, uint8_t recipient,
                             uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES]);
/* Read-only bounded live pipeline: only an active recipient's unacknowledged,
 * committed, still-retained exact tic can be encoded. Does not advance ACK,
 * retire history, alter receive ordering, or change the wire format. */
bool p4_doom_lockstep_packet_at(const p4_doom_lockstep_t *, uint8_t recipient,
                                uint32_t tick, uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES]);
/* Host-only roster removal at the next new frame; also cancels pending
 * reactivation and erases that slot's queued input. */
bool p4_doom_lockstep_depart(p4_doom_lockstep_t *, uint8_t slot);
/* Host-only reactivation of an absent reserved slot at a future canonical tic.
 * Pending slots can supply commands but never retain/backpressure history.
 * If first input is absent when existing players can commit that tic, the
 * admission is canceled (activate_at reset to zero) and play continues.
 * The caller must first authenticate and synchronize the returning engine. */
bool p4_doom_lockstep_reactivate(p4_doom_lockstep_t *, uint8_t slot, uint32_t tick);
/* Replay starts only on a fresh client and ends after every historical frame
 * before activation_tick was consumed. Session/route authentication is owned
 * by the caller; receive_replay must never accept another client's packets. */
bool p4_doom_lockstep_replay_begin(p4_doom_lockstep_t *);
/* Cold checkpoint restore on an explicitly replaying guest only. The caller
 * authenticates/restores engine state before rebasing to its next canonical
 * tic and roster (host required; this guest may still be absent). Clears old
 * historical queues, but rejects prepared/pending live input and never rewinds
 * a host or live client. Engine-specific tic bounds belong to the caller. */
bool p4_doom_lockstep_replay_checkpoint(p4_doom_lockstep_t *, uint32_t next_tick,
                                       uint8_t members);
bool p4_doom_lockstep_receive_replay(p4_doom_lockstep_t *, const uint8_t *, size_t);
/* Arm TX at an authenticated future activation tic while history is still
 * being replayed. The engine must enforce its own consistency-ring lead bound.
 * Historical receive stops before this tic; finish preserves pending live TX. */
bool p4_doom_lockstep_prepare_live_input(p4_doom_lockstep_t *, uint32_t activation_tick);
bool p4_doom_lockstep_replay_finish(p4_doom_lockstep_t *, uint32_t activation_tick);
/* Stateless canonical codec, shared by live lockstep and bounded journals. */
bool p4_doom_lockstep_frame_encode(const p4_doom_lockstep_frame_t *, uint8_t count,
                                  uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES]);
bool p4_doom_lockstep_frame_decode(const uint8_t *, size_t, uint8_t count,
                                  p4_doom_lockstep_frame_t *);
#endif
