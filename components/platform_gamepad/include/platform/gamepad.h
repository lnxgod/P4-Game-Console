// SPDX-License-Identifier: MIT

#ifndef PLATFORM_GAMEPAD_H
#define PLATFORM_GAMEPAD_H

#include "esp_err.h"
#include "gamepad/mapping.h"
#include "gamepad/snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A transport copies one complete, internally synchronized snapshot. */
typedef esp_err_t (*platform_gamepad_snapshot_provider_t)(
    platform_gamepad_snapshot_t *snapshot);

/**
 * Register one OS-owned transport. Registration is idempotent per transport;
 * USB HID, then wired XUSB, then BLE is the deterministic priority order.
 */
esp_err_t platform_gamepad_register_provider(
    platform_gamepad_transport_t transport,
    platform_gamepad_snapshot_provider_t provider);

void platform_gamepad_unregister_provider(
    platform_gamepad_transport_t transport,
    platform_gamepad_snapshot_provider_t provider);

/**
 * Copy the active canonical controller snapshot without exposing transport.
 * An initialized provider with no controller ever connected returns ESP_OK and
 * its neutral, disconnected, identity-free snapshot. Lower-priority connected
 * providers still take precedence over any disconnected publication.
 */
esp_err_t platform_gamepad_get_snapshot(
    platform_gamepad_snapshot_t *snapshot);

/** Copy the selected transport snapshot before user button remapping. */
esp_err_t platform_gamepad_get_raw_snapshot(
    platform_gamepad_snapshot_t *snapshot);

/** Replace or inspect the console-wide button mapping atomically. */
esp_err_t platform_gamepad_set_mapping(
    const gamepad_button_mapping_t *mapping);
esp_err_t platform_gamepad_get_mapping(gamepad_button_mapping_t *mapping);

#ifdef __cplusplus
}
#endif

#endif
