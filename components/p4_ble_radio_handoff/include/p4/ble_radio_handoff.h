// SPDX-License-Identifier: MIT

#ifndef P4_BLE_RADIO_HANDOFF_H
#define P4_BLE_RADIO_HANDOFF_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    P4_BLE_RADIO_HANDOFF_READY = 0,
    P4_BLE_RADIO_HANDOFF_CANCEL_GAMEPAD,
    P4_BLE_RADIO_HANDOFF_WAIT,
} p4_ble_radio_handoff_action_t;

typedef struct {
    bool waiting_for_gamepad;
    bool reconnect_suspended;
} p4_ble_radio_handoff_t;

void p4_ble_radio_handoff_init(p4_ble_radio_handoff_t *handoff);

/**
 * Request ownership for multiplayer. A connected controller is allowed to
 * coexist. A disconnected bonded pad or an active pairing/reconnect attempt
 * must first cancel its pending GAP procedure.
 */
p4_ble_radio_handoff_action_t p4_ble_radio_handoff_request(
    p4_ble_radio_handoff_t *handoff,
    bool gamepad_connected,
    bool gamepad_bonded,
    bool gamepad_busy);

/** Poll after CANCEL_GAMEPAD until the controller reports its GAP work idle. */
p4_ble_radio_handoff_action_t p4_ble_radio_handoff_poll(
    p4_ble_radio_handoff_t *handoff,
    bool gamepad_radio_idle);

/**
 * Release multiplayer ownership. Returns true only when the saved controller
 * reconnect that was suspended should be restarted.
 */
bool p4_ble_radio_handoff_release(
    p4_ble_radio_handoff_t *handoff,
    bool gamepad_mode_enabled,
    bool gamepad_connected,
    bool gamepad_bonded);

#ifdef __cplusplus
}
#endif

#endif
