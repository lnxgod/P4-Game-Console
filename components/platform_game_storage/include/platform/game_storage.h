// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_GAME_STORAGE_H
#define P4_PLATFORM_GAME_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform/game_storage_types.h"
#include "platform/doom_arena_content.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_GAME_STORAGE_PARTITION_LABEL "game_data"
#define PLATFORM_GAME_STORAGE_MOUNT_POINT "/game-data"
#define PLATFORM_GAME_STORAGE_GAMES_DIRECTORY_NAME "GAMES"
#define PLATFORM_GAME_STORAGE_GAMES_MOUNT_POINT \
    PLATFORM_GAME_STORAGE_MOUNT_POINT "/" \
    PLATFORM_GAME_STORAGE_GAMES_DIRECTORY_NAME
#define PLATFORM_GAME_STORAGE_UPDATE_DIRECTORY_NAME "UPDATE"
#define PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT \
    PLATFORM_GAME_STORAGE_MOUNT_POINT "/" \
    PLATFORM_GAME_STORAGE_UPDATE_DIRECTORY_NAME
#define PLATFORM_GAME_STORAGE_DOOM_WAD_PATH "/game-data/DOOM1.WAD"
#define PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES UINT64_C(4196020)
#define PLATFORM_GAME_STORAGE_CHEX_WAD_PATH "/game-data/CHEX.WAD"
#define PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES UINT64_C(12361532)
#define PLATFORM_GAME_STORAGE_CHEX_DEH_PATH "/game-data/CHEX.DEH"
#define PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES UINT64_C(20367)
#define PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_PATH "/game-data/FREEDOOM2.WAD"
#define PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_BYTES UINT64_C(28787748)
/* Separate Arena derivative; preserve original Freedoom campaign support. */
#define PLATFORM_GAME_STORAGE_ARENA_BASE_WAD_BYTES P4_GCA_BASE_WAD_BYTES
#define PLATFORM_GAME_STORAGE_ARENA_BASE_WAD_PATH P4_GCA_BASE_SD_PATH
#define PLATFORM_GAME_STORAGE_DWANGO5_WAD_BYTES UINT64_C(2109396)
#define PLATFORM_GAME_STORAGE_PUREHADES_WAD_BYTES UINT64_C(2313392)

typedef enum {
    PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM = 0,
    PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST,
    PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI,
    PLATFORM_GAME_STORAGE_DOOM_TITLE_COUNT,
} platform_game_storage_doom_title_t;

/**
 * Immutable, exact-hash-verified Doom-engine data retained for a terminal
 * game lease. The storage service owns these buffers; callers must not free
 * or modify them. They remain valid until restart.
 */
typedef struct {
    const uint8_t *wad_data;
    size_t wad_size_bytes;
    const uint8_t *deh_data;
    size_t deh_size_bytes;
} platform_game_storage_doom_snapshot_t;

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

typedef enum {
    PLATFORM_GAME_STORAGE_REPAIR_NOT_RUN = 0,
    PLATFORM_GAME_STORAGE_REPAIR_CLEAN,
    PLATFORM_GAME_STORAGE_REPAIR_REPAIRED,
    PLATFORM_GAME_STORAGE_REPAIR_NEEDS_HOST,
    PLATFORM_GAME_STORAGE_REPAIR_UNSUPPORTED,
    PLATFORM_GAME_STORAGE_REPAIR_FAILED,
} platform_game_storage_repair_outcome_t;

typedef struct {
    platform_game_storage_state_t state;
    bool usb_attached;
    bool usb_driver_running;
    bool usb_mode_supported;
    bool usb_host_ejected;
    uint64_t capacity_bytes;
    uint32_t sector_size_bytes;
    uint32_t generation;
    uint32_t ownership_transfers;
    uint32_t mount_failures;
    uint32_t scans;
    uint32_t usb_verified_writes;
    uint32_t usb_write_failures;
    uint64_t free_bytes;
    uint64_t filesystem_bytes; /* Usable FAT data capacity, excluding metadata. */
    bool space_valid;
    uint32_t real_frequency_khz;
    uint32_t root_entries;
    uint32_t checks;
    uint32_t recovery_attempts;
    uint32_t repair_attempts;
    uint32_t repair_sectors_rewritten;
    bool card_ready;
    bool filesystem_ready;
    bool doom_wad_ready;
    bool chex_quest_ready;
    /** Exact Doom/Chex identity validation is running off the boot path. */
    bool content_validation_running;
    /** The current mounted-storage generation has finished validation. */
    bool content_validation_complete;
    uint8_t content_validation_progress_percent;
    platform_game_storage_repair_outcome_t last_repair_outcome;
    esp_err_t last_check_error;
    esp_err_t last_recovery_error;
    esp_err_t last_repair_error;
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

/** Invalidate content caches after a verified app-owned USB content write. */
esp_err_t platform_game_storage_content_changed(void);

/**
 * Start exact Doom/Chex validation without blocking the Console OS task.
 *
 * On the Waveshare 4.3-inch board this hashes the pinned files in a bounded
 * low-priority worker only when an engine-game feature requests it (for
 * example Multiplayer), never merely because the device booted. Repeated
 * calls are idempotent within the current uninterrupted mounted generation.
 * Other board backends retain their existing synchronous refresh behavior.
 */
esp_err_t platform_game_storage_start_content_validation(void);

/**
 * Run a non-destructive SD/FAT health check while the launcher owns storage.
 *
 * The check issues a card-status command, reads FAT capacity/free-space
 * metadata, walks the bounded root directory, and revalidates game content.
 * It never formats, repairs, creates, removes, or rewrites a file.
 */
esp_err_t platform_game_storage_check_card(void);

/**
 * Retry an offline SD card by releasing the failed slot and remounting it.
 *
 * This is a non-destructive electrical/bus recovery operation. It is accepted
 * only from the fail-closed state and never formats or mutates the FAT volume.
 */
esp_err_t platform_game_storage_retry_card(void);

/**
 * Run the explicitly confirmed, on-device conservative FAT32 repair pass.
 *
 * Storage is unmounted before raw media writes. The implementation restores
 * only an unambiguous boot-sector copy, a structurally invalid FAT mirror
 * from its valid peer, and FAT32 FSInfo hints, with sector readback after
 * every write. It never formats and refuses ambiguous allocation histories,
 * directory cross-links, or lost-chain decisions.
 */
esp_err_t platform_game_storage_repair_fat(void);

/** Copy a coherent status snapshot. */
esp_err_t platform_game_storage_get_status(
    platform_game_storage_status_t *out_status);

/**
 * Give the Waveshare microSD card to H2 USB device MSC, or return it to the
 * launcher after the laptop has cleanly ejected it (or disconnected). In a
 * controller-first build, the caller must stop USB Host/HID before enabling
 * MSC and may restart Host/HID only after disabling MSC succeeds.
 *
 * The service rejects USB-off while an attached host has not ejected the
 * volume. App, USB-device, and controller-host ownership therefore remain
 * mutually exclusive.
 */
esp_err_t platform_game_storage_set_usb_mode(bool enabled);

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
 * List one directory beneath the game-storage root.
 *
 * An empty relative path selects the root. Every non-empty segment must be an
 * exact safe directory name; absolute paths, dot segments, repeated slashes,
 * control characters, and overlong paths are rejected.
 */
esp_err_t platform_game_storage_list_directory(
    const char *relative_path,
    platform_game_storage_file_listing_t *out_listing);

/** List the bounded, sorted contents of the fixed GAMES directory. */
esp_err_t platform_game_storage_list_games(
    platform_game_storage_file_listing_t *out_listing);

/**
 * Remove one regular file from the FAT root while the app owns it.
 *
 * The name must be one exact entry returned by the listing API. Paths,
 * traversal tokens, control characters, directories, and overlong names are
 * rejected. Successful removal invalidates the cached Doom identity.
 */
esp_err_t platform_game_storage_remove_root_file(const char *name);

/** Remove one regular file from a validated relative directory. */
esp_err_t platform_game_storage_remove_file(
    const char *relative_path, const char *name);

/** Remove one regular file from the fixed GAMES directory. */
esp_err_t platform_game_storage_remove_game_file(const char *name);

/** Service a caller-owned activity between bounded content-validation reads. */
typedef void (*platform_game_storage_progress_fn_t)(void *context);

/** Full content scans detect all changed bytes before launch. Trusted-on-read
 * is available only for Tab5 Arena: firmware-owned metadata authenticates each
 * consumed block; unconsumed card blocks are not claimed to be verified. */
typedef enum {
    PLATFORM_GAME_STORAGE_VERIFY_FULL_CONTENT = 0,
    PLATFORM_GAME_STORAGE_VERIFY_TRUSTED_ON_READ = 1,
} platform_game_storage_verification_t;

/** A copied snapshot of the current Arena launch preparation pass. */
typedef struct {
    platform_game_storage_verification_t verification;
    uint64_t bytes_checked;
    uint64_t bytes_total;
    uint16_t file_index;
    uint16_t file_count;
    bool active;
    bool checking_structure;
    bool checking_trusted_metadata;
    /** True after the selected policy and required WAD structure passed.
     * TRUSTED_ON_READ does not imply that unconsumed WAD blocks were read. */
    bool complete;
} platform_game_storage_doom_load_progress_t;

/**
 * Copy Arena launch progress without reading files or changing ownership.
 * bytes_checked counts bytes fed successfully into whole-file hash checks.
 * In TRUSTED_ON_READ mode, bytes_total/checks include only the 13 support files;
 * WAD metadata and structure are separate named stages. complete describes
 * preparation under verification, not full-card health in lazy mode. The calling-task
 * progress callback may use this getter while the recursive storage lock is
 * held. Other tasks may block until validation ends, so this is not a worker
 * polling API. No other storage API is permitted from that callback.
 */
esp_err_t platform_game_storage_get_doom_load_progress(
    platform_game_storage_doom_load_progress_t *out_progress);

/**
 * Read one bounded regular root file into PSRAM (or internal RAM fallback).
 * The returned allocation must be released with the matching function.
 */
esp_err_t platform_game_storage_load_root_file(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes);

/** Read one bounded regular file from the fixed GAMES directory. */
esp_err_t platform_game_storage_load_game_file(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes);

/**
 * Like the corresponding load function, with optional calling-task progress
 * before the first read and between reads of at most 64 KiB. The storage lock
 * stays held: the callback must not call storage APIs or retain file buffers.
 * It must return promptly and must not change storage ownership.
 */
esp_err_t platform_game_storage_load_root_file_with_progress(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes,
    platform_game_storage_progress_fn_t progress, void *context);

esp_err_t platform_game_storage_load_game_file_with_progress(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes,
    platform_game_storage_progress_fn_t progress, void *context);

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
 * exclusive game lease until restart. This compatibility wrapper selects
 * Doom shareware; new callers should use the title-specific function below.
 */
esp_err_t platform_game_storage_lock_for_game(void);

/**
 * Fully hash and lock one supported Doom-engine title for exclusive launch.
 * Chex Quest requires both the exact CHEX.WAD and matching CHEX.DEH.
 */
esp_err_t platform_game_storage_lock_for_doom_title(
    platform_game_storage_doom_title_t title);

/**
 * Like lock_for_doom_title(), with optional progress on the calling task after
 * each hashed block. The storage lock is held: the callback may copy status
 * with get_doom_load_progress(), but must not call any other storage API or
 * retain content buffers. This lets an admitted multiplayer
 * session poll and exchange keepalives during a long SD validation pass.
 */
esp_err_t platform_game_storage_lock_for_doom_title_with_progress(
    platform_game_storage_doom_title_t title,
    platform_game_storage_progress_fn_t progress, void *context);

/** Explicit launch policy. Existing lock APIs always select FULL_CONTENT.
 * TRUSTED_ON_READ is rejected for non-Arena titles and non-Tab5 targets.
 * The terminal storage lease, read-time block checks and failure latch are
 * identical in both modes. Call FULL_CONTENT for a complete Arena card scan.
 * Progress callback restrictions are identical to the API above. */
esp_err_t platform_game_storage_lock_for_doom_title_with_policy(
    platform_game_storage_doom_title_t title,
    platform_game_storage_verification_t verification,
    platform_game_storage_progress_fn_t progress, void *context);

/**
 * Borrow the verified in-memory data for the current terminal game lease.
 *
 * The snapshot is created during the same bounded SD pass that computes the
 * exact pinned SHA-256. This call performs no storage I/O. It succeeds only
 * for the title passed to a successful lock_for_doom_title() call.
 */
esp_err_t platform_game_storage_get_locked_doom_snapshot(
    platform_game_storage_doom_title_t title,
    platform_game_storage_doom_snapshot_t *out_snapshot);

/** True only after platform_game_storage_lock_for_game succeeds. */
bool platform_game_storage_game_locked(void);
/** Arena data under the terminal lease. The optional Tab5 BASE copy is fully
 * verified before publication; both PWADs retain their block-verified streams. */
esp_err_t platform_game_storage_read_arena_wad(unsigned file, size_t offset, void *out, size_t bytes);
/** Engine-owner call immediately before the actual zone allocation. Recheck
 * the exact upcoming request plus the fixed reserve, releasing only an optional
 * Arena BASE copy if necessary. No filesystem I/O, retries or allocation occur.
 * Lock admission is zero-wait; contention leaves all ownership unchanged.
 * Other valid Doom leases are unchanged. Missing/invalid leases are rejected.
 * The optional output is zeroed on failure and reports retained BASE bytes. */
esp_err_t platform_game_storage_prepare_doom_zone(
    size_t zone_bytes, size_t *out_arena_resident_bytes);
/* Optional one-descriptor startup hint. Failed begin means use normal reads;
 * failure after begin is an I/O failure, never an integrity-bypassing fallback.
 * Owner closes the scope before gameplay; lease teardown also frees it. */
bool platform_game_storage_arena_sprite_begin(unsigned file);
esp_err_t platform_game_storage_arena_sprite_read(unsigned file, size_t offset,
                                                  void *out, size_t bytes);
void platform_game_storage_arena_sprite_end(void);
/** Cheap presence/size preflight; launch still must pass its selected policy. */
bool platform_game_storage_arena_present(void);

const char *platform_game_storage_state_name(
    platform_game_storage_state_t state);

#ifdef __cplusplus
}
#endif

#endif
