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
    uint8_t slot, count, mask;
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
/* Host-only, monotonic roster removal, taking effect in the next new frame. */
bool p4_doom_lockstep_depart(p4_doom_lockstep_t *, uint8_t slot);
#endif
