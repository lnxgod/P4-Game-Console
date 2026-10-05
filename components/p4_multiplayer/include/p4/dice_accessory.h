// SPDX-License-Identifier: MIT
#ifndef P4_DICE_ACCESSORY_H
#define P4_DICE_ACCESSORY_H
#include "p4/dice.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Separate P4MP accessory GATT service: accessories never occupy player slots.
 * Fixed 48-byte LE payload, carried in a normal CRC-checked P4MP packet. */
enum { P4_DICE_WIRE_BYTES = 48, P4_DICE_REQUEST = 1, P4_DICE_STATUS = 2 };
#define P4_DICE_SERVICE_UUID "7b0d9f20-6f44-4a0d-9c9e-50344d500010"
#define P4_DICE_CHARACTERISTIC_UUID "7b0d9f20-6f44-4a0d-9c9e-50344d500011"
bool p4_dice_request_valid(const p4_dice_request_t *request);
bool p4_dice_encode(const p4_dice_request_t *request, p4_dice_phase_t phase,
    uint8_t kind, uint8_t bytes[P4_DICE_WIRE_BYTES]);
bool p4_dice_decode(const uint8_t *bytes, size_t length,
    uint8_t kind, p4_dice_request_t *request, p4_dice_phase_t *phase);
bool p4_dice_accept_status(const p4_dice_request_t *wanted,
    const p4_dice_request_t *reply, p4_dice_phase_t phase,
    p4_dice_status_t *out);
/** Portable 100 Hz motion gate. Acceleration is in milli-g, not raw IMU counts. */
typedef struct {
    p4_dice_phase_t phase;
    uint32_t token, armed_ms, first_peak_ms, last_peak_ms, quiet_ms;
    uint8_t peaks;
    bool above;
} p4_dice_gesture_t;
void p4_dice_gesture_reset(p4_dice_gesture_t *gesture, uint32_t token);
bool p4_dice_gesture_ready(p4_dice_gesture_t *gesture, uint32_t now_ms);
bool p4_dice_gesture_sample(p4_dice_gesture_t *gesture, uint32_t now_ms,
    int32_t x_mg, int32_t y_mg, int32_t z_mg);
#ifdef __cplusplus
}
#endif
#endif
