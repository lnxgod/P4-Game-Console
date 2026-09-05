// SPDX-License-Identifier: GPL-2.0-or-later

#include "platform/audio.h"

#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static atomic_flag s_backend_call_active = ATOMIC_FLAG_INIT;
static atomic_uint_least32_t s_invocations;
static atomic_uint_least32_t s_write_calls;
static atomic_uint_least32_t s_frames;
static atomic_uint_least32_t s_nonzero_frames;
static atomic_uint_least32_t s_nonzero_samples;
static atomic_uint_least16_t s_peak;
static atomic_bool s_running_proven;
static atomic_bool s_ready_muted_proven;
static atomic_uint_least32_t s_snapshot_sequence;

static bool try_backend_call(void)
{
    return !atomic_flag_test_and_set_explicit(
        &s_backend_call_active, memory_order_acquire);
}

static void finish_backend_call(void)
{
    atomic_flag_clear_explicit(&s_backend_call_active, memory_order_release);
}

static void add_u32(atomic_uint_least32_t *value, uint32_t amount)
{
    uint_least32_t current = atomic_load_explicit(value, memory_order_relaxed);
    while (current != UINT32_MAX) {
        const uint_least32_t remaining = UINT32_MAX - current;
        const uint_least32_t next = current +
            (amount > remaining ? remaining : amount);
        if (atomic_compare_exchange_weak_explicit(
                value, &current, next, memory_order_relaxed,
                memory_order_relaxed)) {
            return;
        }
    }
}

static void publish_peak(uint16_t candidate)
{
    uint_least16_t current = atomic_load_explicit(&s_peak, memory_order_relaxed);
    while (candidate > current &&
           !atomic_compare_exchange_weak_explicit(
               &s_peak, &current, candidate, memory_order_relaxed,
               memory_order_relaxed)) {
    }
}

static void count_call(void)
{
    add_u32(&s_invocations, 1U);
}

uint32_t platform_audio_invocation_count(void)
{
    return (uint32_t)atomic_load_explicit(&s_invocations, memory_order_relaxed);
}

void platform_audio_adapter_get_stats(platform_audio_adapter_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = (platform_audio_adapter_stats_t){
        .invocations = platform_audio_invocation_count(),
        .write_calls_succeeded = (uint32_t)atomic_load_explicit(
            &s_write_calls, memory_order_relaxed),
        .frames_forwarded = (uint32_t)atomic_load_explicit(
            &s_frames, memory_order_relaxed),
        .nonzero_frames_forwarded = (uint32_t)atomic_load_explicit(
            &s_nonzero_frames, memory_order_relaxed),
        .nonzero_samples_forwarded = (uint32_t)atomic_load_explicit(
            &s_nonzero_samples, memory_order_relaxed),
        .observed_absolute_peak = (uint16_t)atomic_load_explicit(
            &s_peak, memory_order_relaxed),
        .running_low_readback_proven_at_start = atomic_load_explicit(
            &s_running_proven, memory_order_acquire),
        .ready_muted_zero_dma_proven = atomic_load_explicit(
            &s_ready_muted_proven, memory_order_acquire),
    };
}

esp_err_t platform_audio_force_safe_shutdown(void)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_call();
    const esp_err_t result = platform_audio_es8311_force_safe_shutdown();
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_proven, false, memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_recover(void)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_call();
    const esp_err_t result = platform_audio_es8311_recover();
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_proven, false, memory_order_release);
        atomic_store_explicit(&s_ready_muted_proven, true, memory_order_release);
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
    count_call();
    if (config == NULL || config->control_bus == NULL) {
        finish_backend_call();
        return ESP_ERR_INVALID_ARG;
    }
    const platform_audio_es8311_config_t backend_config = {
        .control_bus = config->control_bus,
        .sample_rate_hz = config->sample_rate_hz,
        .volume_percent = config->volume_percent,
    };
    const esp_err_t result = platform_audio_es8311_create(
        &backend_config, out_audio);
    if (result == ESP_OK) {
        atomic_store_explicit(&s_ready_muted_proven, true, memory_order_release);
        atomic_store_explicit(&s_running_proven, false, memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_start(platform_audio_t *audio)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_call();
    atomic_store_explicit(&s_running_proven, false, memory_order_release);
    atomic_store_explicit(&s_ready_muted_proven, false, memory_order_release);
    const esp_err_t result = platform_audio_es8311_start(audio);
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_proven, true, memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *pcm,
                                      size_t frame_count)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_call();
    if (pcm == NULL || frame_count == 0U ||
        frame_count > (size_t)PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
        finish_backend_call();
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t nonzero_frames = 0U;
    uint32_t nonzero_samples = 0U;
    uint16_t peak = 0U;
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        bool nonzero = false;
        for (size_t channel = 0U; channel < PLATFORM_AUDIO_CHANNEL_COUNT;
             ++channel) {
            const int16_t sample = pcm[frame * PLATFORM_AUDIO_CHANNEL_COUNT + channel];
            const uint16_t magnitude = sample == INT16_MIN
                ? UINT16_C(32768)
                : (uint16_t)(sample < 0 ? -sample : sample);
            if (sample != 0) {
                nonzero = true;
                ++nonzero_samples;
            }
            if (magnitude > peak) {
                peak = magnitude;
            }
        }
        if (nonzero) {
            ++nonzero_frames;
        }
    }
    const esp_err_t result = platform_audio_es8311_write_frames(
        audio, pcm, frame_count);
    if (result == ESP_OK) {
        add_u32(&s_write_calls, 1U);
        add_u32(&s_frames, (uint32_t)frame_count);
        add_u32(&s_nonzero_frames, nonzero_frames);
        add_u32(&s_nonzero_samples, nonzero_samples);
        publish_peak(peak);
    } else {
        atomic_store_explicit(&s_running_proven, false, memory_order_release);
        atomic_store_explicit(&s_ready_muted_proven, false, memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_set_volume(platform_audio_t *audio,
                                     uint8_t volume_step)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_call();
    const esp_err_t result = platform_audio_es8311_set_volume(
        audio, volume_step);
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_get_volume(const platform_audio_t *audio,
                                    uint8_t *out_volume_step)
{
    return platform_audio_es8311_get_volume(audio, out_volume_step);
}

esp_err_t platform_audio_stop(platform_audio_t *audio)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_call();
    const esp_err_t result = platform_audio_es8311_stop(audio);
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_proven, false, memory_order_release);
        atomic_store_explicit(&s_ready_muted_proven, true, memory_order_release);
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_get_state(const platform_audio_t *audio,
                                   platform_audio_state_t *out_state)
{
    if (out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_call();
    platform_audio_es8311_state_t state;
    const esp_err_t result = platform_audio_es8311_get_state(audio, &state);
    if (result == ESP_OK) {
        *out_state = (platform_audio_state_t)state;
    }
    finish_backend_call();
    return result;
}

esp_err_t platform_audio_get_telemetry(platform_audio_telemetry_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (platform_audio_telemetry_t){0};
    out->snapshot_sequence = (uint32_t)atomic_fetch_add_explicit(
        &s_snapshot_sequence, 1U, memory_order_relaxed) + 1U;
    out->running = atomic_load_explicit(&s_running_proven, memory_order_acquire);
    out->state = out->running
        ? PLATFORM_AUDIO_FACTORY_STATE_RUNNING
        : PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED;
    return ESP_OK;
}

esp_err_t platform_audio_destroy(platform_audio_t **audio)
{
    if (!try_backend_call()) {
        return ESP_ERR_TIMEOUT;
    }
    count_call();
    const esp_err_t result = platform_audio_es8311_destroy(audio);
    if (result == ESP_OK) {
        atomic_store_explicit(&s_running_proven, false, memory_order_release);
        atomic_store_explicit(&s_ready_muted_proven, true, memory_order_release);
    }
    finish_backend_call();
    return result;
}
