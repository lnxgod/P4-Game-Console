// SPDX-License-Identifier: MIT

#include "game_storage_model.h"

#include <limits.h>
#include <string.h>

static void increment(uint32_t *value)
{
    if (*value != UINT32_MAX) {
        ++*value;
    }
}

void game_storage_model_init(game_storage_model_t *model)
{
    if (model != NULL) {
        memset(model, 0, sizeof(*model));
    }
}

void game_storage_model_mount_start(game_storage_model_t *model,
                                    game_storage_owner_t current_owner)
{
    if (model == NULL ||
        (current_owner != GAME_STORAGE_OWNER_APP &&
         current_owner != GAME_STORAGE_OWNER_USB)) {
        return;
    }
    model->owner = GAME_STORAGE_OWNER_TRANSITION;
    model->transition_target = current_owner == GAME_STORAGE_OWNER_APP
        ? GAME_STORAGE_OWNER_USB : GAME_STORAGE_OWNER_APP;
    if (model->transition_target == GAME_STORAGE_OWNER_USB) {
        model->content = GAME_STORAGE_CONTENT_UNKNOWN;
    }
}

void game_storage_model_mount_complete(game_storage_model_t *model,
                                       game_storage_owner_t new_owner)
{
    if (model == NULL ||
        (new_owner != GAME_STORAGE_OWNER_APP &&
         new_owner != GAME_STORAGE_OWNER_USB)) {
        return;
    }
    model->owner = new_owner;
    model->transition_target = GAME_STORAGE_OWNER_NONE;
    model->format_required = false;
    increment(&model->ownership_transfers);
    if (new_owner == GAME_STORAGE_OWNER_APP) {
        model->content = GAME_STORAGE_CONTENT_UNKNOWN;
        increment(&model->generation);
    } else {
        model->content = GAME_STORAGE_CONTENT_UNKNOWN;
    }
}

void game_storage_model_mount_failed(game_storage_model_t *model,
                                     bool format_required)
{
    if (model == NULL) {
        return;
    }
    increment(&model->mount_failures);
    model->format_required = format_required;
    model->owner = format_required
        ? GAME_STORAGE_OWNER_USB : GAME_STORAGE_OWNER_FAULT;
    model->transition_target = GAME_STORAGE_OWNER_NONE;
    model->content = GAME_STORAGE_CONTENT_UNKNOWN;
}

bool game_storage_model_begin_scan(game_storage_model_t *model)
{
    if (model == NULL || model->owner != GAME_STORAGE_OWNER_APP ||
        model->content != GAME_STORAGE_CONTENT_UNKNOWN) {
        return false;
    }
    model->content = GAME_STORAGE_CONTENT_SCANNING;
    return true;
}

void game_storage_model_finish_scan(game_storage_model_t *model,
                                    game_storage_content_t content)
{
    if (model == NULL || model->owner != GAME_STORAGE_OWNER_APP ||
        model->content != GAME_STORAGE_CONTENT_SCANNING) {
        return;
    }
    if (content == GAME_STORAGE_CONTENT_READY ||
        content == GAME_STORAGE_CONTENT_MISSING ||
        content == GAME_STORAGE_CONTENT_INVALID) {
        model->content = content;
    } else {
        model->content = GAME_STORAGE_CONTENT_INVALID;
    }
}

bool game_storage_model_files_available(const game_storage_model_t *model)
{
    return model != NULL && model->owner == GAME_STORAGE_OWNER_APP &&
        !model->format_required && !model->launch_pending;
}

bool game_storage_model_begin_game_lock(game_storage_model_t *model)
{
    if (model == NULL || model->owner != GAME_STORAGE_OWNER_APP ||
        model->content != GAME_STORAGE_CONTENT_READY ||
        model->launch_pending) {
        return false;
    }
    model->launch_pending = true;
    return true;
}

void game_storage_model_finish_game_lock(game_storage_model_t *model,
                                         bool success)
{
    if (model == NULL || !model->launch_pending) {
        return;
    }
    model->launch_pending = false;
    if (success) {
        model->owner = GAME_STORAGE_OWNER_GAME;
        model->transition_target = GAME_STORAGE_OWNER_NONE;
    }
}

void game_storage_model_fault(game_storage_model_t *model)
{
    if (model == NULL) {
        return;
    }
    model->owner = GAME_STORAGE_OWNER_FAULT;
    model->transition_target = GAME_STORAGE_OWNER_NONE;
    model->content = GAME_STORAGE_CONTENT_UNKNOWN;
    model->launch_pending = false;
}
