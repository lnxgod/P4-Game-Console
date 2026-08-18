// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_GAME_H
#define P4_GAME_API_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P4_GAME_API_VERSION UINT32_C(1)

enum {
    P4_GAME_SURFACE_WIDTH = 320,
    P4_GAME_SURFACE_HEIGHT = 200,
    P4_GAME_MAX_STATE_BYTES = 128 * 1024,
    P4_GAME_ID_MAX_BYTES = 48,
    P4_GAME_TITLE_MAX_BYTES = 16,
    P4_GAME_SUBTITLE_MAX_BYTES = 32,
    P4_GAME_MAX_FRAME_DELTA_MS = 100,
    P4_GAME_MAX_AUDIO_STREAM_FRAMES = 256,
    P4_GAME_ACHIEVEMENT_ID_MAX_BYTES = 24,
    P4_GAME_ACHIEVEMENT_TITLE_MAX_BYTES = 24,
    P4_GAME_ACHIEVEMENT_DESCRIPTION_MAX_BYTES = 48,
    P4_GAME_SIGNAL_MAX_RESULTS = 8,
    P4_GAME_SIGNAL_LABEL_MAX_BYTES = 25,
};

typedef enum {
    P4_GAME_CAP_VIDEO = UINT32_C(1) << 0U,
    P4_GAME_CAP_CONTROLS = UINT32_C(1) << 1U,
    P4_GAME_CAP_AUDIO_TONE = UINT32_C(1) << 2U,
    P4_GAME_CAP_AUDIO_STREAM = UINT32_C(1) << 3U,
    P4_GAME_CAP_STORAGE = UINT32_C(1) << 4U,
    P4_GAME_CAP_SIGNAL_SCAN = UINT32_C(1) << 5U,
} p4_game_capability_t;

typedef enum {
    P4_BUTTON_UP = UINT32_C(1) << 0U,
    P4_BUTTON_DOWN = UINT32_C(1) << 1U,
    P4_BUTTON_LEFT = UINT32_C(1) << 2U,
    P4_BUTTON_RIGHT = UINT32_C(1) << 3U,
    P4_BUTTON_A = UINT32_C(1) << 4U,
    P4_BUTTON_B = UINT32_C(1) << 5U,
    P4_BUTTON_START = UINT32_C(1) << 6U,
    P4_BUTTON_BACK = UINT32_C(1) << 7U,
} p4_button_t;

#define P4_BUTTON_MASK UINT32_C(0x000000ff)

typedef struct {
    uint16_t x;
    uint16_t y;
} p4_game_point_t;

typedef struct {
    uint32_t held;
    uint32_t pressed;
    uint32_t released;
    bool touch_valid;
    uint8_t touch_count;
    p4_game_point_t touches[5];
} p4_game_input_t;

typedef struct {
    uint16_t *pixels;
    size_t stride_pixels;
    uint16_t width;
    uint16_t height;
} p4_game_surface_t;

typedef enum {
    P4_WAVE_SQUARE = 0,
    P4_WAVE_TRIANGLE,
} p4_waveform_t;

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint8_t volume_step;
    p4_waveform_t waveform;
} p4_tone_t;

typedef bool (*p4_game_play_tone_fn)(void *context,
                                     const p4_tone_t *tone);
typedef bool (*p4_game_submit_pcm_fn)(void *context,
                                      const int16_t *interleaved_stereo,
                                      size_t frame_count);
typedef void (*p4_game_stop_audio_fn)(void *context);

/** A bounded, game-owned achievement declaration sent to the OS. */
typedef struct {
    const char *game_id;
    const char *id;
    const char *title;
    const char *description;
    uint64_t unlocked_at_elapsed_ms;
} p4_game_achievement_t;

typedef bool (*p4_game_unlock_achievement_fn)(
    void *context,
    const p4_game_achievement_t *achievement);

typedef enum {
    P4_GAME_SIGNAL_IDLE = 0,
    P4_GAME_SIGNAL_SCANNING,
    P4_GAME_SIGNAL_READY,
    P4_GAME_SIGNAL_UNAVAILABLE,
    P4_GAME_SIGNAL_ERROR,
} p4_game_signal_status_t;

enum {
    P4_GAME_SIGNAL_HIDDEN = UINT8_C(1) << 0U,
    P4_GAME_SIGNAL_PROTECTED = UINT8_C(1) << 1U,
};

/**
 * One OS-sanitized signal result. Identity is an opaque, session-scoped,
 * salted token; games never receive a BSSID, credentials, or raw SSID bytes.
 */
typedef struct {
    uint64_t token;
    char label[P4_GAME_SIGNAL_LABEL_MAX_BYTES];
    int8_t rssi_dbm;
    uint8_t channel;
    uint8_t flags;
} p4_game_signal_t;

typedef struct {
    uint32_t generation;
    p4_game_signal_status_t status;
    uint8_t count;
    p4_game_signal_t results[P4_GAME_SIGNAL_MAX_RESULTS];
} p4_game_signal_snapshot_t;

/** Request one non-blocking bounded scan. focus_token may be zero. */
typedef bool (*p4_game_request_signal_scan_fn)(void *context,
                                               uint64_t focus_token);
/** Copy the latest bounded snapshot without blocking. */
typedef bool (*p4_game_read_signal_scan_fn)(
    void *context, p4_game_signal_snapshot_t *snapshot);

typedef struct {
    uint32_t available_capabilities;
    void *audio_context;
    /** The descriptor ID of the foreground game, owned by the Console OS. */
    const char *game_id;
    p4_game_play_tone_fn play_tone;
    p4_game_submit_pcm_fn submit_pcm16_stereo;
    p4_game_stop_audio_fn stop_audio;
    void *achievement_context;
    p4_game_unlock_achievement_fn unlock_achievement;
    /**
     * Optional validated, read-only payload from the game's SD resource
     * sidecar. The host owns this memory for the foreground session.
     */
    const uint8_t *resource_data;
    size_t resource_bytes;
    uint32_t resource_format_version;
    void *signal_scan_context;
    p4_game_request_signal_scan_fn request_signal_scan;
    p4_game_read_signal_scan_fn read_signal_scan;
} p4_game_services_t;

typedef struct {
    void *state;
    size_t state_bytes;
    const p4_game_services_t *services;
    uint64_t elapsed_ms;
    uint32_t frame_index;
} p4_game_context_t;

typedef enum {
    P4_GAME_CONTINUE = 0,
    P4_GAME_EXIT_TO_LAUNCHER,
    P4_GAME_ERROR,
} p4_game_result_t;

typedef bool (*p4_game_start_fn)(p4_game_context_t *context);
typedef p4_game_result_t (*p4_game_update_fn)(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms);
typedef bool (*p4_game_render_fn)(p4_game_context_t *context,
                                  p4_game_surface_t *surface);
typedef void (*p4_game_stop_fn)(p4_game_context_t *context);

typedef struct {
    uint32_t api_version;
    uint32_t launcher_id;
    const char *id;
    const char *title;
    const char *subtitle;
    uint16_t accent_rgb565;
    uint32_t required_capabilities;
    uint32_t optional_capabilities;
    size_t state_bytes;
    p4_game_start_fn start;
    p4_game_update_fn update;
    p4_game_render_fn render;
    p4_game_stop_fn stop;
} p4_game_descriptor_t;

typedef struct {
    const p4_game_descriptor_t *descriptor;
    /** A copied service table keeps callback pointers valid for the session. */
    p4_game_services_t services;
    p4_game_context_t context;
    bool active;
} p4_game_instance_t;

bool p4_game_descriptor_valid(const p4_game_descriptor_t *descriptor);

bool p4_game_instance_start(p4_game_instance_t *instance,
                            const p4_game_descriptor_t *descriptor,
                            const p4_game_services_t *services,
                            void *state_memory,
                            size_t state_memory_bytes);

p4_game_result_t p4_game_instance_update(p4_game_instance_t *instance,
                                         const p4_game_input_t *input,
                                         uint32_t elapsed_ms);

bool p4_game_instance_render(p4_game_instance_t *instance,
                             p4_game_surface_t *surface);

void p4_game_instance_stop(p4_game_instance_t *instance);

bool p4_game_play_tone(p4_game_context_t *context,
                       uint16_t frequency_hz,
                       uint16_t duration_ms,
                       uint8_t volume_step,
                       p4_waveform_t waveform);

/**
 * Submit 1..256 frames of already-mixed signed 16 kHz PCM16 stereo.
 *
 * The host copies accepted frames before returning. A false result means the
 * optional stream service is unavailable, the request is invalid, or its
 * bounded FIFO is full; callers must never spin waiting for space.
 */
bool p4_game_submit_pcm16_stereo(p4_game_context_t *context,
                                 const int16_t *interleaved_stereo,
                                 size_t frame_count);

/**
 * Ask the OS to unlock one bounded, game-scoped achievement. The call is
 * idempotent: an already-unlocked ID is treated as success by the catalog.
 */
bool p4_game_unlock_achievement(p4_game_context_t *context,
                                const char *achievement_id,
                                const char *title,
                                const char *description);

/**
 * Start a non-blocking scan. A nonzero focus token lets an OS-owned provider
 * prioritize live RSSI updates without revealing the underlying identity.
 */
bool p4_game_request_signal_scan(p4_game_context_t *context,
                                 uint64_t focus_token);

/** Read and validate the OS-owned signal snapshot. */
bool p4_game_read_signal_scan(p4_game_context_t *context,
                              p4_game_signal_snapshot_t *snapshot);

void p4_game_stop_audio(p4_game_context_t *context);

#ifdef __cplusplus
}
#endif

#endif
