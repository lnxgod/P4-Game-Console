// SPDX-License-Identifier: MIT

#include "p4/cartridge.h"
#include "p4/game.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef P4_TEST_SAVE_MODE
#error "P4_TEST_SAVE_MODE must be 0 (old), 1 (required), or 2 (optional)"
#endif

extern int app_main(int argc, char *argv[]);

typedef struct {
    bool finished;
    p4_game_result_t result;
    uint32_t presents;
} fixture_host_t;

typedef struct {
    uint32_t updates;
} fixture_game_t;

static bool fixture_start(p4_game_context_t *context)
{
    return context != NULL && context->state != NULL &&
        context->state_bytes == sizeof(fixture_game_t);
}

static p4_game_result_t fixture_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    (void)input;
    (void)elapsed_ms;
    fixture_game_t *const state = context->state;
    ++state->updates;
    return P4_GAME_EXIT_TO_LAUNCHER;
}

static bool fixture_render(p4_game_context_t *context,
                           p4_game_surface_t *surface)
{
    (void)context;
    if (surface == NULL || surface->pixels == NULL) {
        return false;
    }
    surface->pixels[0] = UINT16_C(0x1234);
    return true;
}

static bool fixture_poll_frame(
    void *context, p4_game_input_t *out_input, uint32_t *out_elapsed_ms)
{
    (void)context;
    if (out_input == NULL || out_elapsed_ms == NULL) {
        return false;
    }
    *out_input = (p4_game_input_t){0};
    *out_elapsed_ms = 16U;
    return true;
}

static bool fixture_present(void *context)
{
    fixture_host_t *const fixture = context;
    if (fixture == NULL) {
        return false;
    }
    ++fixture->presents;
    return true;
}

static bool fixture_play_tone(void *context, const p4_tone_t *tone)
{
    return context != NULL && tone != NULL;
}

static bool fixture_submit_pcm(
    void *context, const int16_t *samples, size_t frame_count)
{
    return context != NULL && samples != NULL && frame_count != 0U;
}

static void fixture_stop_audio(void *context)
{
    (void)context;
}

static bool fixture_unlock(
    void *context, const p4_game_achievement_t *achievement)
{
    return context != NULL && achievement != NULL;
}

static bool fixture_request_signal(void *context, uint64_t focus_token)
{
    (void)focus_token;
    return context != NULL;
}

static bool fixture_read_signal(
    void *context, p4_game_signal_snapshot_t *snapshot)
{
    if (context == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = (p4_game_signal_snapshot_t){
        .status = P4_GAME_SIGNAL_IDLE,
    };
    return true;
}

static bool fixture_queue_save(
    void *context,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *data,
    size_t data_bytes,
    p4_game_save_ticket_t *ticket_out)
{
    (void)expected_sequence;
    if (context == NULL || slot_id == NULL || schema_version == 0U ||
        data == NULL || data_bytes == 0U || ticket_out == NULL) {
        return false;
    }
    *ticket_out = UINT32_C(1);
    return true;
}

static bool fixture_read_save(
    void *context,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out)
{
    if (context == NULL || ticket == P4_GAME_SAVE_INVALID_TICKET ||
        status_out == NULL || committed_sequence_out == NULL) {
        return false;
    }
    *status_out = P4_GAME_SAVE_QUEUED;
    *committed_sequence_out = 0U;
    return true;
}

static void fixture_finished(void *context, p4_game_result_t result)
{
    fixture_host_t *const fixture = context;
    if (fixture != NULL) {
        fixture->finished = true;
        fixture->result = result;
    }
}

const p4_game_descriptor_t p4_test_cartridge_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(999),
    .id = "org.p4.compat",
    .title = "COMPAT",
    .subtitle = "HOST TAIL",
    .accent_rgb565 = UINT16_C(0x5fea),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
        (P4_TEST_SAVE_MODE == 1 ? P4_GAME_CAP_SAVE : 0U),
    .optional_capabilities =
        (P4_TEST_SAVE_MODE == 2 ? P4_GAME_CAP_SAVE : 0U),
    .state_bytes = sizeof(fixture_game_t),
    .start = fixture_start,
    .update = fixture_update,
    .render = fixture_render,
};

static int run_with_size(uint32_t struct_bytes)
{
    static uint16_t pixels[
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT];
    fixture_host_t fixture = {0};
    p4_cartridge_host_v1_t host = {
        .magic = P4_CARTRIDGE_HOST_MAGIC,
        .api_version = P4_CARTRIDGE_HOST_API_VERSION,
        .struct_bytes = struct_bytes,
        .available_capabilities = UINT32_MAX,
        .expected_game_id = p4_test_cartridge_game.id,
        .surface = {
            .pixels = pixels,
            .stride_pixels = P4_GAME_SURFACE_WIDTH,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        },
        .context = &fixture,
        .poll_frame = fixture_poll_frame,
        .present = fixture_present,
        .play_tone = fixture_play_tone,
        .stop_audio = fixture_stop_audio,
        .finished = fixture_finished,
        .unlock_achievement = fixture_unlock,
        .submit_pcm16_stereo = fixture_submit_pcm,
        .request_signal_scan = fixture_request_signal,
        .read_signal_scan = fixture_read_signal,
        .queue_save = fixture_queue_save,
        .read_save_status = fixture_read_save,
    };
    char *arguments[] = {(char *)(void *)&host};
    const int result = app_main(1, arguments);
    if (result == P4_CARTRIDGE_EXIT_OK &&
        (!fixture.finished || fixture.result != P4_GAME_EXIT_TO_LAUNCHER ||
         fixture.presents == 0U || pixels[0] != UINT16_C(0x1234))) {
        return P4_CARTRIDGE_EXIT_HOST_FAILED;
    }
    return result;
}

int main(void)
{
    const size_t minimum = offsetof(p4_cartridge_host_v1_t, finished) +
        sizeof(((p4_cartridge_host_v1_t *)0)->finished);
    const size_t complete = sizeof(p4_cartridge_host_v1_t);
    if (minimum > UINT32_MAX || complete > UINT32_MAX) {
        return EXIT_FAILURE;
    }
    for (size_t bytes = minimum; bytes <= complete; ++bytes) {
        const int result = run_with_size((uint32_t)bytes);
#if P4_TEST_SAVE_MODE == 1
        const int expected = bytes == complete
            ? P4_CARTRIDGE_EXIT_OK
            : P4_CARTRIDGE_EXIT_CAPABILITY_MISSING;
#else
        const int expected = P4_CARTRIDGE_EXIT_OK;
#endif
        if (result != expected) {
            fprintf(stderr,
                    "tail compatibility failed mode=%d bytes=%zu "
                    "result=%d expected=%d\n",
                    P4_TEST_SAVE_MODE, bytes, result, expected);
            return EXIT_FAILURE;
        }
    }
    printf("cartridge host tail compatibility passed mode=%d\n",
           P4_TEST_SAVE_MODE);
    return EXIT_SUCCESS;
}
