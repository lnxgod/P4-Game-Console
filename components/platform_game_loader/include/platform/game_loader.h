// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_GAME_LOADER_H
#define P4_PLATFORM_GAME_LOADER_H

#include "esp_err.h"
#include "p4/cartridge.h"
#include "platform/game_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t platform_game_loader_run(
    const platform_game_catalog_entry_t *entry,
    p4_cartridge_host_v1_t *host);

#ifdef __cplusplus
}
#endif

#endif
