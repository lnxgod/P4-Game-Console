// SPDX-License-Identifier: MIT
#ifndef P4_DOOM_RESUME_H
#define P4_DOOM_RESUME_H

#include "p4/multiplayer.h"

enum { P4_DOOM_RESUME_TICKET_BYTES = 16, P4_DOOM_RESUME_CONTROL_BYTES = 64,
       P4_DOOM_RESUME_RECORD_BYTES = 80 };
enum { P4_DOOM_RESUME_REASON_PROVISIONAL_EXPIRED = 3 };
typedef enum {
    P4_DOOM_RESUME_TICKET = 1,
    P4_DOOM_RESUME_TICKET_ACK,
    P4_DOOM_RESUME_REQUEST,
    P4_DOOM_RESUME_ACCEPTED,
    P4_DOOM_RESUME_UNAVAILABLE,
} p4_doom_resume_type_t;

/* GCR2 Arena-specific GAME_MESSAGE controls; generic lobby wire stays unchanged.
 * Tickets identify a reserved slot. The local open Wi-Fi transport does not
 * encrypt them and this protocol does not claim cryptographic authentication.
 * initial_player_mask is immutable tic-zero history: it includes the host but
 * need not include this guest's slot. UNAVAILABLE has no capacity field and
 * validates its mask against P4_MP_MAX_PLAYERS instead. */
typedef struct {
    p4_doom_resume_type_t type;
    uint8_t slot, player_count, reason, initial_player_mask;
    uint8_t ticket[P4_DOOM_RESUME_TICKET_BYTES];
    uint64_t nonce;
    uint8_t compatibility_sha256[P4_MP_SHA256_BYTES];
    p4_mp_lobby_accept_t accept;
} p4_doom_resume_control_t;

typedef struct {
    uint32_t session_id, self_peer_id, host_peer_id;
    uint64_t session_seed;
    uint8_t slot, player_count, initial_player_mask;
    uint8_t ticket[P4_DOOM_RESUME_TICKET_BYTES];
    uint8_t compatibility_sha256[P4_MP_SHA256_BYTES];
} p4_doom_resume_record_t;

bool p4_doom_resume_control_encode(const p4_doom_resume_control_t *,
    uint8_t output[P4_DOOM_RESUME_CONTROL_BYTES], size_t *length);
bool p4_doom_resume_control_decode(const uint8_t *, size_t, p4_doom_resume_control_t *);
bool p4_doom_resume_record_encode(const p4_doom_resume_record_t *,
    uint8_t output[P4_DOOM_RESUME_RECORD_BYTES]);
bool p4_doom_resume_record_decode(const uint8_t input[P4_DOOM_RESUME_RECORD_BYTES],
    p4_doom_resume_record_t *);

#endif
