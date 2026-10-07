// SPDX-License-Identifier: GPL-2.0-or-later

#include "doom/audio_runtime.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "doom/audio_ring.h"
#include "doom/music_synth.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

#define DOOM_AUDIO_WORKER_CHUNK_FRAMES ((size_t)128U)
#define DOOM_AUDIO_WORKER_STACK_BYTES ((uint32_t)6144U)
#define DOOM_AUDIO_WORKER_PRIORITY ((UBaseType_t)(tskIDLE_PRIORITY + 3U))
#define DOOM_AUDIO_STOP_TIMEOUT_MS UINT32_C(250)
#define DOOM_AUDIO_STATE_ACTIVE UINT32_C(1)
#define DOOM_AUDIO_STATE_GENERATION_STEP UINT32_C(2)

typedef enum {
    RUNTIME_UNBOUND = 0,
    RUNTIME_BOUND,
    RUNTIME_STARTING,
    RUNTIME_RUNNING,
    RUNTIME_STOPPING,
    RUNTIME_FAILED,
} runtime_state_t;

static platform_audio_t *s_platform_audio;
static doom_audio_command_ring_t s_command_ring;
static atomic_uint_least32_t s_desired_voice_state[DOOM_AUDIO_MAX_VOICES];
static atomic_bool s_stop_requested;
static atomic_int s_runtime_state;
static atomic_uint_least32_t s_commands_enqueued;
static atomic_uint_least32_t s_commands_dropped;
static atomic_uint_least32_t s_frames_rendered;
static atomic_uint_least32_t s_write_failures;
static atomic_uint_least32_t s_music_songs_started;
static atomic_uint_least32_t s_music_events_processed;
static atomic_uint_least32_t s_music_notes_started;
static atomic_uint_least32_t s_music_loops_completed;
static atomic_uint_least32_t s_music_mixed_frames;
static atomic_uint_least32_t s_music_parse_failures;
static atomic_uint_least32_t s_music_maximum_absolute_mix;
static atomic_uint_least32_t s_music_volume;
static atomic_bool s_music_playing;
static atomic_bool s_music_paused;
/* A constant static initializer is the C11 equivalent of ATOMIC_VAR_INIT. */
static atomic_uint_least32_t s_worker_stack_hwm_bytes = UINT32_MAX;
/* A single bounded desired-music mailbox is independent of SFX saturation.
 * The critical section covers publication/admission and ownership transfer only;
 * heap operations and the player remain outside it on their owning tasks. */
typedef struct {
    doom_audio_command_type_t type;
    doom_audio_command_type_t pause_action;
    doom_audio_command_type_t prior_pause_action;
    bool stop_before_play;
    doom_music_song_t *song;
    bool looping;
} music_request_t;

static portMUX_TYPE s_music_request_lock = portMUX_INITIALIZER_UNLOCKED;
static music_request_t s_music_request;

static music_request_t take_music_request(void)
{
    portENTER_CRITICAL(&s_music_request_lock);
    const music_request_t request = s_music_request;
    s_music_request = (music_request_t){0};
    portEXIT_CRITICAL(&s_music_request_lock);
    return request;
}

static void discard_music_request(void)
{
    const music_request_t request = take_music_request();
    doom_music_song_release(request.song);
}

static void apply_music_pause_action(doom_music_player_t *music,
                                      doom_audio_command_type_t action)
{
    if (action == DOOM_AUDIO_COMMAND_MUSIC_PAUSE) {
        doom_music_player_pause(music);
    } else if (action == DOOM_AUDIO_COMMAND_MUSIC_RESUME) {
        doom_music_player_resume(music);
    }
}

static void process_music_request(doom_music_player_t *music)
{
    const music_request_t request = take_music_request();
    if (request.type == DOOM_AUDIO_COMMAND_MUSIC_PLAY) {
        /* Keep explicit controls meaningful even if replacement allocation
         * fails and the player would otherwise retain the previous song. */
        if (request.stop_before_play) {
            doom_music_player_stop(music);
        } else {
            apply_music_pause_action(music, request.prior_pause_action);
        }
        (void)doom_music_player_start(music, request.song, request.looping);
        doom_music_song_release(request.song);
    } else if (request.type == DOOM_AUDIO_COMMAND_MUSIC_STOP) {
        doom_music_player_stop(music);
    }
    apply_music_pause_action(music, request.type);
    apply_music_pause_action(music, request.pause_action);
}

static SemaphoreHandle_t s_worker_gate;
static SemaphoreHandle_t s_worker_done;
static TaskHandle_t s_worker_task;

static void publish_worker_stack_hwm(void)
{
    const uint32_t observed =
        (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    uint32_t previous = atomic_load_explicit(
        &s_worker_stack_hwm_bytes, memory_order_relaxed);
    while (observed < previous &&
           !atomic_compare_exchange_weak_explicit(
               &s_worker_stack_hwm_bytes, &previous, observed,
               memory_order_relaxed, memory_order_relaxed)) {
    }
}

static void delete_worker_synchronization(void)
{
    if (s_worker_done != NULL) {
        vSemaphoreDelete(s_worker_done);
        s_worker_done = NULL;
    }
    if (s_worker_gate != NULL) {
        vSemaphoreDelete(s_worker_gate);
        s_worker_gate = NULL;
    }
    s_worker_task = NULL;
}

/*
 * A failed module Init is not followed by module Shutdown in the pinned
 * engine. Restore BOUND here only when the borrowed backend is demonstrably
 * muted and the amplifier shutdown operation succeeded, so the app can
 * unbind/destroy it even though sound initialization returned false.
 */
static esp_err_t restore_bound_after_start_failure(esp_err_t primary_result)
{
    const esp_err_t safe_result = platform_audio_force_safe_shutdown();
    platform_audio_state_t platform_state = PLATFORM_AUDIO_STATE_RUNNING;
    const esp_err_t state_result = platform_audio_get_state(
        s_platform_audio, &platform_state);
    const bool restored = safe_result == ESP_OK && state_result == ESP_OK &&
        platform_state == PLATFORM_AUDIO_STATE_READY_MUTED;
    if (restored) {
        atomic_store_explicit(&s_runtime_state, RUNTIME_BOUND,
                              memory_order_release);
    } else {
        atomic_store_explicit(&s_runtime_state, RUNTIME_FAILED,
                              memory_order_release);
    }
    if (primary_result != ESP_OK) {
        return primary_result;
    }
    if (safe_result != ESP_OK) {
        return safe_result;
    }
    if (state_result != ESP_OK) {
        return state_result;
    }
    return restored ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static uint32_t next_desired_state(uint32_t previous, bool active)
{
    return ((previous + DOOM_AUDIO_STATE_GENERATION_STEP) &
            ~DOOM_AUDIO_STATE_ACTIVE) |
           (active ? DOOM_AUDIO_STATE_ACTIVE : UINT32_C(0));
}

static uint32_t replace_desired_state(size_t voice_index, bool active)
{
    atomic_uint_least32_t *const destination =
        &s_desired_voice_state[voice_index];
    uint32_t previous =
        atomic_load_explicit(destination, memory_order_relaxed);
    for (;;) {
        const uint32_t replacement = next_desired_state(previous, active);
        if (atomic_compare_exchange_weak_explicit(
                destination, &previous, replacement, memory_order_release,
                memory_order_relaxed)) {
            return replacement;
        }
    }
}

static void mark_voice_finished(size_t voice_index, uint32_t expected_state)
{
    if ((expected_state & DOOM_AUDIO_STATE_ACTIVE) == 0U) {
        return;
    }
    const uint32_t inactive = expected_state & ~DOOM_AUDIO_STATE_ACTIVE;
    (void)atomic_compare_exchange_strong_explicit(
        &s_desired_voice_state[voice_index], &expected_state, inactive,
        memory_order_release, memory_order_relaxed);
}

static void process_commands(doom_audio_mixer_t *mixer,
                             uint32_t actual_state[DOOM_AUDIO_MAX_VOICES])
{
    doom_audio_command_t command;
    while (doom_audio_command_ring_pop(&s_command_ring, &command)) {
        const size_t voice_index = command.voice_index;
        if (voice_index >= (size_t)DOOM_AUDIO_MAX_VOICES) {
            continue;
        }
        const uint32_t desired = atomic_load_explicit(
            &s_desired_voice_state[voice_index], memory_order_acquire);
        if (desired != command.desired_state ||
            (desired & DOOM_AUDIO_STATE_ACTIVE) == 0U) {
            continue;
        }
        if (command.type == DOOM_AUDIO_COMMAND_START &&
            doom_audio_mixer_start(mixer, voice_index, command.sample,
                                   command.volume, command.separation)) {
            actual_state[voice_index] = desired;
        } else if (command.type == DOOM_AUDIO_COMMAND_UPDATE) {
            (void)doom_audio_mixer_update(
                mixer, voice_index, command.volume, command.separation);
        } else {
            mark_voice_finished(voice_index, desired);
        }
    }
}

static void publish_music_stats(const doom_music_player_t *music)
{
    doom_music_stats_t stats = {0};
    doom_music_player_get_stats(music, &stats);
    atomic_store_explicit(
        &s_music_songs_started, stats.songs_started, memory_order_relaxed);
    atomic_store_explicit(
        &s_music_events_processed, stats.events_processed,
        memory_order_relaxed);
    atomic_store_explicit(
        &s_music_notes_started, stats.notes_started, memory_order_relaxed);
    atomic_store_explicit(
        &s_music_loops_completed, stats.loops_completed,
        memory_order_relaxed);
    atomic_store_explicit(
        &s_music_mixed_frames, stats.mixed_frames, memory_order_relaxed);
    atomic_store_explicit(
        &s_music_parse_failures, stats.parse_failures, memory_order_relaxed);
    atomic_store_explicit(
        &s_music_maximum_absolute_mix, stats.maximum_absolute_mix,
        memory_order_relaxed);
    atomic_store_explicit(
        &s_music_playing, stats.playing, memory_order_release);
    atomic_store_explicit(
        &s_music_paused, stats.paused, memory_order_release);
}

static void reconcile_stops(doom_audio_mixer_t *mixer,
                            uint32_t actual_state[DOOM_AUDIO_MAX_VOICES])
{
    for (size_t voice_index = 0U;
         voice_index < (size_t)DOOM_AUDIO_MAX_VOICES; ++voice_index) {
        const uint32_t desired = atomic_load_explicit(
            &s_desired_voice_state[voice_index], memory_order_acquire);
        if (desired != actual_state[voice_index] &&
            (desired & DOOM_AUDIO_STATE_ACTIVE) == 0U) {
            (void)doom_audio_mixer_stop(mixer, voice_index);
            actual_state[voice_index] = desired;
        }
    }
}

static void publish_finished_voices(
    const doom_audio_mixer_t *mixer,
    uint32_t actual_state[DOOM_AUDIO_MAX_VOICES])
{
    for (size_t voice_index = 0U;
         voice_index < (size_t)DOOM_AUDIO_MAX_VOICES; ++voice_index) {
        if ((actual_state[voice_index] & DOOM_AUDIO_STATE_ACTIVE) != 0U &&
            !doom_audio_mixer_voice_active(mixer, voice_index)) {
            mark_voice_finished(voice_index, actual_state[voice_index]);
            actual_state[voice_index] &= ~DOOM_AUDIO_STATE_ACTIVE;
        }
    }
}

static void worker_task(void *unused)
{
    (void)unused;
    doom_audio_mixer_t mixer;
    doom_music_player_t music;
    uint32_t actual_state[DOOM_AUDIO_MAX_VOICES] = {0};
    int16_t output[DOOM_AUDIO_WORKER_CHUNK_FRAMES *
                   (size_t)DOOM_AUDIO_CHANNEL_COUNT];
    doom_audio_mixer_init(&mixer);
    doom_music_player_init(&music);
    publish_worker_stack_hwm();

    (void)xSemaphoreTake(s_worker_gate, portMAX_DELAY);
    while (!atomic_load_explicit(&s_stop_requested, memory_order_acquire)) {
        (void)doom_music_player_set_volume(
            &music,
            (uint8_t)atomic_load_explicit(
                &s_music_volume, memory_order_relaxed));
        process_commands(&mixer, actual_state);
        process_music_request(&music);
        reconcile_stops(&mixer, actual_state);
        if (!doom_audio_mixer_render(
                &mixer, output, DOOM_AUDIO_WORKER_CHUNK_FRAMES)) {
            atomic_store_explicit(
                &s_runtime_state, RUNTIME_FAILED, memory_order_release);
            break;
        }
        if (!doom_music_player_mix(
                &music, output, DOOM_AUDIO_WORKER_CHUNK_FRAMES)) {
            doom_music_player_stop(&music);
        }
        publish_music_stats(&music);
        publish_finished_voices(&mixer, actual_state);
        const esp_err_t write_result = platform_audio_write_frames(
            s_platform_audio, output, DOOM_AUDIO_WORKER_CHUNK_FRAMES);
        if (write_result != ESP_OK) {
            (void)atomic_fetch_add_explicit(
                &s_write_failures, UINT32_C(1), memory_order_relaxed);
            atomic_store_explicit(
                &s_runtime_state, RUNTIME_FAILED, memory_order_release);
            break;
        }
        (void)atomic_fetch_add_explicit(
            &s_frames_rendered, (uint32_t)DOOM_AUDIO_WORKER_CHUNK_FRAMES,
            memory_order_relaxed);
        publish_worker_stack_hwm();
    }

    doom_music_player_stop(&music);
    /* Admission is closed (not RUNNING) before this locked final detach. */
    discard_music_request();
    publish_music_stats(&music);
    const int state =
        atomic_load_explicit(&s_runtime_state, memory_order_acquire);
    if (state == RUNTIME_FAILED) {
        (void)platform_audio_stop(s_platform_audio);
        (void)platform_audio_force_safe_shutdown();
    }
    publish_worker_stack_hwm();
    (void)xSemaphoreGive(s_worker_done);
    vTaskDelete(NULL);
}

static void reset_runtime_queues(void)
{
    /* A previous worker has joined; still detach rather than overwrite a ref. */
    discard_music_request();
    doom_audio_command_ring_init(&s_command_ring);
    for (size_t voice_index = 0U;
         voice_index < (size_t)DOOM_AUDIO_MAX_VOICES; ++voice_index) {
        atomic_store_explicit(&s_desired_voice_state[voice_index], UINT32_C(0),
                              memory_order_relaxed);
    }
    atomic_store_explicit(&s_stop_requested, false, memory_order_relaxed);
    atomic_store_explicit(&s_commands_enqueued, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_commands_dropped, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_frames_rendered, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_write_failures, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_songs_started, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_events_processed, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_notes_started, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_loops_completed, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_mixed_frames, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_parse_failures, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_maximum_absolute_mix, UINT32_C(0),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_volume, UINT32_C(64),
                          memory_order_relaxed);
    atomic_store_explicit(&s_music_playing, false, memory_order_relaxed);
    atomic_store_explicit(&s_music_paused, false, memory_order_relaxed);
    atomic_store_explicit(&s_worker_stack_hwm_bytes, UINT32_MAX,
                          memory_order_relaxed);
}

esp_err_t doom_audio_runtime_bind(const doom_audio_runtime_config_t *config)
{
    if (config == NULL || config->platform_audio == NULL ||
        config->sample_rate_hz != (uint32_t)DOOM_AUDIO_OUTPUT_RATE_HZ ||
        !config->display_owns_ldo3_ldo4) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_load_explicit(&s_runtime_state, memory_order_acquire) !=
        RUNTIME_UNBOUND) {
        return ESP_ERR_INVALID_STATE;
    }
    platform_audio_state_t platform_state;
    const esp_err_t state_result = platform_audio_get_state(
        config->platform_audio, &platform_state);
    if (state_result != ESP_OK) {
        return state_result;
    }
    if (platform_state != PLATFORM_AUDIO_STATE_READY_MUTED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_platform_audio = config->platform_audio;
    atomic_store_explicit(&s_runtime_state, RUNTIME_BOUND,
                          memory_order_release);
    return ESP_OK;
}

esp_err_t doom_audio_runtime_start(void)
{
    int expected = RUNTIME_BOUND;
    if (!atomic_compare_exchange_strong_explicit(
            &s_runtime_state, &expected, RUNTIME_STARTING,
            memory_order_acq_rel, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    reset_runtime_queues();
    s_worker_gate = xSemaphoreCreateBinary();
    s_worker_done = xSemaphoreCreateBinary();
    if (s_worker_gate == NULL || s_worker_done == NULL) {
        delete_worker_synchronization();
        return restore_bound_after_start_failure(ESP_ERR_NO_MEM);
    }

    if (xTaskCreate(worker_task, "doom_audio", DOOM_AUDIO_WORKER_STACK_BYTES,
                    NULL, DOOM_AUDIO_WORKER_PRIORITY, &s_worker_task) != pdPASS) {
        delete_worker_synchronization();
        return restore_bound_after_start_failure(ESP_ERR_NO_MEM);
    }

    const esp_err_t start_result = platform_audio_start(s_platform_audio);
    if (start_result != ESP_OK) {
        atomic_store_explicit(&s_stop_requested, true, memory_order_release);
        (void)xSemaphoreGive(s_worker_gate);
        const BaseType_t worker_stopped = xSemaphoreTake(
            s_worker_done, pdMS_TO_TICKS(DOOM_AUDIO_STOP_TIMEOUT_MS));
        if (worker_stopped != pdTRUE) {
            /*
             * The worker may still access both semaphores and the borrowed
             * backend. Keep every resource live for a later runtime_stop().
             */
            (void)platform_audio_force_safe_shutdown();
            atomic_store_explicit(&s_runtime_state, RUNTIME_FAILED,
                                  memory_order_release);
            return ESP_ERR_TIMEOUT;
        }
        delete_worker_synchronization();
        return restore_bound_after_start_failure(start_result);
    }
    atomic_store_explicit(&s_runtime_state, RUNTIME_RUNNING,
                          memory_order_release);
    (void)xSemaphoreGive(s_worker_gate);
    return ESP_OK;
}

esp_err_t doom_audio_runtime_stop(void)
{
    const int state =
        atomic_load_explicit(&s_runtime_state, memory_order_acquire);
    if (state != RUNTIME_RUNNING && state != RUNTIME_FAILED) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store_explicit(&s_runtime_state, RUNTIME_STOPPING,
                          memory_order_release);
    atomic_store_explicit(&s_stop_requested, true, memory_order_release);
    if (s_worker_gate != NULL) {
        (void)xSemaphoreGive(s_worker_gate);
    }

    const BaseType_t worker_stopped = s_worker_done == NULL
        ? pdTRUE
        : xSemaphoreTake(
              s_worker_done, pdMS_TO_TICKS(DOOM_AUDIO_STOP_TIMEOUT_MS));
    if (worker_stopped != pdTRUE) {
        /* Never race the worker through the single-owner platform handle. */
        (void)platform_audio_force_safe_shutdown();
        atomic_store_explicit(&s_runtime_state, RUNTIME_FAILED,
                              memory_order_release);
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t stop_result = platform_audio_stop(s_platform_audio);
    delete_worker_synchronization();
    return restore_bound_after_start_failure(stop_result);
}

esp_err_t doom_audio_runtime_unbind(void)
{
    int expected = RUNTIME_BOUND;
    if (!atomic_compare_exchange_strong_explicit(
            &s_runtime_state, &expected, RUNTIME_UNBOUND,
            memory_order_acq_rel, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_platform_audio = NULL;
    return ESP_OK;
}

bool doom_audio_runtime_start_voice(size_t voice_index,
                                    const doom_audio_sample_t *sample,
                                    uint8_t volume,
                                    uint8_t separation)
{
    if (atomic_load_explicit(&s_runtime_state, memory_order_acquire) !=
            RUNTIME_RUNNING ||
        voice_index >= (size_t)DOOM_AUDIO_MAX_VOICES || sample == NULL ||
        sample->samples == NULL || sample->sample_count == 0U ||
        volume > UINT8_C(127) || separation > UINT8_C(254)) {
        return false;
    }
    const uint32_t desired_state = replace_desired_state(voice_index, true);
    const doom_audio_command_t command = {
        .type = DOOM_AUDIO_COMMAND_START,
        .voice_index = (uint8_t)voice_index,
        .volume = volume,
        .separation = separation,
        .desired_state = desired_state,
        .sample = sample,
    };
    if (!doom_audio_command_ring_push(&s_command_ring, &command)) {
        (void)replace_desired_state(voice_index, false);
        (void)atomic_fetch_add_explicit(
            &s_commands_dropped, UINT32_C(1), memory_order_relaxed);
        return false;
    }
    (void)atomic_fetch_add_explicit(
        &s_commands_enqueued, UINT32_C(1), memory_order_relaxed);
    return true;
}

void doom_audio_runtime_stop_voice(size_t voice_index)
{
    if (voice_index < (size_t)DOOM_AUDIO_MAX_VOICES) {
        (void)replace_desired_state(voice_index, false);
    }
}

bool doom_audio_runtime_update_voice(size_t voice_index,
                                     uint8_t volume,
                                     uint8_t separation)
{
    if (atomic_load_explicit(&s_runtime_state, memory_order_acquire) !=
            RUNTIME_RUNNING ||
        voice_index >= (size_t)DOOM_AUDIO_MAX_VOICES ||
        volume > UINT8_C(127) || separation > UINT8_C(254)) {
        return false;
    }
    const uint32_t desired_state = atomic_load_explicit(
        &s_desired_voice_state[voice_index], memory_order_acquire);
    if ((desired_state & DOOM_AUDIO_STATE_ACTIVE) == 0U) {
        return false;
    }
    const doom_audio_command_t command = {
        .type = DOOM_AUDIO_COMMAND_UPDATE,
        .voice_index = (uint8_t)voice_index,
        .volume = volume,
        .separation = separation,
        .desired_state = desired_state,
        .sample = NULL,
    };
    if (!doom_audio_command_ring_push(&s_command_ring, &command)) {
        (void)atomic_fetch_add_explicit(
            &s_commands_dropped, UINT32_C(1), memory_order_relaxed);
        return false;
    }
    (void)atomic_fetch_add_explicit(
        &s_commands_enqueued, UINT32_C(1), memory_order_relaxed);
    return true;
}

bool doom_audio_runtime_voice_active(size_t voice_index)
{
    return atomic_load_explicit(&s_runtime_state, memory_order_acquire) ==
               RUNTIME_RUNNING &&
           voice_index < (size_t)DOOM_AUDIO_MAX_VOICES &&
           (atomic_load_explicit(&s_desired_voice_state[voice_index],
                                 memory_order_acquire) &
            DOOM_AUDIO_STATE_ACTIVE) != 0U;
}

static bool enqueue_music_command(doom_audio_command_type_t type,
                                  doom_music_song_t *song,
                                  bool looping)
{
    doom_music_song_t *displaced_song = NULL;
    portENTER_CRITICAL(&s_music_request_lock);
    if (atomic_load_explicit(&s_runtime_state, memory_order_acquire) !=
        RUNTIME_RUNNING) {
        portEXIT_CRITICAL(&s_music_request_lock);
        return false;
    }
    if (type == DOOM_AUDIO_COMMAND_MUSIC_PLAY ||
        type == DOOM_AUDIO_COMMAND_MUSIC_STOP) {
        displaced_song = s_music_request.song;
        const bool stop_before_play =
            s_music_request.type == DOOM_AUDIO_COMMAND_MUSIC_STOP ||
            s_music_request.stop_before_play;
        doom_audio_command_type_t prior_pause_action =
            s_music_request.prior_pause_action;
        if (s_music_request.type == DOOM_AUDIO_COMMAND_MUSIC_PAUSE ||
            s_music_request.type == DOOM_AUDIO_COMMAND_MUSIC_RESUME) {
            prior_pause_action = s_music_request.type;
        } else if (s_music_request.pause_action != 0) {
            prior_pause_action = s_music_request.pause_action;
        }
        s_music_request = (music_request_t){
            .type = type,
            .song = song,
            .looping = looping,
            .stop_before_play = type == DOOM_AUDIO_COMMAND_MUSIC_PLAY &&
                                stop_before_play,
            .prior_pause_action = type == DOOM_AUDIO_COMMAND_MUSIC_PLAY
                ? prior_pause_action : 0,
        };
    } else if (s_music_request.type == DOOM_AUDIO_COMMAND_MUSIC_PLAY) {
        s_music_request.pause_action = type;
    } else if (s_music_request.type != DOOM_AUDIO_COMMAND_MUSIC_STOP) {
        s_music_request.type = type;
    }
    portEXIT_CRITICAL(&s_music_request_lock);
    doom_music_song_release(displaced_song);
    (void)atomic_fetch_add_explicit(
        &s_commands_enqueued, UINT32_C(1), memory_order_relaxed);
    return true;
}

bool doom_audio_runtime_music_play(doom_music_song_t *song, bool looping)
{
    if (!doom_music_song_retain(song)) {
        return false;
    }
    if (!enqueue_music_command(
            DOOM_AUDIO_COMMAND_MUSIC_PLAY, song, looping)) {
        doom_music_song_release(song);
        return false;
    }
    return true;
}

bool doom_audio_runtime_music_stop(void)
{
    return enqueue_music_command(
        DOOM_AUDIO_COMMAND_MUSIC_STOP, NULL, false);
}

bool doom_audio_runtime_music_pause(void)
{
    return enqueue_music_command(
        DOOM_AUDIO_COMMAND_MUSIC_PAUSE, NULL, false);
}

bool doom_audio_runtime_music_resume(void)
{
    return enqueue_music_command(
        DOOM_AUDIO_COMMAND_MUSIC_RESUME, NULL, false);
}

bool doom_audio_runtime_music_set_volume(uint8_t volume)
{
    if (volume > UINT8_C(127) ||
        atomic_load_explicit(&s_runtime_state, memory_order_acquire) !=
            RUNTIME_RUNNING) {
        return false;
    }
    atomic_store_explicit(
        &s_music_volume, (uint32_t)volume, memory_order_release);
    return true;
}

bool doom_audio_runtime_music_is_playing(void)
{
    return atomic_load_explicit(&s_music_playing, memory_order_acquire);
}

esp_err_t doom_audio_runtime_get_stats(doom_audio_runtime_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out_stats->commands_enqueued = atomic_load_explicit(
        &s_commands_enqueued, memory_order_relaxed);
    out_stats->commands_dropped = atomic_load_explicit(
        &s_commands_dropped, memory_order_relaxed);
    out_stats->frames_rendered = atomic_load_explicit(
        &s_frames_rendered, memory_order_relaxed);
    out_stats->write_failures = atomic_load_explicit(
        &s_write_failures, memory_order_relaxed);
    out_stats->worker_stack_hwm_bytes = atomic_load_explicit(
        &s_worker_stack_hwm_bytes, memory_order_relaxed);
    out_stats->music_songs_started = atomic_load_explicit(
        &s_music_songs_started, memory_order_relaxed);
    out_stats->music_events_processed = atomic_load_explicit(
        &s_music_events_processed, memory_order_relaxed);
    out_stats->music_notes_started = atomic_load_explicit(
        &s_music_notes_started, memory_order_relaxed);
    out_stats->music_loops_completed = atomic_load_explicit(
        &s_music_loops_completed, memory_order_relaxed);
    out_stats->music_mixed_frames = atomic_load_explicit(
        &s_music_mixed_frames, memory_order_relaxed);
    out_stats->music_parse_failures = atomic_load_explicit(
        &s_music_parse_failures, memory_order_relaxed);
    out_stats->music_maximum_absolute_mix = atomic_load_explicit(
        &s_music_maximum_absolute_mix, memory_order_relaxed);
    out_stats->music_playing = atomic_load_explicit(
        &s_music_playing, memory_order_acquire);
    out_stats->music_paused = atomic_load_explicit(
        &s_music_paused, memory_order_acquire);
    return ESP_OK;
}
