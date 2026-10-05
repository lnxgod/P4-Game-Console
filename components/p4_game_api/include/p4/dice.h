// SPDX-License-Identifier: MIT
#ifndef P4_GAME_DICE_H
#define P4_GAME_DICE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { P4_DICE_MAX = 8, P4_DICE_NAME_BYTES = 20 };
typedef enum {
    P4_DICE_OFFLINE = 0, P4_DICE_SEARCHING, P4_DICE_WAITING,
    P4_DICE_READY, P4_DICE_SHAKING, P4_DICE_ROLLED
} p4_dice_phase_t;
/** A new token invalidates Ready and any shake from the previous turn/roll. */
typedef struct {
    uint32_t token;
    uint8_t player_slot, count, sides, held_mask;
    uint8_t faces[P4_DICE_MAX];
    char player_name[P4_DICE_NAME_BYTES];
    bool enabled;
    /** v2: allow choosing held dice even when all dice are currently held. */
    bool can_hold;
    uint16_t hold_ack;
} p4_dice_request_t;
typedef struct {
    uint32_t token;
    p4_dice_phase_t phase;
    uint8_t player_slot;
    uint8_t held_mask;
    bool hold_changed;
    uint16_t hold_sequence;
} p4_dice_status_t;
typedef bool (*p4_game_dice_exchange_fn)(void *context,
    const p4_dice_request_t *request, p4_dice_status_t *status);
#ifdef __cplusplus
}
#endif
#endif
