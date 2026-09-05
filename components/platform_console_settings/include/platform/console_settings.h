// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_CONSOLE_SETTINGS_H
#define P4_PLATFORM_CONSOLE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gamepad/mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLATFORM_CONSOLE_SETTINGS_VERSION = 3,
    PLATFORM_CONSOLE_NODE_NAME_BYTES = 17,
    PLATFORM_CONSOLE_VOLUME_MIN = 0,
    PLATFORM_CONSOLE_VOLUME_MAX = 10,
    PLATFORM_CONSOLE_BOOT_VOLUME_DEFAULT = 0,
    PLATFORM_CONSOLE_GAME_VOLUME_DEFAULT = 0,
};

typedef struct {
    uint16_t version;
    uint16_t size;
    uint8_t boot_volume_step;
    uint8_t game_volume_step;
    char node_name[PLATFORM_CONSOLE_NODE_NAME_BYTES];
    bool persistent;
    bool ble_controller_enabled;
    gamepad_button_mapping_t controller_mapping;
} platform_console_settings_t;

/** Load bounded settings. Safe defaults are returned if NVS is unavailable. */
esp_err_t platform_console_settings_init(
    platform_console_settings_t *settings);

/** Persist the master level used by the next boot sound sequence (0..10). */
esp_err_t platform_console_settings_set_boot_volume(
    platform_console_settings_t *settings, uint8_t volume_step);

/** Persist the master level passed to native games and Doom (0..10). */
esp_err_t platform_console_settings_set_game_volume(
    platform_console_settings_t *settings, uint8_t volume_step);

/** Persist a short uppercase development/lobby identity such as GC-P4-B289. */
esp_err_t platform_console_settings_set_node_name(
    platform_console_settings_t *settings, const char *node_name);

/** Persist whether the BLE gamepad client may reconnect or pair. */
esp_err_t platform_console_settings_set_ble_controller_enabled(
    platform_console_settings_t *settings, bool enabled);

/** Persist the console-wide A/B/X/Y/Start/Back physical button mapping. */
esp_err_t platform_console_settings_set_controller_mapping(
    platform_console_settings_t *settings,
    const gamepad_button_mapping_t *mapping);

/**
 * Arm the one-boot USB enumeration recovery probe.
 *
 * A committed marker is written before the experimental retry path can run.
 * If the previous boot left that marker armed, recovery is suppressed for the
 * current boot.  When persistence is unavailable this fails closed and leaves
 * `out_allowed` false.
 */
esp_err_t platform_console_settings_begin_usb_enum_probe(
    const platform_console_settings_t *settings,
    bool *out_allowed,
    bool *out_prior_boot_incomplete);

/** Clear the armed USB probe only after Console OS has remained stable. */
esp_err_t platform_console_settings_confirm_usb_enum_probe(
    const platform_console_settings_t *settings);

bool platform_console_settings_volume_valid(uint8_t volume_step);
bool platform_console_settings_node_name_valid(const char *node_name);

#ifdef __cplusplus
}
#endif

#endif
