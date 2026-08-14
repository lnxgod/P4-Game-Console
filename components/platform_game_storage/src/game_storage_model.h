// SPDX-License-Identifier: MIT

#ifndef P4_GAME_STORAGE_MODEL_H
#define P4_GAME_STORAGE_MODEL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GAME_STORAGE_OWNER_NONE = 0,
    GAME_STORAGE_OWNER_APP,
    GAME_STORAGE_OWNER_USB,
    GAME_STORAGE_OWNER_TRANSITION,
    GAME_STORAGE_OWNER_GAME,
    GAME_STORAGE_OWNER_FAULT,
} game_storage_owner_t;

typedef enum {
    GAME_STORAGE_CONTENT_UNKNOWN = 0,
    GAME_STORAGE_CONTENT_SCANNING,
    GAME_STORAGE_CONTENT_READY,
    GAME_STORAGE_CONTENT_MISSING,
    GAME_STORAGE_CONTENT_INVALID,
} game_storage_content_t;

typedef struct {
    game_storage_owner_t owner;
    game_storage_owner_t transition_target;
    game_storage_content_t content;
    bool format_required;
    bool launch_pending;
    uint32_t generation;
    uint32_t ownership_transfers;
    uint32_t mount_failures;
} game_storage_model_t;

void game_storage_model_init(game_storage_model_t *model);
void game_storage_model_mount_start(game_storage_model_t *model,
                                    game_storage_owner_t current_owner);
void game_storage_model_mount_complete(game_storage_model_t *model,
                                       game_storage_owner_t new_owner);
void game_storage_model_mount_failed(game_storage_model_t *model,
                                     bool format_required);
bool game_storage_model_begin_scan(game_storage_model_t *model);
void game_storage_model_finish_scan(game_storage_model_t *model,
                                    game_storage_content_t content);
bool game_storage_model_begin_game_lock(game_storage_model_t *model);
void game_storage_model_finish_game_lock(game_storage_model_t *model,
                                         bool success);
void game_storage_model_fault(game_storage_model_t *model);

#endif
