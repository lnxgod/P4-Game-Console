#include "platform_audio_factory/audio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

#include "mock_factory_runtime.h"

static unsigned failures;

#define EXPECT_TRUE(expression_)                                                \
    do {                                                                        \
        if (!(expression_)) {                                                   \
            fprintf(stderr, "%s:%d: expected true: %s\n", __FILE__,          \
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

static platform_audio_factory_config_t valid_config(uint8_t volume)
{
    const platform_audio_factory_config_t configuration = {
        .sample_rate_hz = PLATFORM_AUDIO_FACTORY_SAMPLE_RATE_HZ,
        .volume_percent = volume,
    };
    return configuration;
}

static size_t find_event_after(mock_factory_event_t event, size_t begin)
{
    for (size_t index = begin; index < g_mock_factory.event_count; ++index) {
        if (g_mock_factory.events[index] == event) {
            return index;
        }
    }
    return SIZE_MAX;
}

static platform_audio_factory_state_t state_of(
    platform_audio_factory_t *audio)
{
    platform_audio_factory_state_t state =
        PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE;
    EXPECT_EQ(ESP_OK, platform_audio_factory_get_state(audio, &state));
    return state;
}

static platform_audio_factory_telemetry_t telemetry(void)
{
    platform_audio_factory_telemetry_t result;
    memset(&result, 0, sizeof(result));
    EXPECT_EQ(ESP_OK, platform_audio_factory_get_telemetry(&result));
    return result;
}

static void test_safe_gpio_latch_config_rewrite_and_readback(void)
{
    mock_factory_reset();
    EXPECT_EQ(ESP_OK, platform_audio_factory_force_safe_shutdown());
    EXPECT_TRUE(g_mock_factory.gpio_configured);
    EXPECT_EQ(GPIO_MODE_INPUT_OUTPUT,
              g_mock_factory.gpio_configuration.mode);
    EXPECT_EQ(UINT64_C(1) << 30,
              g_mock_factory.gpio_configuration.pin_bit_mask);
    EXPECT_EQ(1, g_mock_factory.amp_level);
    EXPECT_EQ(4, g_mock_factory.event_count);
    EXPECT_EQ(MOCK_FACTORY_EVENT_GPIO_HIGH, g_mock_factory.events[0]);
    EXPECT_EQ(MOCK_FACTORY_EVENT_GPIO_CONFIG, g_mock_factory.events[1]);
    EXPECT_EQ(MOCK_FACTORY_EVENT_GPIO_HIGH, g_mock_factory.events[2]);
    EXPECT_EQ(MOCK_FACTORY_EVENT_GPIO_GET_HIGH,
              g_mock_factory.events[3]);

    mock_factory_reset();
    g_mock_factory.force_high_readback_low = true;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_audio_factory_force_safe_shutdown());
    EXPECT_EQ(MOCK_FACTORY_EVENT_GPIO_GET_LOW,
              g_mock_factory.events[g_mock_factory.event_count - 1U]);
}

static void test_argument_and_high_readback_gates(void)
{
    mock_factory_reset();
    platform_audio_factory_t *audio = NULL;
    platform_audio_factory_config_t configuration =
        valid_config(UINT8_C(10));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_audio_factory_create(NULL, &audio));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_audio_factory_create(&configuration, NULL));
    platform_audio_factory_t *occupied =
        (platform_audio_factory_t *)(uintptr_t)1U;
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_audio_factory_create(&configuration, &occupied));
    configuration.sample_rate_hz = UINT32_C(48000);
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_audio_factory_create(&configuration, &audio));
    configuration = valid_config(UINT8_C(11));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_audio_factory_create(&configuration, &audio));
    configuration = valid_config(UINT8_C(12));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_audio_factory_create(&configuration, &audio));

    configuration = valid_config(UINT8_C(10));
    g_mock_factory.force_high_readback_low = true;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_audio_factory_create(&configuration, &audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_EQ(0, g_mock_factory.tx_new_calls);
    g_mock_factory.force_high_readback_low = false;
    EXPECT_EQ(ESP_OK, platform_audio_factory_recover());
    const platform_audio_factory_telemetry_t recovered = telemetry();
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED, recovered.state);
    EXPECT_TRUE(!recovered.resources_retained);
    EXPECT_EQ(0, recovered.resources_owned);
}

static void test_factory_lifecycle_zero_settle_and_attenuation(void)
{
    mock_factory_reset();
    platform_audio_factory_t *audio = NULL;
    const platform_audio_factory_config_t configuration =
        valid_config(UINT8_C(10));
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));
    EXPECT_TRUE(audio != NULL);
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED,
              state_of(audio));
    EXPECT_EQ(1, g_mock_factory.amp_level);
    EXPECT_EQ(0, g_mock_factory.pdm_channel_configuration.id);
    EXPECT_EQ(6, g_mock_factory.pdm_channel_configuration.dma_desc_num);
    EXPECT_EQ(256, g_mock_factory.pdm_channel_configuration.dma_frame_num);
    EXPECT_TRUE(g_mock_factory.pdm_channel_configuration.auto_clear_after_cb);
    EXPECT_TRUE(g_mock_factory.pdm_channel_configuration.auto_clear_before_cb);
    EXPECT_EQ(16000, g_mock_factory.pdm_configuration.clk_cfg.sample_rate_hz);
    EXPECT_EQ(I2S_PDM_DSR_8S,
              g_mock_factory.pdm_configuration.clk_cfg.dn_sample_mode);
    EXPECT_EQ(8, g_mock_factory.pdm_configuration.clk_cfg.bclk_div);
    EXPECT_EQ(I2S_SLOT_MODE_MONO,
              g_mock_factory.pdm_configuration.slot_cfg.slot_mode);
    EXPECT_EQ(I2S_PDM_SLOT_LEFT,
              g_mock_factory.pdm_configuration.slot_cfg.slot_mask);
    EXPECT_EQ(24, g_mock_factory.pdm_configuration.gpio_cfg.clk);
    EXPECT_EQ(26, g_mock_factory.pdm_configuration.gpio_cfg.din);
    EXPECT_TRUE(g_mock_factory.pdm_enabled);
    EXPECT_EQ(1, g_mock_factory.tx_channel_configuration.id);
    EXPECT_EQ(6, g_mock_factory.tx_channel_configuration.dma_desc_num);
    EXPECT_EQ(256, g_mock_factory.tx_channel_configuration.dma_frame_num);
    EXPECT_TRUE(g_mock_factory.tx_channel_configuration.auto_clear);
    EXPECT_EQ(16000,
              g_mock_factory.tx_configuration.clk_cfg.sample_rate_hz);
    EXPECT_EQ(-1, g_mock_factory.tx_configuration.gpio_cfg.mclk);
    EXPECT_EQ(22, g_mock_factory.tx_configuration.gpio_cfg.bclk);
    EXPECT_EQ(21, g_mock_factory.tx_configuration.gpio_cfg.ws);
    EXPECT_EQ(23, g_mock_factory.tx_configuration.gpio_cfg.dout);
    EXPECT_EQ(12, g_mock_factory.preload_calls);
    EXPECT_TRUE(mock_factory_dma_ring_is_zero());

    const size_t first_new =
        find_event_after(MOCK_FACTORY_EVENT_PDM_NEW, 0U);
    const size_t first_safe_read =
        find_event_after(MOCK_FACTORY_EVENT_GPIO_GET_HIGH, 0U);
    EXPECT_TRUE(first_safe_read < first_new);
    const size_t pdm_init =
        find_event_after(MOCK_FACTORY_EVENT_PDM_INIT, first_new + 1U);
    const size_t pdm_enable =
        find_event_after(MOCK_FACTORY_EVENT_PDM_ENABLE, pdm_init + 1U);
    const size_t tx_new =
        find_event_after(MOCK_FACTORY_EVENT_TX_NEW, pdm_enable + 1U);
    EXPECT_TRUE(first_new < pdm_init);
    EXPECT_TRUE(pdm_init < pdm_enable);
    EXPECT_TRUE(pdm_enable < tx_new);

    const size_t start_begin = g_mock_factory.event_count;
    EXPECT_EQ(ESP_OK, platform_audio_factory_start(audio));
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_RUNNING, state_of(audio));
    EXPECT_EQ(0, g_mock_factory.amp_level);
    EXPECT_EQ(351, g_mock_factory.delay_ticks);
    EXPECT_TRUE(g_mock_factory.low_saw_tx_enabled);
    EXPECT_TRUE(g_mock_factory.low_saw_pdm_enabled);
    EXPECT_TRUE(g_mock_factory.low_saw_zero_ring);
    EXPECT_TRUE(g_mock_factory.delay_saw_amp_enabled);
    EXPECT_TRUE(g_mock_factory.delay_saw_tx_enabled);
    EXPECT_TRUE(g_mock_factory.delay_saw_pdm_enabled);
    EXPECT_TRUE(g_mock_factory.delay_saw_zero_ring);

    const size_t start_safe =
        find_event_after(MOCK_FACTORY_EVENT_GPIO_HIGH, start_begin);
    const size_t start_safe_read = find_event_after(
        MOCK_FACTORY_EVENT_GPIO_GET_HIGH, start_safe + 1U
    );
    const size_t start_disable = find_event_after(
        MOCK_FACTORY_EVENT_TX_DISABLE, start_safe_read + 1U
    );
    const size_t start_preload = find_event_after(
        MOCK_FACTORY_EVENT_TX_PRELOAD, start_disable + 1U
    );
    const size_t start_enable = find_event_after(
        MOCK_FACTORY_EVENT_TX_ENABLE, start_preload + 1U
    );
    const size_t start_low = find_event_after(
        MOCK_FACTORY_EVENT_GPIO_LOW, start_enable + 1U
    );
    const size_t start_low_read = find_event_after(
        MOCK_FACTORY_EVENT_GPIO_GET_LOW, start_low + 1U
    );
    const size_t start_delay = find_event_after(
        MOCK_FACTORY_EVENT_DELAY, start_low_read + 1U
    );
    const size_t post_settle_low_read = find_event_after(
        MOCK_FACTORY_EVENT_GPIO_GET_LOW, start_delay + 1U
    );
    EXPECT_TRUE(start_safe < start_safe_read);
    EXPECT_TRUE(start_safe_read < start_disable);
    EXPECT_TRUE(start_disable < start_preload);
    EXPECT_TRUE(start_preload < start_enable);
    EXPECT_TRUE(start_enable < start_low);
    EXPECT_TRUE(start_low < start_low_read);
    EXPECT_TRUE(start_low_read < start_delay);
    EXPECT_TRUE(start_delay < post_settle_low_read);
    platform_audio_factory_telemetry_t running = telemetry();
    EXPECT_TRUE(running.pdm_created);
    EXPECT_TRUE(running.pdm_enabled);
    EXPECT_TRUE(running.tx_created);
    EXPECT_TRUE(running.tx_enabled);
    EXPECT_EQ(1536, running.zero_preload_frames);
    EXPECT_EQ(1, running.gpio30_low_attempts);
    EXPECT_EQ(1, running.gpio30_low_successes);
    EXPECT_EQ(1, running.gpio30_low_initial_readback_successes);
    EXPECT_TRUE(running.measured_settle_us >= UINT32_C(350000));
    EXPECT_EQ(1, running.gpio30_low_second_readback_successes);
    EXPECT_TRUE(running.running);
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_audio_factory_start(audio));

    int16_t input[
        PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES *
        PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT
    ];
    int16_t original[
        PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES *
        PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT
    ];
    for (size_t index = 0U;
         index < (sizeof(input) / sizeof(input[0])); ++index) {
        input[index] = (index & 1U) == 0U ? INT16_MIN : INT16_MAX;
    }
    memcpy(original, input, sizeof(input));
    EXPECT_EQ(ESP_OK, platform_audio_factory_write_frames(
        audio, input, PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES
    ));
    EXPECT_TRUE(memcmp(input, original, sizeof(input)) == 0);
    EXPECT_EQ(1, g_mock_factory.write_calls);
    EXPECT_EQ(512, g_mock_factory.write_sizes[0]);
    EXPECT_EQ(100, g_mock_factory.write_timeouts[0]);
    const int16_t *sent =
        (const int16_t *)g_mock_factory.write_data[0];
    for (size_t index = 0U;
         index < (sizeof(input) / sizeof(input[0])); ++index) {
        EXPECT_EQ((index & 1U) == 0U ? INT16_MIN : INT16_MAX, sent[index]);
    }
    running = telemetry();
    EXPECT_EQ(1, running.write_successes);
    EXPECT_EQ(128, running.frames_written);
    EXPECT_EQ(256, running.samples_written);
    EXPECT_EQ(128, running.nonzero_frames);
    EXPECT_EQ(256, running.nonzero_samples);
    EXPECT_EQ(32768, running.maximum_absolute_magnitude);
    EXPECT_EQ(ESP_ERR_INVALID_SIZE,
              platform_audio_factory_write_frames(
                  audio, input,
                  PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES + 1U
              ));

    EXPECT_EQ(ESP_OK, platform_audio_factory_stop(audio));
    EXPECT_EQ(1, g_mock_factory.amp_level);
    EXPECT_TRUE(mock_factory_dma_ring_is_zero());
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED,
              state_of(audio));
    EXPECT_EQ(ESP_OK, platform_audio_factory_start(audio));
    EXPECT_TRUE(g_mock_factory.delay_saw_zero_ring);
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_factory.tx_allocated);
    EXPECT_TRUE(!g_mock_factory.pdm_allocated);
    EXPECT_EQ(1, g_mock_factory.amp_level);
}

static void test_low_readback_failure_rolls_back_high(void)
{
    mock_factory_reset();
    platform_audio_factory_t *audio = NULL;
    const platform_audio_factory_config_t configuration =
        valid_config(UINT8_C(5));
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));
    const size_t begin = g_mock_factory.event_count;
    g_mock_factory.force_low_readback_high = true;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_audio_factory_start(audio));
    EXPECT_EQ(1, g_mock_factory.amp_level);
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED,
              state_of(audio));
    const size_t low =
        find_event_after(MOCK_FACTORY_EVENT_GPIO_LOW, begin);
    const size_t mismatched_read = find_event_after(
        MOCK_FACTORY_EVENT_GPIO_GET_HIGH, low + 1U
    );
    const size_t rollback_high = find_event_after(
        MOCK_FACTORY_EVENT_GPIO_HIGH, mismatched_read + 1U
    );
    EXPECT_TRUE(low < mismatched_read);
    EXPECT_TRUE(mismatched_read < rollback_high);
    g_mock_factory.force_low_readback_high = false;
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));

    mock_factory_reset();
    audio = NULL;
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));
    g_mock_factory.force_post_delay_low_readback_high = true;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_audio_factory_start(audio));
    EXPECT_EQ(351, g_mock_factory.delay_ticks);
    EXPECT_EQ(1, g_mock_factory.amp_level);
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED,
              state_of(audio));
    g_mock_factory.force_post_delay_low_readback_high = false;
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));
}

static void test_stop_failure_latches_failed_safe_and_rejects_start(void)
{
    mock_factory_reset();
    platform_audio_factory_t *audio = NULL;
    const platform_audio_factory_config_t configuration =
        valid_config(UINT8_C(10));
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));
    EXPECT_EQ(ESP_OK, platform_audio_factory_start(audio));

    g_mock_factory.tx_preload_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_audio_factory_stop(audio));
    EXPECT_EQ(1, g_mock_factory.amp_level);
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE,
              state_of(audio));
    const size_t event_count = g_mock_factory.event_count;
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_audio_factory_start(audio));
    EXPECT_EQ(event_count, g_mock_factory.event_count);

    g_mock_factory.tx_preload_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_factory_stop(audio));
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED,
              state_of(audio));
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));
}

static void test_write_failure_with_failed_rollback_latches_state(void)
{
    mock_factory_reset();
    platform_audio_factory_t *audio = NULL;
    const platform_audio_factory_config_t configuration =
        valid_config(UINT8_C(10));
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));
    EXPECT_EQ(ESP_OK, platform_audio_factory_start(audio));
    const int16_t sample[2] = {INT16_MAX, INT16_MIN};

    g_mock_factory.tx_write_result = ESP_ERR_TIMEOUT;
    g_mock_factory.tx_preload_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT,
              platform_audio_factory_write_frames(audio, sample, 1U));
    EXPECT_EQ(1, g_mock_factory.amp_level);
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE,
              state_of(audio));
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_audio_factory_start(audio));

    g_mock_factory.tx_write_result = ESP_OK;
    g_mock_factory.tx_preload_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_factory_stop(audio));
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));
}

static void test_cleanup_ownership_is_retained_until_proven(void)
{
    mock_factory_reset();
    platform_audio_factory_t *audio = NULL;
    const platform_audio_factory_config_t configuration =
        valid_config(UINT8_C(5));
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));

    g_mock_factory.tx_disable_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT,
              platform_audio_factory_destroy(&audio));
    EXPECT_TRUE(audio != NULL);
    EXPECT_TRUE(g_mock_factory.tx_allocated);
    EXPECT_EQ(1, g_mock_factory.amp_level);
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE,
              state_of(audio));
    g_mock_factory.tx_disable_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));
    EXPECT_TRUE(audio == NULL);

    mock_factory_reset();
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));
    g_mock_factory.tx_delete_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT,
              platform_audio_factory_destroy(&audio));
    EXPECT_TRUE(audio != NULL);
    EXPECT_TRUE(g_mock_factory.tx_allocated);
    EXPECT_TRUE(!g_mock_factory.tx_enabled);
    EXPECT_EQ(PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE,
              state_of(audio));
    g_mock_factory.tx_delete_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));
    EXPECT_TRUE(audio == NULL);

    mock_factory_reset();
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));
    g_mock_factory.pdm_delete_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT,
              platform_audio_factory_destroy(&audio));
    EXPECT_TRUE(audio != NULL);
    EXPECT_TRUE(!g_mock_factory.tx_allocated);
    EXPECT_TRUE(g_mock_factory.pdm_allocated);
    platform_audio_factory_telemetry_t retained = telemetry();
    EXPECT_TRUE(retained.resources_retained);
    EXPECT_EQ(1, retained.resources_owned);
    g_mock_factory.pdm_delete_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(!g_mock_factory.pdm_allocated);

    mock_factory_reset();
    g_mock_factory.tx_init_result = ESP_FAIL;
    g_mock_factory.tx_delete_result = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_FAIL,
              platform_audio_factory_create(&configuration, &audio));
    EXPECT_TRUE(audio == NULL);
    EXPECT_TRUE(g_mock_factory.tx_allocated);
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_audio_factory_recover());
    EXPECT_TRUE(g_mock_factory.tx_allocated);
    g_mock_factory.tx_delete_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_audio_factory_recover());
    EXPECT_TRUE(!g_mock_factory.tx_allocated);
}

typedef struct {
    platform_audio_factory_t *audio;
    atomic_bool stop;
    atomic_uint snapshots;
    atomic_uint invalid_snapshots;
} snapshot_stress_context_t;

static void *snapshot_stress_reader(void *opaque)
{
    snapshot_stress_context_t *context = opaque;
    while (!atomic_load_explicit(&context->stop, memory_order_acquire)) {
        platform_audio_factory_telemetry_t value;
        if (platform_audio_factory_get_telemetry(&value) != ESP_OK ||
            value.gpio30_high_successes > value.gpio30_high_attempts ||
            value.gpio30_high_readback_successes >
                value.gpio30_high_successes ||
            value.gpio30_low_successes > value.gpio30_low_attempts ||
            value.gpio30_low_initial_readback_successes >
                value.gpio30_low_successes ||
            value.gpio30_low_second_readback_successes >
                value.gpio30_low_initial_readback_successes ||
            value.nonzero_samples > value.samples_written ||
            value.nonzero_frames > value.frames_written ||
            value.maximum_absolute_magnitude > UINT32_C(32768) ||
            value.resources_owned > UINT32_C(2) ||
            value.running !=
                (value.state == PLATFORM_AUDIO_FACTORY_STATE_RUNNING)) {
            (void)atomic_fetch_add_explicit(
                &context->invalid_snapshots, 1U, memory_order_relaxed
            );
        }
        (void)atomic_fetch_add_explicit(
            &context->snapshots, 1U, memory_order_relaxed
        );
    }
    return NULL;
}

static void test_concurrent_snapshot_invariants(void)
{
    mock_factory_reset();
    platform_audio_factory_t *audio = NULL;
    const platform_audio_factory_config_t configuration =
        valid_config(UINT8_C(10));
    EXPECT_EQ(ESP_OK,
              platform_audio_factory_create(&configuration, &audio));
    EXPECT_EQ(ESP_OK, platform_audio_factory_start(audio));

    snapshot_stress_context_t context = {
        .audio = audio,
    };
    atomic_init(&context.stop, false);
    atomic_init(&context.snapshots, 0U);
    atomic_init(&context.invalid_snapshots, 0U);
    pthread_t reader;
    EXPECT_EQ(0, pthread_create(&reader, NULL, snapshot_stress_reader,
                                &context));
    const int16_t sample[2] = {INT16_MIN, INT16_MAX};
    for (unsigned write = 0U; write < 1000U; ++write) {
        EXPECT_EQ(ESP_OK,
                  platform_audio_factory_write_frames(audio, sample, 1U));
    }
    atomic_store_explicit(&context.stop, true, memory_order_release);
    EXPECT_EQ(0, pthread_join(reader, NULL));
    EXPECT_TRUE(atomic_load_explicit(&context.snapshots,
                                     memory_order_relaxed) > 0U);
    EXPECT_EQ(0, atomic_load_explicit(&context.invalid_snapshots,
                                      memory_order_relaxed));
    EXPECT_EQ(ESP_OK, platform_audio_factory_destroy(&audio));
}

int main(void)
{
    test_safe_gpio_latch_config_rewrite_and_readback();
    test_argument_and_high_readback_gates();
    test_factory_lifecycle_zero_settle_and_attenuation();
    test_low_readback_failure_rolls_back_high();
    test_stop_failure_latches_failed_safe_and_rejects_start();
    test_write_failure_with_failed_rollback_latches_state();
    test_cleanup_ownership_is_retained_until_proven();
    test_concurrent_snapshot_invariants();
    if (failures != 0U) {
        fprintf(stderr, "factory audio runtime tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_AUDIO_FACTORY RUNTIME HOST PASS amp_high_first=1 "
         "gpio_readback=1 zero_settle_ms=350 no_i2c=1 exact_bytes=1 "
         "pdm_rx_first=1 pdm_clk_hz=1024000 tx_mclk=unused "
         "failed_safe=1 retained_cleanup=1 telemetry_thread_safe=1 "
         "stop_failure_start_rejected=1 max_abs=32768");
    return 0;
}
