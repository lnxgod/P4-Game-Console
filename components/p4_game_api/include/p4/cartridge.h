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
typedef bool (*p4_cartridge_submit_pcm_fn)(
    void *context, const int16_t *interleaved_stereo, size_t frame_count);
typedef void (*p4_cartridge_stop_audio_fn)(void *context);
typedef bool (*p4_cartridge_unlock_achievement_fn)(
    void *context, const p4_game_achievement_t *achievement);
typedef bool (*p4_cartridge_request_signal_scan_fn)(
    void *context, uint64_t focus_token);
typedef bool (*p4_cartridge_read_signal_scan_fn)(
    void *context, p4_game_signal_snapshot_t *snapshot);
typedef bool (*p4_cartridge_queue_save_fn)(
    void *context,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *data,
    size_t data_bytes,
    p4_game_save_ticket_t *ticket_out);
typedef bool (*p4_cartridge_read_save_status_fn)(
    void *context,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out);
typedef bool (*p4_cartridge_multiplayer_read_status_fn)(
    void *context, p4_game_multiplayer_status_t *status_out);
typedef bool (*p4_cartridge_multiplayer_send_fn)(
    void *context, const uint8_t *data, size_t data_bytes);
typedef bool (*p4_cartridge_multiplayer_receive_fn)(
    void *context, p4_game_multiplayer_message_t *message_out);
typedef void (*p4_cartridge_finished_fn)(
    void *context, p4_game_result_t result);

/**
 * Stable host table passed to the entry point of a p4-native-elf-v1 game.
 *
 * A cartridge owns no hardware handles. It renders into `surface` and asks
 * the Console OS to poll sanitized input, present a frame, and play tones.
 * The OS may rotate surface.pixels after present; render a complete frame
 * into the current supplied surface and never retain its pixels pointer.
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
    /** Optional v1 extension; old cartridges safely ignore this tail field. */
    p4_cartridge_unlock_achievement_fn unlock_achievement;
    /** Optional v1 extension for copied, bounded PCM16-stereo blocks. */
    p4_cartridge_submit_pcm_fn submit_pcm16_stereo;
    /** Optional v1 extension: validated payload from a same-ID .P4R file. */
    const uint8_t *resource_data;
    size_t resource_bytes;
    uint32_t resource_format_version;
    /** Optional v1 extension for privacy-sanitized, non-blocking scans. */
    p4_cartridge_request_signal_scan_fn request_signal_scan;
    p4_cartridge_read_signal_scan_fn read_signal_scan;
    /** Optional v1 extension: immutable OS-selected launch save snapshot. */
    const uint8_t *save_data;
    size_t save_bytes;
    uint32_t save_schema_version;
    uint32_t save_sequence;
    /** Optional v1 extension: copied, queued save commits and ticket status. */
    p4_cartridge_queue_save_fn queue_save;
    p4_cartridge_read_save_status_fn read_save_status;
    /** Optional v1 extension for one OS-owned bounded P4MP session. */
    p4_cartridge_multiplayer_read_status_fn multiplayer_read_status;
    p4_cartridge_multiplayer_send_fn multiplayer_send;
    p4_cartridge_multiplayer_receive_fn multiplayer_receive;
    /** Optional v1 extension: immutable validated game.json profile. */
    const p4_game_multiplayer_profile_t *multiplayer_profile;
    /** Optional v1 extension; presence is checked using struct_bytes. */
    p4_game_dice_exchange_fn dice_exchange;
    /** v2 dice requests/status support tap-to-hold. Never call through v1. */
    p4_game_dice_exchange_fn dice_exchange_v2;
    /** Optional v1 extension, guarded by struct_bytes and MOTION capability. */
    p4_game_read_motion_fn read_motion;
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
