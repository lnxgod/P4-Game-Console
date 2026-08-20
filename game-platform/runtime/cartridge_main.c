// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "p4/cartridge.h"
#include "p4/game.h"

#ifndef P4_GAME_ENTRY_SYMBOL
#error "P4_GAME_ENTRY_SYMBOL must name one p4_game_descriptor_t"
#endif

extern const p4_game_descriptor_t P4_GAME_ENTRY_SYMBOL;

static bool host_play_tone(void *opaque, const p4_tone_t *tone)
{
    p4_cartridge_host_v1_t *const host = opaque;
    return host != NULL && host->play_tone != NULL &&
        host->play_tone(host->context, tone);
}

static bool host_submit_pcm16_stereo(
    void *opaque, const int16_t *interleaved_stereo, size_t frame_count)
{
    p4_cartridge_host_v1_t *const host = opaque;
    return host != NULL && host->submit_pcm16_stereo != NULL &&
        host->submit_pcm16_stereo(
            host->context, interleaved_stereo, frame_count);
}

static void host_stop_audio(void *opaque)
{
    p4_cartridge_host_v1_t *const host = opaque;
    if (host != NULL && host->stop_audio != NULL) {
        host->stop_audio(host->context);
    }
}

static bool host_unlock_achievement(
    void *opaque, const p4_game_achievement_t *achievement)
{
    p4_cartridge_host_v1_t *const host = opaque;
    return host != NULL && host->unlock_achievement != NULL &&
        host->unlock_achievement(host->context, achievement);
}

static bool host_request_signal_scan(void *opaque, uint64_t focus_token)
{
    p4_cartridge_host_v1_t *const host = opaque;
    return host != NULL && host->request_signal_scan != NULL &&
        host->request_signal_scan(host->context, focus_token);
}

static bool host_read_signal_scan(
    void *opaque, p4_game_signal_snapshot_t *snapshot)
{
    p4_cartridge_host_v1_t *const host = opaque;
    return host != NULL && host->read_signal_scan != NULL &&
        host->read_signal_scan(host->context, snapshot);
}

static bool host_queue_save(
    void *opaque,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *data,
    size_t data_bytes,
    p4_game_save_ticket_t *ticket_out)
{
    p4_cartridge_host_v1_t *const host = opaque;
    return host != NULL && host->queue_save != NULL &&
        host->queue_save(host->context, slot_id, schema_version,
                         expected_sequence, data, data_bytes, ticket_out);
}

static bool host_read_save_status(
    void *opaque,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out)
{
    p4_cartridge_host_v1_t *const host = opaque;
    return host != NULL && host->read_save_status != NULL &&
        host->read_save_status(host->context, ticket, status_out,
                               committed_sequence_out);
}

static bool host_field_present(const p4_cartridge_host_v1_t *host,
                               size_t offset, size_t bytes)
{
    return host != NULL && offset <= SIZE_MAX - bytes &&
        (size_t)host->struct_bytes >= offset + bytes;
}

static bool host_valid(const p4_cartridge_host_v1_t *host)
{
    return host != NULL && host->magic == P4_CARTRIDGE_HOST_MAGIC &&
        host->api_version == P4_CARTRIDGE_HOST_API_VERSION &&
        host_field_present(
            host, offsetof(p4_cartridge_host_v1_t, finished),
            sizeof(host->finished)) &&
        host->expected_game_id != NULL && host->surface.pixels != NULL &&
        host->surface.width == P4_GAME_SURFACE_WIDTH &&
        host->surface.height == P4_GAME_SURFACE_HEIGHT &&
        host->surface.stride_pixels >= P4_GAME_SURFACE_WIDTH &&
        host->poll_frame != NULL && host->present != NULL;
}

static uint32_t supported_service_capabilities(void)
{
    return P4_GAME_CAP_VIDEO |
        P4_GAME_CAP_CONTROLS |
        P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_AUDIO_STREAM |
        P4_GAME_CAP_STORAGE |
        P4_GAME_CAP_SIGNAL_SCAN |
        P4_GAME_CAP_SAVE;
}

static bool host_save_snapshot_valid(const p4_cartridge_host_v1_t *host)
{
    if (host->save_bytes == 0U) {
        return host->save_data == NULL && host->save_schema_version == 0U &&
            host->save_sequence == 0U;
    }
    return host->save_data != NULL &&
        host->save_bytes <= P4_GAME_SAVE_MAX_BYTES &&
        host->save_schema_version != 0U && host->save_sequence != 0U;
}

int app_main(int argc, char *argv[])
{
    if (argc != 1 || argv == NULL || argv[0] == NULL) {
        return P4_CARTRIDGE_EXIT_BAD_HOST;
    }
    p4_cartridge_host_v1_t *const host =
        (p4_cartridge_host_v1_t *)(void *)argv[0];
    if (!host_valid(host)) {
        return P4_CARTRIDGE_EXIT_BAD_HOST;
    }

    const p4_game_descriptor_t *const game = &P4_GAME_ENTRY_SYMBOL;
    if (!p4_game_descriptor_valid(game) ||
        strcmp(game->id, host->expected_game_id) != 0) {
        return P4_CARTRIDGE_EXIT_ID_MISMATCH;
    }
    const bool has_unlock_achievement = host_field_present(
        host, offsetof(p4_cartridge_host_v1_t, unlock_achievement),
        sizeof(host->unlock_achievement));
    const bool has_submit_pcm = host_field_present(
        host, offsetof(p4_cartridge_host_v1_t, submit_pcm16_stereo),
        sizeof(host->submit_pcm16_stereo));
    const bool has_resource = host_field_present(
        host, offsetof(p4_cartridge_host_v1_t, resource_format_version),
        sizeof(host->resource_format_version));
    const bool has_signal_scan = host_field_present(
        host, offsetof(p4_cartridge_host_v1_t, read_signal_scan),
        sizeof(host->read_signal_scan));
    const bool has_save = host_field_present(
        host, offsetof(p4_cartridge_host_v1_t, read_save_status),
        sizeof(host->read_save_status));
    uint32_t available_capabilities = host->available_capabilities &
        supported_service_capabilities();
    if (host->play_tone == NULL) {
        available_capabilities &=
            (uint32_t)~(uint32_t)P4_GAME_CAP_AUDIO_TONE;
    }
    if (!has_submit_pcm || host->submit_pcm16_stereo == NULL) {
        available_capabilities &=
            (uint32_t)~(uint32_t)P4_GAME_CAP_AUDIO_STREAM;
    }
    if (!has_resource || host->resource_data == NULL ||
        host->resource_bytes == 0U || host->resource_format_version == 0U) {
        available_capabilities &=
            (uint32_t)~(uint32_t)P4_GAME_CAP_STORAGE;
    }
    if (!has_signal_scan || host->request_signal_scan == NULL ||
        host->read_signal_scan == NULL) {
        available_capabilities &=
            (uint32_t)~(uint32_t)P4_GAME_CAP_SIGNAL_SCAN;
    }
    if (!has_save || host->queue_save == NULL ||
        host->read_save_status == NULL || !host_save_snapshot_valid(host)) {
        available_capabilities &=
            (uint32_t)~(uint32_t)P4_GAME_CAP_SAVE;
    }
    if ((game->required_capabilities & ~available_capabilities) != 0U) {
        return P4_CARTRIDGE_EXIT_CAPABILITY_MISSING;
    }

    void *const state = calloc(1U, game->state_bytes);
    if (state == NULL) {
        return P4_CARTRIDGE_EXIT_NO_MEMORY;
    }
    const p4_game_services_t services = {
        .available_capabilities = available_capabilities,
        .audio_context = host,
        .game_id = game->id,
        .play_tone = host->play_tone == NULL ? NULL : host_play_tone,
        .submit_pcm16_stereo =
            (available_capabilities & P4_GAME_CAP_AUDIO_STREAM) != 0U
            ? host_submit_pcm16_stereo : NULL,
        .stop_audio = host->stop_audio == NULL ? NULL : host_stop_audio,
        .achievement_context = host,
        .unlock_achievement = !has_unlock_achievement ||
            host->unlock_achievement == NULL
            ? NULL : host_unlock_achievement,
        .resource_data = has_resource ? host->resource_data : NULL,
        .resource_bytes = has_resource ? host->resource_bytes : 0U,
        .resource_format_version = has_resource
            ? host->resource_format_version : 0U,
        .signal_scan_context = host,
        .request_signal_scan =
            (available_capabilities & P4_GAME_CAP_SIGNAL_SCAN) != 0U
            ? host_request_signal_scan : NULL,
        .read_signal_scan =
            (available_capabilities & P4_GAME_CAP_SIGNAL_SCAN) != 0U
            ? host_read_signal_scan : NULL,
        .save_context =
            (available_capabilities & P4_GAME_CAP_SAVE) != 0U ? host : NULL,
        .save_data = (available_capabilities & P4_GAME_CAP_SAVE) != 0U
            ? host->save_data : NULL,
        .save_bytes = (available_capabilities & P4_GAME_CAP_SAVE) != 0U
            ? host->save_bytes : 0U,
        .save_schema_version =
            (available_capabilities & P4_GAME_CAP_SAVE) != 0U
            ? host->save_schema_version : 0U,
        .save_sequence = (available_capabilities & P4_GAME_CAP_SAVE) != 0U
            ? host->save_sequence : 0U,
        .queue_save = (available_capabilities & P4_GAME_CAP_SAVE) != 0U
            ? host_queue_save : NULL,
        .read_save_status =
            (available_capabilities & P4_GAME_CAP_SAVE) != 0U
            ? host_read_save_status : NULL,
    };
    p4_game_instance_t instance = {0};
    if (!p4_game_instance_start(
            &instance, game, &services, state, game->state_bytes)) {
        free(state);
        return P4_CARTRIDGE_EXIT_START_FAILED;
    }

    p4_cartridge_exit_t exit_code = P4_CARTRIDGE_EXIT_OK;
    p4_game_result_t result = P4_GAME_CONTINUE;
    if (!p4_game_instance_render(&instance, &host->surface) ||
        !host->present(host->context)) {
        exit_code = P4_CARTRIDGE_EXIT_RENDER_FAILED;
        result = P4_GAME_ERROR;
    }

    while (result == P4_GAME_CONTINUE) {
        p4_game_input_t input;
        uint32_t elapsed_ms = 0U;
        if (!host->poll_frame(host->context, &input, &elapsed_ms)) {
            exit_code = P4_CARTRIDGE_EXIT_HOST_FAILED;
            result = P4_GAME_ERROR;
            break;
        }
        result = p4_game_instance_update(&instance, &input, elapsed_ms);
        if (result == P4_GAME_ERROR) {
            exit_code = P4_CARTRIDGE_EXIT_GAME_FAILED;
            break;
        }
        if (result == P4_GAME_CONTINUE &&
            (!p4_game_instance_render(&instance, &host->surface) ||
             !host->present(host->context))) {
            exit_code = P4_CARTRIDGE_EXIT_RENDER_FAILED;
            result = P4_GAME_ERROR;
            break;
        }
    }

    p4_game_instance_stop(&instance);
    free(state);
    if (host->finished != NULL) {
        host->finished(host->context, result);
    }
    return (int)exit_code;
}
