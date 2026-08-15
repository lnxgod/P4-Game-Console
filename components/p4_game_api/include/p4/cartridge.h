// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_CARTRIDGE_H
#define P4_GAME_API_CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P4_CARTRIDGE_HOST_MAGIC UINT32_C(0x50344348)
#define P4_CARTRIDGE_HOST_API_VERSION UINT32_C(1)

typedef bool (*p4_cartridge_poll_frame_fn)(
    void *context, p4_game_input_t *out_input, uint32_t *out_elapsed_ms);
typedef bool (*p4_cartridge_present_fn)(void *context);
typedef bool (*p4_cartridge_play_tone_fn)(
    void *context, const p4_tone_t *tone);
typedef void (*p4_cartridge_stop_audio_fn)(void *context);
typedef void (*p4_cartridge_finished_fn)(
    void *context, p4_game_result_t result);

/**
 * Stable host table passed to the entry point of a p4-native-elf-v1 game.
 *
 * A cartridge owns no hardware handles. It renders into `surface` and asks
 * the Console OS to poll sanitized input, present a frame, and play tones.
 * The table is intentionally small so stored games do not bind to ESP-IDF.
 */
typedef struct {
    uint32_t magic;
    uint32_t api_version;
    uint32_t struct_bytes;
    uint32_t available_capabilities;
    const char *expected_game_id;
    p4_game_surface_t surface;
    void *context;
    p4_cartridge_poll_frame_fn poll_frame;
    p4_cartridge_present_fn present;
    p4_cartridge_play_tone_fn play_tone;
    p4_cartridge_stop_audio_fn stop_audio;
    p4_cartridge_finished_fn finished;
} p4_cartridge_host_v1_t;

typedef enum {
    P4_CARTRIDGE_EXIT_OK = 0,
    P4_CARTRIDGE_EXIT_BAD_HOST = 10,
    P4_CARTRIDGE_EXIT_ID_MISMATCH = 11,
    P4_CARTRIDGE_EXIT_CAPABILITY_MISSING = 12,
    P4_CARTRIDGE_EXIT_NO_MEMORY = 13,
    P4_CARTRIDGE_EXIT_START_FAILED = 14,
    P4_CARTRIDGE_EXIT_RENDER_FAILED = 15,
    P4_CARTRIDGE_EXIT_HOST_FAILED = 16,
    P4_CARTRIDGE_EXIT_GAME_FAILED = 17,
} p4_cartridge_exit_t;

#ifdef __cplusplus
}
#endif

#endif
