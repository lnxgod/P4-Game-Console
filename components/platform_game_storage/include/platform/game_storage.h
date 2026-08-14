// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_GAME_STORAGE_H
#define P4_PLATFORM_GAME_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_GAME_STORAGE_PARTITION_LABEL "game_data"
#define PLATFORM_GAME_STORAGE_MOUNT_POINT "/game-data"
#define PLATFORM_GAME_STORAGE_DOOM_WAD_PATH "/game-data/DOOM1.WAD"
#define PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES UINT64_C(4196020)

typedef enum {
    PLATFORM_GAME_STORAGE_UNINITIALIZED = 0,
    PLATFORM_GAME_STORAGE_APP_SCANNING,
    PLATFORM_GAME_STORAGE_APP_READY,
    PLATFORM_GAME_STORAGE_APP_MISSING,
    PLATFORM_GAME_STORAGE_APP_INVALID,
    PLATFORM_GAME_STORAGE_USB_HOST,
    PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED,
    PLATFORM_GAME_STORAGE_TRANSITION,
    PLATFORM_GAME_STORAGE_GAME_LOCKED,
    PLATFORM_GAME_STORAGE_FAULT,
} platform_game_storage_state_t;

typedef struct {
    platform_game_storage_state_t state;
    bool usb_attached;
    bool usb_driver_running;
    uint64_t capacity_bytes;
    uint32_t sector_size_bytes;
    uint32_t generation;
    uint32_t ownership_transfers;
    uint32_t mount_failures;
    uint32_t scans;
    esp_err_t last_error;
} platform_game_storage_status_t;

/**
 * Initialize the wear-levelled FAT volume and the ESP32-P4 USB MSC device.
 *
 * The filesystem is never formatted at runtime. A missing/corrupt filesystem
 * is exposed fail-closed and must be restored from a reviewed image or by the
 * host. App and USB ownership are mutually exclusive.
 */
esp_err_t platform_game_storage_init(void);

/** Refresh the cached game-file inventory after an ownership generation. */
esp_err_t platform_game_storage_refresh(void);

/** Copy a coherent status snapshot. */
esp_err_t platform_game_storage_get_status(
    platform_game_storage_status_t *out_status);

/**
 * Revoke USB access, remount for the app, re-hash DOOM1.WAD, and retain an
 * exclusive game lease until restart. No host can remount beneath the game.
 */
esp_err_t platform_game_storage_lock_for_game(void);

/** True only after platform_game_storage_lock_for_game succeeds. */
bool platform_game_storage_game_locked(void);

const char *platform_game_storage_state_name(
    platform_game_storage_state_t state);

#ifdef __cplusplus
}
#endif

#endif
