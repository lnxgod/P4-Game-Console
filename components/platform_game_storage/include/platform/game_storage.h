// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_GAME_STORAGE_H
#define P4_PLATFORM_GAME_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform/game_storage_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_GAME_STORAGE_PARTITION_LABEL "game_data"
#define PLATFORM_GAME_STORAGE_MOUNT_POINT "/game-data"
#define PLATFORM_GAME_STORAGE_UPDATE_DIRECTORY_NAME "UPDATE"
#define PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT \
    PLATFORM_GAME_STORAGE_MOUNT_POINT "/" \
    PLATFORM_GAME_STORAGE_UPDATE_DIRECTORY_NAME
#define PLATFORM_GAME_STORAGE_DOOM_WAD_PATH "/game-data/DOOM1.WAD"
#define PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES UINT64_C(4196020)

typedef esp_err_t (*platform_game_storage_stream_fn)(
    void *context, const uint8_t *data, size_t size_bytes,
    uint64_t file_offset);

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
    uint32_t usb_verified_writes;
    uint32_t usb_write_failures;
    esp_err_t last_error;
} platform_game_storage_status_t;

/**
 * Initialize the selected board's persistent game-data FAT volume.
 *
 * The filesystem is never formatted at runtime. A missing/corrupt filesystem
 * is exposed fail-closed and must be restored from a reviewed image or by the
 * host. On boards with USB device storage, app and USB ownership are mutually
 * exclusive. The Olimex profile mounts microSD for app-only access and never
 * advertises that card over its programming USB-C connector.
 */
esp_err_t platform_game_storage_init(void);

/** Refresh the cached game-file inventory after an ownership generation. */
esp_err_t platform_game_storage_refresh(void);

/** Copy a coherent status snapshot. */
esp_err_t platform_game_storage_get_status(
    platform_game_storage_status_t *out_status);

/**
 * List a bounded, sorted snapshot of the FAT root while the app owns it.
 *
 * Host ownership, mount transitions, and the terminal game lease return
 * ESP_ERR_INVALID_STATE. Hidden host metadata is counted but not returned.
 * Names are copied into fixed buffers; entries that cannot be represented
 * safely are counted as omitted and never truncated.
 */
esp_err_t platform_game_storage_list_root(
    platform_game_storage_file_listing_t *out_listing);

/**
 * Remove one regular file from the FAT root while the app owns it.
 *
 * The name must be one exact entry returned by the listing API. Paths,
 * traversal tokens, control characters, directories, and overlong names are
 * rejected. Successful removal invalidates the cached Doom identity.
 */
esp_err_t platform_game_storage_remove_root_file(const char *name);

/**
 * Read one bounded regular root file into PSRAM (or internal RAM fallback).
 * The returned allocation must be released with the matching function.
 */
esp_err_t platform_game_storage_load_root_file(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes);

/** Read one bounded regular file from the fixed UPDATE directory. */
esp_err_t platform_game_storage_load_update_file(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes);

void platform_game_storage_release_file(uint8_t *data);

/**
 * Hold an exclusive storage maintenance lease and stream one root file.
 * The callback must not call another game-storage API.
 */
esp_err_t platform_game_storage_stream_root_file_exclusive(
    const char *name, size_t maximum_bytes,
    platform_game_storage_stream_fn consume, void *context,
    size_t *out_size_bytes);

/** Hold an exclusive maintenance lease and stream one file from UPDATE. */
esp_err_t platform_game_storage_stream_update_file_exclusive(
    const char *name, size_t maximum_bytes,
    platform_game_storage_stream_fn consume, void *context,
    size_t *out_size_bytes);

/** Remove one regular file from the fixed UPDATE directory. */
esp_err_t platform_game_storage_remove_update_file(const char *name);

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
