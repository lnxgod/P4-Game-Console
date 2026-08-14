#include "platform/audio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mock_esp32_runtime.h"

static unsigned failures;

#define EXPECT_TRUE(expression_)                                                \
    do {                                                                        \
        if (!(expression_)) {                                                   \
            fprintf(stderr, "%s:%d: expected true: %s\n", __FILE__,           \
                    __LINE__, #expression_);                                    \
            ++failures;                                                        \
        }                                                                       \
    } while (0)

#define EXPECT_EQ(expected_, actual_)                                           \
    do {                                                                        \
        const int64_t expected_value_ = (int64_t)(expected_);                   \
        const int64_t actual_value_ = (int64_t)(actual_);                       \
        if (expected_value_ != actual_value_) {                                 \
            fprintf(stderr, "%s:%d: expected %lld, got %lld: %s\n",          \
                    __FILE__, __LINE__, (long long)expected_value_,             \
                    (long long)actual_value_, #actual_);                        \
            ++failures;                                                        \
        }                                                                       \
    } while (0)

static platform_audio_config_t valid_config(uint8_t volume)
{
    const platform_audio_config_t config = {
        .control_bus = NULL,
        .sample_rate_hz = PLATFORM_AUDIO_SAMPLE_RATE_HZ,
        .volume_percent = volume,
    };
    return config;
}

static bool dma_ring_is_all_zero(void)
{
    for (size_t index = 0U; index < sizeof(g_mock_esp32.dma_ring); ++index) {
        if (g_mock_esp32.dma_ring[index] != 0U) {
            return false;
        }
    }
    return true;
}

static size_t find_event_after(mock_event_t event, size_t begin)
{
    for (size_t index = begin; index < g_mock_esp32.event_count; ++index) {
        if (g_mock_esp32.events[index] == event) {
            return index;
        }
    }
    return SIZE_MAX;
}

static void test_create_argument_policy(void)
{
    platform_audio_t *audio = NULL;
    platform_audio_config_t config = valid_config(UINT8_C(10));
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_audio_create(NULL, &audio));
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_audio_create(&config, NULL));

    platform_audio_t *occupied = (platform_audio_t *)(uintptr_t)1U;
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_audio_create(&config, &occupied));
    config.control_bus = &config;
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_audio_create(&config, &audio));
    config = valid_config(UINT8_C(10));
    config.sample_rate_hz = UINT32_C(48000);
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_audio_create(&config, &audio));
    config = valid_config(UINT8_C(0));
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_audio_create(&config, &audio));
    config = valid_config(UINT8_C(11));
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_audio_create(&config, &audio));
}

static void test_failed_create_retains_and_recovers_driver_owner(void)
{
    mock_esp32_reset();
    platform_audio_t *audio = NULL;
    const platform_audio_config_t config = valid_config(UINT8_C(10));

    g_mock_esp32.i2s_init_result = ESP_FAIL;
    g_mock_esp32.i2s_delete_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_FAIL, platform_audio_create(&config, &audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(g_mock_esp32.channel_allocated);
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    EXPECT_EQ(1, g_mock_esp32.new_calls);

    /* Explicit recovery proves whether callers may release shared rails. */
    g_mock_esp32.i2s_init_result = ESP_OK;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_audio_recover());
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(g_mock_esp32.channel_allocated);
    EXPECT_EQ(1, g_mock_esp32.new_calls);

    /* Once driver deletion recovers, a later create gets a clean owner. */
    g_mock_esp32.i2s_delete_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_recover());
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);
    EXPECT_EQ(ESP_OK, platform_audio_create(&config, &audio));
    EXPECT_TRUE(audio != NULL);
    EXPECT_EQ(2, g_mock_esp32.new_calls);
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    EXPECT_EQ(ESP_OK, platform_audio_destroy(&audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);
}

static void test_each_create_driver_failure_is_safe_and_owned(void)
{
    const platform_audio_config_t config = valid_config(UINT8_C(10));
    platform_audio_t *audio = NULL;

    mock_esp32_reset();
    g_mock_esp32.i2s_new_result = ESP_ERR_NO_MEM;
    EXPECT_EQ(ESP_ERR_NO_MEM, platform_audio_create(&config, &audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);
    EXPECT_EQ(1, g_mock_esp32.amp_level);

    mock_esp32_reset();
    g_mock_esp32.i2s_init_result = ESP_FAIL;
    EXPECT_EQ(ESP_FAIL, platform_audio_create(&config, &audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);
    EXPECT_EQ(1, g_mock_esp32.amp_level);

    mock_esp32_reset();
    g_mock_esp32.i2s_enable_result = ESP_FAIL;
    EXPECT_EQ(ESP_FAIL, platform_audio_create(&config, &audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);
    EXPECT_EQ(1, g_mock_esp32.amp_level);

    mock_esp32_reset();
    g_mock_esp32.i2s_preload_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_audio_create(&config, &audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);
    EXPECT_EQ(1, g_mock_esp32.amp_level);

    mock_esp32_reset();
    g_mock_esp32.force_partial_preload = true;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_audio_create(&config, &audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);
    EXPECT_EQ(1, g_mock_esp32.amp_level);
}

static void test_start_preroll_failure_remains_muted(void)
{
    mock_esp32_reset();
    platform_audio_t *audio = NULL;
    const platform_audio_config_t config = valid_config(UINT8_C(10));
    EXPECT_EQ(ESP_OK, platform_audio_create(&config, &audio));
    g_mock_esp32.i2s_preload_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_audio_start(audio));
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    platform_audio_state_t state = PLATFORM_AUDIO_STATE_RUNNING;
    EXPECT_EQ(ESP_OK, platform_audio_get_state(audio, &state));
    EXPECT_EQ(PLATFORM_AUDIO_STATE_READY_MUTED, state);
    g_mock_esp32.i2s_preload_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_destroy(&audio));
}

static void test_direct_i2s_lifecycle_and_attenuation(void)
{
    mock_esp32_reset();
    platform_audio_t *audio = NULL;
    const platform_audio_config_t config = valid_config(UINT8_C(10));
    EXPECT_EQ(ESP_OK, platform_audio_create(&config, &audio));
    EXPECT_TRUE(audio != NULL);
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    EXPECT_EQ(1, g_mock_esp32.channel_config.controller_id);
    EXPECT_EQ(6, g_mock_esp32.channel_config.dma_desc_num);
    EXPECT_EQ(256, g_mock_esp32.channel_config.dma_frame_num);
    EXPECT_TRUE(g_mock_esp32.channel_config.auto_clear);
    EXPECT_EQ(16000, g_mock_esp32.standard_config.clk_cfg.sample_rate_hz);
    EXPECT_EQ(-1, g_mock_esp32.standard_config.gpio_cfg.mclk);
    EXPECT_EQ(22, g_mock_esp32.standard_config.gpio_cfg.bclk);
    EXPECT_EQ(21, g_mock_esp32.standard_config.gpio_cfg.ws);
    EXPECT_EQ(23, g_mock_esp32.standard_config.gpio_cfg.dout);
    EXPECT_EQ(0, g_mock_esp32.write_calls);
    EXPECT_EQ(12, g_mock_esp32.preload_calls);
    EXPECT_TRUE(dma_ring_is_all_zero());
    EXPECT_EQ(sizeof(g_mock_esp32.dma_ring),
              g_mock_esp32.dma_preload_position);
    EXPECT_TRUE(g_mock_esp32.event_count >= 22U);
    EXPECT_EQ(MOCK_EVENT_GPIO_HIGH, g_mock_esp32.events[0]);
    EXPECT_EQ(MOCK_EVENT_GPIO_CONFIG, g_mock_esp32.events[1]);
    EXPECT_EQ(MOCK_EVENT_GPIO_HIGH, g_mock_esp32.events[2]);
    EXPECT_EQ(MOCK_EVENT_I2S_NEW, g_mock_esp32.events[3]);
    EXPECT_EQ(MOCK_EVENT_I2S_INIT, g_mock_esp32.events[4]);
    const size_t create_safe = find_event_after(MOCK_EVENT_GPIO_HIGH, 5U);
    const size_t create_enable =
        find_event_after(MOCK_EVENT_I2S_ENABLE, create_safe + 1U);
    const size_t create_disable =
        find_event_after(MOCK_EVENT_I2S_DISABLE, create_enable + 1U);
    const size_t create_preload =
        find_event_after(MOCK_EVENT_I2S_PRELOAD, create_disable + 1U);
    EXPECT_TRUE(create_safe < create_enable);
    EXPECT_TRUE(create_enable < create_disable);
    EXPECT_TRUE(create_disable < create_preload);

    platform_audio_state_t state = PLATFORM_AUDIO_STATE_RUNNING;
    EXPECT_EQ(ESP_OK, platform_audio_get_state(audio, &state));
    EXPECT_EQ(PLATFORM_AUDIO_STATE_READY_MUTED, state);

    EXPECT_EQ(ESP_OK, platform_audio_start(audio));
    EXPECT_EQ(0, g_mock_esp32.amp_level);
    EXPECT_EQ(20, g_mock_esp32.delay_ticks);
    EXPECT_EQ(0, g_mock_esp32.write_calls);
    EXPECT_EQ(24, g_mock_esp32.preload_calls);
    EXPECT_TRUE(dma_ring_is_all_zero());
    EXPECT_EQ(ESP_OK, platform_audio_get_state(audio, &state));
    EXPECT_EQ(PLATFORM_AUDIO_STATE_RUNNING, state);
    EXPECT_EQ(ESP_ERR_INVALID_STATE, platform_audio_start(audio));

    platform_audio_t *second = NULL;
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_audio_create(&config, &second));
    EXPECT_TRUE(second == NULL);
    EXPECT_EQ(0, g_mock_esp32.amp_level);
    EXPECT_EQ(ESP_ERR_INVALID_STATE, platform_audio_recover());
    EXPECT_EQ(0, g_mock_esp32.amp_level);

    int16_t input[PLATFORM_AUDIO_MAX_WRITE_FRAMES *
                  PLATFORM_AUDIO_CHANNEL_COUNT];
    int16_t original[PLATFORM_AUDIO_MAX_WRITE_FRAMES *
                     PLATFORM_AUDIO_CHANNEL_COUNT];
    for (size_t index = 0U;
         index < (sizeof(input) / sizeof(input[0])); ++index) {
        input[index] = (index & 1U) == 0U ? INT16_MIN : INT16_MAX;
    }
    memcpy(original, input, sizeof(input));
    EXPECT_EQ(ESP_OK, platform_audio_write_frames(
        audio, input, PLATFORM_AUDIO_MAX_WRITE_FRAMES
    ));
    EXPECT_TRUE(memcmp(input, original, sizeof(input)) == 0);
    EXPECT_EQ(1, g_mock_esp32.write_calls);
    EXPECT_EQ(512, g_mock_esp32.write_sizes[0]);
    EXPECT_EQ(100, g_mock_esp32.write_timeouts[0]);
    const int16_t *sent = (const int16_t *)g_mock_esp32.write_data[0];
    for (size_t index = 0U;
         index < (sizeof(input) / sizeof(input[0])); ++index) {
        EXPECT_EQ((index & 1U) == 0U ? -512 : 512, sent[index]);
    }
    EXPECT_TRUE(!dma_ring_is_all_zero());

    EXPECT_EQ(ESP_ERR_INVALID_SIZE, platform_audio_write_frames(
        audio, input, PLATFORM_AUDIO_MAX_WRITE_FRAMES + 1U
    ));
    EXPECT_EQ(1, g_mock_esp32.write_calls);

    /* Stop after nonzero output overwrites every descriptor before restart. */
    EXPECT_EQ(ESP_OK, platform_audio_stop(audio));
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    EXPECT_TRUE(dma_ring_is_all_zero());
    EXPECT_EQ(36, g_mock_esp32.preload_calls);
    EXPECT_EQ(ESP_OK, platform_audio_start(audio));
    EXPECT_EQ(0, g_mock_esp32.amp_level);
    EXPECT_TRUE(dma_ring_is_all_zero());

    /* Partial output is rejected and the complete ring is re-primed. */
    g_mock_esp32.force_partial_write = true;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_audio_write_frames(audio, input, 1U));
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    EXPECT_TRUE(dma_ring_is_all_zero());
    EXPECT_EQ(ESP_OK, platform_audio_get_state(audio, &state));
    EXPECT_EQ(PLATFORM_AUDIO_STATE_READY_MUTED, state);
    g_mock_esp32.force_partial_write = false;

    /* A timeout after prior nonzero data is also a full-ring mute boundary. */
    EXPECT_EQ(ESP_OK, platform_audio_start(audio));
    EXPECT_EQ(ESP_OK, platform_audio_write_frames(audio, input, 1U));
    EXPECT_TRUE(!dma_ring_is_all_zero());
    g_mock_esp32.i2s_write_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT,
              platform_audio_write_frames(audio, input, 1U));
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    EXPECT_TRUE(dma_ring_is_all_zero());
    EXPECT_EQ(ESP_OK, platform_audio_get_state(audio, &state));
    EXPECT_EQ(PLATFORM_AUDIO_STATE_READY_MUTED, state);
    g_mock_esp32.i2s_write_result = ESP_OK;

    const size_t destroy_begin = g_mock_esp32.event_count;
    EXPECT_EQ(ESP_OK, platform_audio_destroy(&audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(g_mock_esp32.event_count >= destroy_begin + 5U);
    EXPECT_EQ(MOCK_EVENT_GPIO_HIGH, g_mock_esp32.events[destroy_begin]);
    EXPECT_EQ(MOCK_EVENT_GPIO_CONFIG, g_mock_esp32.events[destroy_begin + 1U]);
    EXPECT_EQ(MOCK_EVENT_GPIO_HIGH, g_mock_esp32.events[destroy_begin + 2U]);
    EXPECT_EQ(MOCK_EVENT_I2S_DISABLE, g_mock_esp32.events[destroy_begin + 3U]);
    EXPECT_EQ(MOCK_EVENT_I2S_DELETE, g_mock_esp32.events[destroy_begin + 4U]);
}

static void test_destroy_retains_handle_until_cleanup_succeeds(void)
{
    mock_esp32_reset();
    platform_audio_t *audio = NULL;
    const platform_audio_config_t config = valid_config(UINT8_C(5));
    EXPECT_EQ(ESP_OK, platform_audio_create(&config, &audio));
    EXPECT_EQ(ESP_OK, platform_audio_start(audio));

    g_mock_esp32.i2s_disable_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_audio_destroy(&audio));
    EXPECT_TRUE(audio != NULL);
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    EXPECT_TRUE(g_mock_esp32.channel_allocated);

    g_mock_esp32.i2s_disable_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_destroy(&audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);

    mock_esp32_reset();
    EXPECT_EQ(ESP_OK, platform_audio_create(&config, &audio));
    g_mock_esp32.i2s_delete_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_audio_destroy(&audio));
    EXPECT_TRUE(audio != NULL);
    EXPECT_EQ(1, g_mock_esp32.amp_level);
    EXPECT_TRUE(g_mock_esp32.channel_allocated);
    EXPECT_TRUE(!g_mock_esp32.channel_enabled);
    g_mock_esp32.i2s_delete_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_destroy(&audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_esp32.channel_allocated);
}

int main(void)
{
    mock_esp32_reset();
    test_create_argument_policy();
    test_failed_create_retains_and_recovers_driver_owner();
    test_each_create_driver_failure_is_safe_and_owned();
    test_start_preroll_failure_remains_muted();
    test_direct_i2s_lifecycle_and_attenuation();
    test_destroy_retains_handle_until_cleanup_succeeds();
    if (failures != 0U) {
        fprintf(stderr, "platform audio runtime tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_AUDIO RUNTIME HOST PASS direct_i2s=1 rollback_owner_retained=1 amp_high_first=1 dma_ring_zero_frames=1536 restart_stale_pcm=0 exact_bytes=1 finite_timeout_ms=100 immutable_staging=1");
    return 0;
}
