// SPDX-License-Identifier: MIT

#ifndef PLATFORM_GAMEPAD_H
#define PLATFORM_GAMEPAD_H

#include "esp_err.h"
#include "gamepad/snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A transport copies one complete, internally synchronized snapshot. */
typedef esp_err_t (*platform_gamepad_snapshot_provider_t)(
    platform_gamepad_snapshot_t *snapshot);

/**
 * Register one OS-owned transport. Registration is idempotent per transport;
 * USB HID has deterministic priority when wired and BLE pads coexist.
 */
esp_err_t platform_gamepad_register_provider(
    platform_gamepad_transport_t transport,
    platform_gamepad_snapshot_provider_t provider);

void platform_gamepad_unregister_provider(
    platform_gamepad_transport_t transport,
    platform_gamepad_snapshot_provider_t provider);

/** Copy the active canonical controller snapshot without exposing transport. */
esp_err_t platform_gamepad_get_snapshot(
    platform_gamepad_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
