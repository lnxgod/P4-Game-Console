// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_GAME_STORAGE_INTERNAL_H
#define P4_PLATFORM_GAME_STORAGE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Commit and read back one complete USB MSC WRITE(10) transfer. */
esp_err_t platform_game_storage_msc_write10(
    uint8_t lun, uint32_t lba, uint32_t offset,
    const uint8_t *data, size_t size_bytes);

#endif
