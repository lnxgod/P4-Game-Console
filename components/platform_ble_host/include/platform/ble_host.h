// SPDX-License-Identifier: MIT

#ifndef PLATFORM_BLE_HOST_H
#define PLATFORM_BLE_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ble_gatt_svc_def;

typedef enum {
    PLATFORM_BLE_HOST_OFF = 0,
    PLATFORM_BLE_HOST_STARTING_RADIO,
    PLATFORM_BLE_HOST_STARTING_NIMBLE,
    PLATFORM_BLE_HOST_READY,
    PLATFORM_BLE_HOST_ERROR,
} platform_ble_host_state_t;

typedef void (*platform_ble_host_sync_handler_t)(void *context);
typedef void (*platform_ble_host_reset_handler_t)(int reason, void *context);

typedef struct {
    /** Optional, static, zero-terminated local GATT service table. */
    const struct ble_gatt_svc_def *services;
    platform_ble_host_sync_handler_t on_sync;
    platform_ble_host_reset_handler_t on_reset;
    void *context;
} platform_ble_host_client_t;

typedef struct {
    platform_ble_host_state_t state;
    bool initialized;
    bool synced;
    uint8_t own_addr_type;
    uint8_t client_count;
    int last_error;
} platform_ble_host_status_t;

/** Register static services and lifecycle callbacks without starting radio. */
esp_err_t platform_ble_host_register_client(
    const platform_ble_host_client_t *client);

/** Lazily start ESP-Hosted and the single process-wide NimBLE host. */
esp_err_t platform_ble_host_start(void);

platform_ble_host_status_t platform_ble_host_status(void);
bool platform_ble_host_ready(void);
esp_err_t platform_ble_host_own_addr_type(uint8_t *own_addr_type);

#ifdef __cplusplus
}
#endif

#endif
