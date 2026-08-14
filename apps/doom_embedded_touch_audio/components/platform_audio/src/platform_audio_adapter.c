// SPDX-License-Identifier: GPL-2.0-or-later

#include "platform/audio.h"

#include <stddef.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

_Static_assert((int)PLATFORM_AUDIO_STATE_READY_MUTED ==
                   (int)PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED,
               "ready/muted ABI value changed");
_Static_assert((int)PLATFORM_AUDIO_STATE_RUNNING ==
                   (int)PLATFORM_AUDIO_FACTORY_STATE_RUNNING,
               "running ABI value changed");
_Static_assert((int)PLATFORM_AUDIO_STATE_FAILED_SAFE ==
                   (int)PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE,
               "failed-safe ABI value changed");

static atomic_uint_least32_t s_invocation_count;
static atomic_uint_least32_t s_write_calls_succeeded;
static atomic_uint_least32_t s_frames_forwarded;
static atomic_uint_least32_t s_nonzero_frames_forwarded;
static atomic_uint_least32_t s_nonzero_samples_forwarded;
static atomic_uint_least16_t s_observed_absolute_peak;
static atomic_bool s_running_low_readback_proven_at_start;
static atomic_bool s_ready_muted_zero_dma_proven;
/*
 * The factory backend is a task-context singleton with an externally
 * serialized API.  Doom's worker owns normal writes, while timeout recovery
 * may arrive from the engine task.  Never wait behind a potentially stuck
 * I2S write: a concurrent call or snapshot fails closed without entering the
 * backend.  The same nonblocking gate makes each accepted statistics snapshot
 * coherent without a task spinlock or a racy plain-payload seqlock.
 */
static atomic_flag s_backend_call_active = ATOMIC_FLAG_INIT;

static bool try_backend_call(void)
{
    return !atomic_flag_test_and_set_explicit(
        &s_backend_call_active, memory_order_acquire);
}

static void finish_backend_call(void)
{
    atomic_flag_clear_explicit(&s_backend_call_active, memory_order_release);
}

static void saturating_add(atomic_uint_least32_t *destination,
                           uint_least32_t increment)
{
    uint_least32_t current = atomic_load_explicit(
        destination, memory_order_relaxed);
    while (current != UINT32_MAX) {
        const uint_least32_t remaining = UINT32_MAX - current;
        const uint_least32_t replacement = current +
            (increment > remaining ? remaining : increment);
        if (atomic_compare_exchange_weak_explicit(
                destination, &current, replacement,
                memory_order_relaxed, memory_order_relaxed)) {
            return;
        }
    }
}

static void publish_peak(uint_least16_t candidate)
{
    uint_least16_t current = atomic_load_explicit(
        &s_observed_absolute_peak, memory_order_relaxed);
    while (candidate > current &&
           !atomic_compare_exchange_weak_explicit(
               &s_observed_absolute_peak, &current, candidate,
               memory_order_relaxed, memory_order_relaxed)) {
    }
}

static void count_invocation(void)
{
    uint_least32_t current = atomic_load_explicit(
        &s_invocation_count, memory_order_relaxed);
    while (current != UINT32_MAX &&
           !atomic_compare_exchange_weak_explicit(
               &s_invocation_count, &current, current + 1U,
               memory_order_relaxed, memory_order_relaxed)) {
    }
}

uint32_t platform_audio_invocation_count(void)
{
    return (uint32_t)atomic_load_explicit(
        &s_invocation_count, memory_order_relaxed);
}

void platform_audio_adapter_get_stats(platform_audio_adapter_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return;
    }
    *out_stats = (platform_audio_adapter_stats_t){0};
    if (!try_backend_call()) {
        return;
    }
    *out_stats = (platform_audio_adapter_stats_t){
        .invocations = (uint32_t)atomic_load_explicit(
            &s_invocation_count, memory_order_relaxed),
        .write_calls_succeeded = (uint32_t)atomic_load_explicit(
            &s_write_calls_succeeded, memory_order_relaxed),
        .frames_forwarded = (uint32_t)atomic_load_explicit(
            &s_frames_forwarded, memory_order_relaxed),
        .nonzero_frames_forwarded = (uint32_t)atomic_load_explicit(
            &s_nonzero_frames_forwarded, memory_order_relaxed),
        .nonzero_samples_forwarded = (uint32_t)atomic_load_explicit(
            &s_nonzero_samples_forwarded, memory_order_relaxed),
        .observed_absolute_peak = (uint16_t)atomic_load_explicit(
            &s_observed_absolute_peak, memory_order_relaxed),
        .running_low_readback_proven_at_start = atomic_load_explicit(
            &s_running_low_readback_proven_at_start, memory_order_relaxed),
        .ready_muted_zero_dma_proven = atomic_load_explicit(
            &s_ready_muted_zero_dma_proven, memory_order_relaxed),
    };
    finish_backend_call();
}

esp_err_t platform_audio_force_safe_shutdown(void)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_invocation();
    const esp_err_t result = platform_audio_factory_force_safe_shutdown();
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_low_readback_proven_at_start, false,
                              memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_recover(void)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_invocation();
    const esp_err_t result = platform_audio_factory_recover();
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_low_readback_proven_at_start, false,
                              memory_order_release);
        atomic_store_explicit(&s_ready_muted_zero_dma_proven, true,
                              memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_create(const platform_audio_config_t *config,
                                platform_audio_t **out_audio)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_invocation();
    if (config == NULL || config->control_bus != NULL) {
        finish_backend_call();
        return ESP_ERR_INVALID_ARG;
    }
    const platform_audio_factory_config_t backend_config = {
        .sample_rate_hz = config->sample_rate_hz,
        .volume_percent = config->volume_percent,
    };
    const esp_err_t result = platform_audio_factory_create(
        &backend_config, out_audio);
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_low_readback_proven_at_start, false,
                              memory_order_release);
        atomic_store_explicit(&s_ready_muted_zero_dma_proven, true,
                              memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_start(platform_audio_t *audio)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_invocation();
    atomic_store_explicit(&s_running_low_readback_proven_at_start, false,
                          memory_order_release);
    atomic_store_explicit(&s_ready_muted_zero_dma_proven, false,
                          memory_order_release);
    const esp_err_t result = platform_audio_factory_start(audio);
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_low_readback_proven_at_start, true,
                              memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *interleaved_pcm,
                                      size_t frame_count)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_invocation();
    if (interleaved_pcm == NULL || frame_count == 0U ||
        frame_count > (size_t)PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
        const esp_err_t result = platform_audio_factory_write_frames(
            audio, interleaved_pcm, frame_count);
        finish_backend_call();
        return result;
    }
    uint32_t nonzero_samples = 0U;
    uint32_t nonzero_frames = 0U;
    uint16_t peak = 0U;
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        bool frame_nonzero = false;
        for (size_t channel = 0U;
             channel < (size_t)PLATFORM_AUDIO_CHANNEL_COUNT; ++channel) {
            const int16_t sample = interleaved_pcm[
                frame * PLATFORM_AUDIO_CHANNEL_COUNT + channel];
            if (sample != 0) {
                frame_nonzero = true;
                ++nonzero_samples;
            }
            const uint16_t magnitude = sample == INT16_MIN
                ? UINT16_C(32768)
                : (uint16_t)(sample < 0 ? -sample : sample);
            if (magnitude > peak) {
                peak = magnitude;
            }
        }
        if (frame_nonzero) {
            ++nonzero_frames;
        }
    }
    const esp_err_t result = platform_audio_factory_write_frames(
        audio, interleaved_pcm, frame_count);
    if (result == ESP_OK) {
        saturating_add(&s_write_calls_succeeded, UINT32_C(1));
        saturating_add(&s_frames_forwarded, (uint_least32_t)frame_count);
        saturating_add(&s_nonzero_frames_forwarded, nonzero_frames);
        saturating_add(&s_nonzero_samples_forwarded, nonzero_samples);
        publish_peak(peak);
    } else {
        atomic_store_explicit(&s_running_low_readback_proven_at_start, false,
                              memory_order_release);
        atomic_store_explicit(&s_ready_muted_zero_dma_proven, false,
                              memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_stop(platform_audio_t *audio)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_invocation();
    const esp_err_t result = platform_audio_factory_stop(audio);
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_low_readback_proven_at_start, false,
                              memory_order_release);
        atomic_store_explicit(&s_ready_muted_zero_dma_proven, true,
                              memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_get_state(const platform_audio_t *audio,
                                   platform_audio_state_t *out_state)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_invocation();
    if (out_state == NULL) {
        finish_backend_call();
        return ESP_ERR_INVALID_ARG;
    }
    platform_audio_factory_state_t backend_state =
        PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE;
    const esp_err_t result =
        platform_audio_factory_get_state(audio, &backend_state);
    if (result != ESP_OK) {
        finish_backend_call();
        return result;
    }
    if (backend_state < PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED ||
        backend_state > PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE) {
        finish_backend_call();
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out_state = (platform_audio_state_t)backend_state;
    if (backend_state != PLATFORM_AUDIO_FACTORY_STATE_RUNNING) {
        atomic_store_explicit(&s_running_low_readback_proven_at_start, false,
                              memory_order_release);
    }
    atomic_store_explicit(
        &s_ready_muted_zero_dma_proven,
        backend_state == PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED,
        memory_order_release);
    finish_backend_call();
    return ESP_OK;
}

esp_err_t platform_audio_get_telemetry(
    platform_audio_telemetry_t *out_telemetry)
{
    /* The backend explicitly guarantees a coherent thread-safe snapshot. */
    return platform_audio_factory_get_telemetry(out_telemetry);
}

esp_err_t platform_audio_destroy(platform_audio_t **audio)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_invocation();
    const esp_err_t result = platform_audio_factory_destroy(audio);
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_low_readback_proven_at_start, false,
                              memory_order_release);
        atomic_store_explicit(&s_ready_muted_zero_dma_proven, true,
                              memory_order_release);
    }
    finish_backend_call();
    return result;
}
