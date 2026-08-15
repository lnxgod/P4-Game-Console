// SPDX-License-Identifier: MIT

#include "game_storage_model.h"

#include <assert.h>
#include <stdio.h>

static void test_host_handoff_invalidates_cache(void)
{
    game_storage_model_t model;
    game_storage_model_init(&model);
    game_storage_model_mount_complete(&model, GAME_STORAGE_OWNER_APP);
    assert(model.owner == GAME_STORAGE_OWNER_APP);
    assert(game_storage_model_files_available(&model));
    assert(model.generation == 1U);
    assert(game_storage_model_begin_scan(&model));
    game_storage_model_finish_scan(&model, GAME_STORAGE_CONTENT_READY);
    assert(game_storage_model_begin_game_lock(&model));
    game_storage_model_finish_game_lock(&model, false);

    game_storage_model_mount_start(&model, GAME_STORAGE_OWNER_APP);
    assert(model.owner == GAME_STORAGE_OWNER_TRANSITION);
    assert(model.transition_target == GAME_STORAGE_OWNER_USB);
    assert(model.content == GAME_STORAGE_CONTENT_UNKNOWN);
    game_storage_model_mount_complete(&model, GAME_STORAGE_OWNER_USB);
    assert(model.owner == GAME_STORAGE_OWNER_USB);
    assert(!game_storage_model_files_available(&model));
    assert(!game_storage_model_begin_scan(&model));

    game_storage_model_mount_start(&model, GAME_STORAGE_OWNER_USB);
    game_storage_model_mount_complete(&model, GAME_STORAGE_OWNER_APP);
    assert(model.generation == 2U);
    assert(game_storage_model_begin_scan(&model));
    game_storage_model_finish_scan(&model, GAME_STORAGE_CONTENT_MISSING);
    assert(!game_storage_model_begin_game_lock(&model));
}

static void test_game_lock_is_terminal_and_exclusive(void)
{
    game_storage_model_t model;
    game_storage_model_init(&model);
    game_storage_model_mount_complete(&model, GAME_STORAGE_OWNER_APP);
    assert(game_storage_model_begin_scan(&model));
    game_storage_model_finish_scan(&model, GAME_STORAGE_CONTENT_READY);
    assert(game_storage_model_begin_game_lock(&model));
    assert(!game_storage_model_files_available(&model));
    /* The launch path invalidates and re-hashes while USB is revoked. */
    model.content = GAME_STORAGE_CONTENT_UNKNOWN;
    assert(game_storage_model_begin_scan(&model));
    game_storage_model_finish_scan(&model, GAME_STORAGE_CONTENT_READY);
    assert(!game_storage_model_begin_game_lock(&model));
    game_storage_model_finish_game_lock(&model, true);
    assert(model.owner == GAME_STORAGE_OWNER_GAME);
    assert(!game_storage_model_files_available(&model));
    assert(!model.launch_pending);
    assert(!game_storage_model_begin_scan(&model));
}

static void test_mount_and_format_fail_closed(void)
{
    game_storage_model_t model;
    game_storage_model_init(&model);
    game_storage_model_mount_failed(&model, true);
    assert(model.owner == GAME_STORAGE_OWNER_USB);
    assert(model.format_required);
    assert(model.mount_failures == 1U);

    game_storage_model_mount_failed(&model, false);
    assert(model.owner == GAME_STORAGE_OWNER_FAULT);
    assert(!model.format_required);
    assert(model.mount_failures == 2U);
    game_storage_model_fault(&model);
    assert(model.owner == GAME_STORAGE_OWNER_FAULT);
}

static void test_launch_mount_failure_cannot_be_promoted(void)
{
    game_storage_model_t model;
    game_storage_model_init(&model);
    game_storage_model_mount_complete(&model, GAME_STORAGE_OWNER_APP);
    assert(game_storage_model_begin_scan(&model));
    game_storage_model_finish_scan(&model, GAME_STORAGE_CONTENT_READY);
    assert(game_storage_model_begin_game_lock(&model));
    game_storage_model_mount_failed(&model, false);
    game_storage_model_finish_game_lock(&model, false);
    assert(model.owner == GAME_STORAGE_OWNER_FAULT);
    assert(!model.launch_pending);
}

int main(void)
{
    test_host_handoff_invalidates_cache();
    test_game_lock_is_terminal_and_exclusive();
    test_mount_and_format_fail_closed();
    test_launch_mount_failure_cannot_be_promoted();
    puts("platform game storage model tests passed");
    return 0;
}
