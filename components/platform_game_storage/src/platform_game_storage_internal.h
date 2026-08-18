// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_GAME_STORAGE_INTERNAL_H
#define P4_PLATFORM_GAME_STORAGE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Commit and read back one complete USB MSC WRITE(10) transfer. */
esp_err_t platform_game_storage_msc_write10(
    uint8_t lun, uint32_t lba, uint32_t offset,
    const uint8_t *data, size_t size_bytes);

/** Record a host load/eject command without changing storage ownership. */
void platform_game_storage_msc_start_stop(
    uint8_t lun, bool start, bool load_eject);

#endif
