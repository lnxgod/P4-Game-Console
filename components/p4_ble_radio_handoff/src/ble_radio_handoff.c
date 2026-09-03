// SPDX-License-Identifier: MIT

#include "p4/ble_radio_handoff.h"

#include <stddef.h>

void p4_ble_radio_handoff_init(p4_ble_radio_handoff_t *handoff)
{
    if (handoff == NULL) {
        return;
    }
    *handoff = (p4_ble_radio_handoff_t){0};
}

p4_ble_radio_handoff_action_t p4_ble_radio_handoff_request(
    p4_ble_radio_handoff_t *handoff,
    bool gamepad_connected,
    bool gamepad_bonded,
    bool gamepad_busy)
{
    if (handoff == NULL) {
        return P4_BLE_RADIO_HANDOFF_WAIT;
    }
    if (handoff->waiting_for_gamepad) {
        return P4_BLE_RADIO_HANDOFF_WAIT;
    }
    if (gamepad_connected || (!gamepad_bonded && !gamepad_busy)) {
        return P4_BLE_RADIO_HANDOFF_READY;
    }
    handoff->waiting_for_gamepad = true;
    handoff->reconnect_suspended = gamepad_bonded;
    return P4_BLE_RADIO_HANDOFF_CANCEL_GAMEPAD;
}

p4_ble_radio_handoff_action_t p4_ble_radio_handoff_poll(
    p4_ble_radio_handoff_t *handoff,
    bool gamepad_radio_idle)
{
    if (handoff == NULL) {
        return P4_BLE_RADIO_HANDOFF_WAIT;
    }
    if (!handoff->waiting_for_gamepad) {
        return P4_BLE_RADIO_HANDOFF_READY;
    }
    if (!gamepad_radio_idle) {
        return P4_BLE_RADIO_HANDOFF_WAIT;
    }
    handoff->waiting_for_gamepad = false;
    return P4_BLE_RADIO_HANDOFF_READY;
}

bool p4_ble_radio_handoff_release(
    p4_ble_radio_handoff_t *handoff,
    bool gamepad_mode_enabled,
    bool gamepad_connected,
    bool gamepad_bonded)
{
    if (handoff == NULL) {
        return false;
    }
    const bool resume = handoff->reconnect_suspended &&
        gamepad_mode_enabled && !gamepad_connected && gamepad_bonded;
    p4_ble_radio_handoff_init(handoff);
    return resume;
}
