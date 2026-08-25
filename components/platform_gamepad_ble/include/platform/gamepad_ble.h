// SPDX-License-Identifier: MIT

#ifndef PLATFORM_GAMEPAD_BLE_H
#define PLATFORM_GAMEPAD_BLE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gamepad/snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLATFORM_GAMEPAD_BLE_NAME_BYTES = 32,
};

typedef enum {
    PLATFORM_GAMEPAD_BLE_OFF = 0,
    PLATFORM_GAMEPAD_BLE_STANDBY,
    PLATFORM_GAMEPAD_BLE_STARTING_HOST,
    PLATFORM_GAMEPAD_BLE_SCANNING,
    PLATFORM_GAMEPAD_BLE_CONNECTING,
    PLATFORM_GAMEPAD_BLE_SECURING,
    PLATFORM_GAMEPAD_BLE_DISCOVERING,
    PLATFORM_GAMEPAD_BLE_SUBSCRIBING,
    PLATFORM_GAMEPAD_BLE_READY,
    PLATFORM_GAMEPAD_BLE_ERROR,
} platform_gamepad_ble_state_t;

typedef struct {
    platform_gamepad_ble_state_t state;
    bool supported;
    bool host_ready;
    bool bonded;
    bool connected;
    bool encrypted;
    bool ready;
    int8_t rssi;
    uint8_t input_reports;
    uint16_t att_mtu;
    uint32_t reports_received;
    uint32_t reports_dropped;
    int last_error;
    char name[PLATFORM_GAMEPAD_BLE_NAME_BYTES];
} platform_gamepad_ble_status_t;

/** Register the BLE HID client and controller broker without starting radio. */
esp_err_t platform_gamepad_ble_prepare(void);

/**
 * Start a bounded scan. If a saved pad exists, only that identity/name is
 * accepted; otherwise the first connectable HID gamepad enters pairing.
 */
esp_err_t platform_gamepad_ble_connect_or_pair(void);

/** Cancel an in-progress scan without disturbing BLE multiplayer. */
void platform_gamepad_ble_cancel(void);

/** Disconnect the active pad but retain its persistent bond. */
void platform_gamepad_ble_disconnect(void);

/** Remove the saved controller bond; this never clears unrelated BLE peers. */
esp_err_t platform_gamepad_ble_forget(void);

esp_err_t platform_gamepad_ble_get_snapshot(
    platform_gamepad_snapshot_t *snapshot);
platform_gamepad_ble_status_t platform_gamepad_ble_status(void);

#ifdef __cplusplus
}
#endif

#endif
