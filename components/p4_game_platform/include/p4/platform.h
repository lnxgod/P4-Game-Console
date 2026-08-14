// SPDX-License-Identifier: MIT

#ifndef P4_GAME_PLATFORM_H
#define P4_GAME_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ = 16000,
    P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT = 2,
    P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES = 128,
};

typedef struct {
    void *backend;
    bool hardware_touched;
    bool running;
    bool safe_high_proven;
    uint32_t opens;
    uint32_t writes;
    uint32_t frames_written;
    uint32_t write_failures;
    uint32_t closes;
} p4_game_platform_audio_t;

void p4_game_platform_audio_init(p4_game_platform_audio_t *session);

/**
 * Start the exact factory-compatible backend only when the outer firmware's
 * exact-unit runtime gate is true. A false gate returns ESP_ERR_NOT_ALLOWED
 * without touching GPIO, I2S, or the amplifier.
 */
esp_err_t p4_game_platform_audio_open(
    p4_game_platform_audio_t *session,
    bool exact_unit_runtime_authorized,
    uint8_t volume_step);

/** Write 1..128 signed PCM16 stereo frames; failure immediately fails safe. */
esp_err_t p4_game_platform_audio_write(
    p4_game_platform_audio_t *session,
    const int16_t *interleaved_stereo,
    size_t frame_count);

/** Stop, release, recover, and re-prove active-high amplifier shutdown. */
esp_err_t p4_game_platform_audio_close(
    p4_game_platform_audio_t *session);

bool p4_game_platform_audio_running(
    const p4_game_platform_audio_t *session);

#ifdef __cplusplus
}
#endif

#endif
