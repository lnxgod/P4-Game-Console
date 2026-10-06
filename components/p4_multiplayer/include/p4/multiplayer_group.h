// SPDX-License-Identifier: MIT
#ifndef P4_MULTIPLAYER_GROUP_H
#define P4_MULTIPLAYER_GROUP_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
enum { P4_MP_GROUP_BYTES=12, P4_MP_GROUP_HOLD_MS=1000, P4_MP_GROUP_TIMEOUT_MS=8000 };
typedef enum { P4_MP_GROUP_IDLE, P4_MP_GROUP_WAITING, P4_MP_GROUP_COMMITTED,
 P4_MP_GROUP_DUE, P4_MP_GROUP_FAILED } p4_mp_group_phase_t;
typedef struct {
 p4_mp_group_phase_t phase;
 uint64_t started_ms,launch_ms,next_send_ms;
 uint16_t token;
 uint8_t slot,count,ready_mask;
 bool host;
} p4_mp_group_start_t;
bool p4_mp_group_begin(p4_mp_group_start_t *,uint16_t token,uint8_t count,uint64_t now_ms);
bool p4_mp_group_receive(p4_mp_group_start_t *,uint8_t local_slot,uint8_t sender,
 uint16_t expected_token,const uint8_t *,size_t,uint64_t now_ms);
/* Returns a retry packet when due; the OS sends it over its existing session. */
bool p4_mp_group_poll(p4_mp_group_start_t *,uint64_t now_ms,uint8_t out[P4_MP_GROUP_BYTES]);
#endif
