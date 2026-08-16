// SPDX-License-Identifier: MIT

#include "p4/platform.h"

#include <limits.h>
#include <string.h>

#include "platform/audio.h"

_Static_assert(P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ ==
                   PLATFORM_AUDIO_SAMPLE_RATE_HZ,
               "game audio sample rate differs from platform backend");
_Static_assert(P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT ==
                   PLATFORM_AUDIO_CHANNEL_COUNT,
               "game audio channel count differs from platform backend");
_Static_assert(P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES ==
                   PLATFORM_AUDIO_MAX_WRITE_FRAMES,
               "game audio write bound differs from platform backend");

static platform_audio_t *backend(const p4_game_platform_audio_t *session)
{
    return (platform_audio_t *)session->backend;
}

static void increment(uint32_t *value)
{
    if (*value != UINT32_MAX) {
        ++*value;
    }
}

static void add_frames(uint32_t *value, size_t frames)
{
    const uint32_t bounded = frames > UINT32_MAX
        ? UINT32_MAX : (uint32_t)frames;
    *value = UINT32_MAX - *value < bounded
        ? UINT32_MAX : *value + bounded;
}

void p4_game_platform_audio_init(p4_game_platform_audio_t *session)
{
    if (session != NULL) {
        memset(session, 0, sizeof(*session));
    }
}

static esp_err_t first_error(esp_err_t first, esp_err_t candidate)
{
    return first == ESP_OK ? candidate : first;
}

static esp_err_t fail_safe(p4_game_platform_audio_t *session)
{
    esp_err_t error = ESP_OK;
    platform_audio_t *audio = backend(session);
    if (audio != NULL && session->running) {
        const esp_err_t result = platform_audio_stop(audio);
        error = first_error(error, result);
        if (result == ESP_OK) {
            session->running = false;
            session->safe_high_proven = true;
        }
    }
    if (audio != NULL) {
        const esp_err_t result = platform_audio_destroy(&audio);
        error = first_error(error, result);
        session->backend = audio;
        if (result == ESP_OK && audio == NULL) {
            session->running = false;
        }
    }
    if (session->backend == NULL) {
        const esp_err_t recover_result = platform_audio_recover();
        error = first_error(error, recover_result);
        const esp_err_t high_result = platform_audio_force_safe_shutdown();
        error = first_error(error, high_result);
        session->safe_high_proven =
            recover_result == ESP_OK && high_result == ESP_OK;
    }
    return error;
}

esp_err_t p4_game_platform_audio_open(
    p4_game_platform_audio_t *session,
    bool exact_unit_runtime_authorized,
    void *control_bus,
    uint8_t volume_step)
{
    if (session == NULL || session->backend != NULL || session->running ||
        session->hardware_touched) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!exact_unit_runtime_authorized) {
        return ESP_ERR_NOT_ALLOWED;
    }
    if (volume_step == 0U ||
        volume_step > PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT) {
        return ESP_ERR_INVALID_ARG;
    }
    session->hardware_touched = true;
    increment(&session->opens);
    esp_err_t result = platform_audio_force_safe_shutdown();
    if (result != ESP_OK) {
        (void)fail_safe(session);
        return result;
    }
    session->safe_high_proven = true;
    const platform_audio_config_t config = {
        .control_bus = control_bus,
        .sample_rate_hz = P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ,
        .volume_percent = volume_step,
    };
    platform_audio_t *audio = NULL;
    result = platform_audio_create(&config, &audio);
    session->backend = audio;
    if (result != ESP_OK || audio == NULL) {
        const esp_err_t cleanup = fail_safe(session);
        return cleanup == ESP_OK ? result : cleanup;
    }
    result = platform_audio_start(audio);
    if (result != ESP_OK) {
        const esp_err_t cleanup = fail_safe(session);
        return cleanup == ESP_OK ? result : cleanup;
    }
    session->running = true;
    session->safe_high_proven = false;
    return ESP_OK;
}

esp_err_t p4_game_platform_audio_write(
    p4_game_platform_audio_t *session,
    const int16_t *interleaved_stereo,
    size_t frame_count)
{
    if (session == NULL || !session->running || session->backend == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (interleaved_stereo == NULL || frame_count == 0U ||
        frame_count > P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = platform_audio_write_frames(
        backend(session), interleaved_stereo, frame_count);
    if (result == ESP_OK) {
        increment(&session->writes);
        add_frames(&session->frames_written, frame_count);
        return ESP_OK;
    }
    increment(&session->write_failures);
    const esp_err_t cleanup = fail_safe(session);
    return cleanup == ESP_OK ? result : cleanup;
}

esp_err_t p4_game_platform_audio_close(
    p4_game_platform_audio_t *session)
{
    if (session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!session->hardware_touched) {
        return ESP_OK;
    }
    increment(&session->closes);
    return fail_safe(session);
}

bool p4_game_platform_audio_running(
    const p4_game_platform_audio_t *session)
{
    return session != NULL && session->running && session->backend != NULL;
}
