// SPDX-License-Identifier: MIT
#ifndef P4_MOTION_H
#define P4_MOTION_H
#include <stdbool.h>
#include <stdint.h>
/* Copied physical sensor axes, milligravity and millidegrees/second.
 * No hardware handles; games calibrate a neutral pose and choose orientation. */
typedef struct {
    uint32_t sequence;
    uint16_t age_ms;
    bool valid;
    int32_t accel_mg[3];
    int32_t gyro_mdps[3];
} p4_game_motion_t;
typedef bool (*p4_game_read_motion_fn)(void *context, p4_game_motion_t *out);
#endif
