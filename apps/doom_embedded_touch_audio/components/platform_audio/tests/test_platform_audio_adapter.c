#include "platform/audio.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

struct platform_audio_factory {
    platform_audio_factory_state_t state;
};

static platform_audio_factory_config_t s_create_config;
static unsigned s_calls;
static int16_t s_last_write[4];
static size_t s_last_write_frames;
static bool s_try_reentrant_safe_call;
static esp_err_t s_reentrant_safe_result = ESP_OK;
static platform_audio_adapter_stats_t s_reentrant_stats;
static unsigned s_force_safe_backend_calls;
static esp_err_t s_write_result = ESP_OK;
static struct platform_audio_factory s_instance;
static platform_audio_factory_telemetry_t s_telemetry;

esp_err_t platform_audio_factory_force_safe_shutdown(void)
{
    ++s_calls;
    ++s_force_safe_backend_calls;
    return ESP_OK;
}

esp_err_t platform_audio_factory_recover(void)
{
    ++s_calls;
    return ESP_OK;
}

esp_err_t platform_audio_factory_create(
    const platform_audio_factory_config_t *config,
    platform_audio_factory_t **out_audio)
{
    ++s_calls;
    s_create_config = *config;
    s_instance.state = PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED;
    *out_audio = &s_instance;
    return ESP_OK;
}

esp_err_t platform_audio_factory_start(platform_audio_factory_t *audio)
{
    ++s_calls;
    audio->state = PLATFORM_AUDIO_FACTORY_STATE_RUNNING;
    return ESP_OK;
}

esp_err_t platform_audio_factory_write_frames(
    platform_audio_factory_t *audio,
    const int16_t *interleaved_pcm,
    size_t frame_count)
{
    ++s_calls;
    if (audio == NULL || interleaved_pcm == NULL || frame_count == 0U ||
        frame_count > 2U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_try_reentrant_safe_call) {
        s_reentrant_safe_result = platform_audio_force_safe_shutdown();
        platform_audio_adapter_get_stats(&s_reentrant_stats);
    }
    if (s_write_result != ESP_OK) {
        return s_write_result;
    }
    s_last_write_frames = frame_count;
    for (size_t index = 0U; index < frame_count * 2U; ++index) {
        s_last_write[index] = interleaved_pcm[index];
    }
    return ESP_OK;
}

esp_err_t platform_audio_factory_stop(platform_audio_factory_t *audio)
{
    ++s_calls;
    audio->state = PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED;
    return ESP_OK;
}

esp_err_t platform_audio_factory_get_state(
    const platform_audio_factory_t *audio,
    platform_audio_factory_state_t *out_state)
{
    ++s_calls;
    *out_state = audio->state;
    return ESP_OK;
}

esp_err_t platform_audio_factory_destroy(platform_audio_factory_t **audio)
{
    ++s_calls;
    *audio = NULL;
    return ESP_OK;
}

esp_err_t platform_audio_factory_get_telemetry(
    platform_audio_factory_telemetry_t *out_telemetry)
{
    *out_telemetry = s_telemetry;
    return ESP_OK;
}

int main(void)
{
    assert(platform_audio_invocation_count() == 0U);
    platform_audio_t *audio = NULL;
    platform_audio_config_t config = {
        .control_bus = (void *)(uintptr_t)1U,
        .sample_rate_hz = PLATFORM_AUDIO_SAMPLE_RATE_HZ,
        .volume_percent = 10U,
    };
    assert(platform_audio_create(&config, &audio) == ESP_ERR_INVALID_ARG);
    assert(platform_audio_invocation_count() == 1U);
    assert(s_calls == 0U);
    config.control_bus = NULL;
    assert(platform_audio_create(&config, &audio) == ESP_OK);
    assert(platform_audio_invocation_count() == 2U);
    assert(s_create_config.sample_rate_hz == PLATFORM_AUDIO_SAMPLE_RATE_HZ);
    assert(s_create_config.volume_percent == 10U);

    platform_audio_state_t state = PLATFORM_AUDIO_STATE_FAILED_SAFE;
    assert(platform_audio_get_state(audio, &state) == ESP_OK);
    assert(state == PLATFORM_AUDIO_STATE_READY_MUTED);
    assert(platform_audio_start(audio) == ESP_OK);
    platform_audio_adapter_stats_t stats = {0};
    platform_audio_adapter_get_stats(&stats);
    assert(stats.running_low_readback_proven_at_start);
    assert(!stats.ready_muted_zero_dma_proven);
    s_telemetry.snapshot_sequence = 2U;
    s_telemetry.zero_preload_frames = 1536U;
    platform_audio_telemetry_t telemetry = {0};
    assert(platform_audio_get_telemetry(&telemetry) == ESP_OK);
    assert(telemetry.snapshot_sequence == 2U);
    assert(telemetry.zero_preload_frames == 1536U);
    assert(platform_audio_get_state(audio, &state) == ESP_OK);
    assert(state == PLATFORM_AUDIO_STATE_RUNNING);
    const int16_t frame[2] = {INT16_MIN, INT16_MAX};
    s_try_reentrant_safe_call = true;
    assert(platform_audio_write_frames(audio, frame, 1U) == ESP_OK);
    s_try_reentrant_safe_call = false;
    assert(s_reentrant_safe_result == ESP_ERR_TIMEOUT);
    assert(s_reentrant_stats.invocations == 0U);
    assert(s_reentrant_stats.write_calls_succeeded == 0U);
    assert(s_force_safe_backend_calls == 0U);
    assert(s_last_write_frames == 1U);
    assert(s_last_write[0] == INT16_MIN);
    assert(s_last_write[1] == INT16_MAX);
    platform_audio_adapter_get_stats(&stats);
    assert(stats.write_calls_succeeded == 1U);
    assert(stats.frames_forwarded == 1U);
    assert(stats.nonzero_frames_forwarded == 1U);
    assert(stats.nonzero_samples_forwarded == 2U);
    assert(stats.observed_absolute_peak == UINT16_C(32768));
    assert(stats.running_low_readback_proven_at_start);
    s_write_result = ESP_FAIL;
    assert(platform_audio_write_frames(audio, frame, 1U) == ESP_FAIL);
    platform_audio_adapter_get_stats(&stats);
    assert(!stats.running_low_readback_proven_at_start);
    assert(!stats.ready_muted_zero_dma_proven);
    s_write_result = ESP_OK;
    assert(platform_audio_start(audio) == ESP_OK);
    platform_audio_adapter_get_stats(&stats);
    assert(stats.running_low_readback_proven_at_start);
    assert(platform_audio_force_safe_shutdown() == ESP_OK);
    assert(s_force_safe_backend_calls == 1U);
    assert(platform_audio_get_state(audio, &state) == ESP_OK);
    assert(state == PLATFORM_AUDIO_STATE_RUNNING);
    platform_audio_adapter_get_stats(&stats);
    assert(!stats.running_low_readback_proven_at_start);
    assert(platform_audio_stop(audio) == ESP_OK);
    platform_audio_adapter_get_stats(&stats);
    assert(!stats.running_low_readback_proven_at_start);
    assert(stats.ready_muted_zero_dma_proven);
    assert(platform_audio_force_safe_shutdown() == ESP_OK);
    assert(s_force_safe_backend_calls == 2U);
    assert(platform_audio_destroy(&audio) == ESP_OK);
    assert(audio == NULL);
    assert(platform_audio_recover() == ESP_OK);
    assert(platform_audio_get_state(NULL, NULL) == ESP_ERR_INVALID_ARG);
    /* The rejected reentrant safety call never enters the counted seam. */
    assert(platform_audio_invocation_count() == 15U);
    return 0;
}
