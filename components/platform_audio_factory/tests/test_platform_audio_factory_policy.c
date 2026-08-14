#include "platform_audio_factory/audio.h"
#include "platform_audio_factory_policy.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void test_factory_contract(void)
{
    EXPECT_EQ(0, PLATFORM_AUDIO_FACTORY_PDM_I2S_CONTROLLER);
    EXPECT_EQ(24, PLATFORM_AUDIO_FACTORY_PDM_GPIO_CLK);
    EXPECT_EQ(26, PLATFORM_AUDIO_FACTORY_PDM_GPIO_DIN);
    EXPECT_EQ(8, PLATFORM_AUDIO_FACTORY_PDM_DOWNSAMPLE_FACTOR);
    EXPECT_EQ(8, PLATFORM_AUDIO_FACTORY_PDM_BCLK_DIV);
    EXPECT_EQ(1, PLATFORM_AUDIO_FACTORY_I2S_CONTROLLER);
    EXPECT_EQ(21, PLATFORM_AUDIO_FACTORY_GPIO_LRCLK);
    EXPECT_EQ(22, PLATFORM_AUDIO_FACTORY_GPIO_BCLK);
    EXPECT_EQ(23, PLATFORM_AUDIO_FACTORY_GPIO_DOUT);
    EXPECT_EQ(30, PLATFORM_AUDIO_FACTORY_GPIO_AMP_SHUTDOWN);
    EXPECT_EQ(6, PLATFORM_AUDIO_FACTORY_DMA_DESCRIPTOR_COUNT);
    EXPECT_EQ(256, PLATFORM_AUDIO_FACTORY_DMA_FRAMES_PER_DESCRIPTOR);
    EXPECT_EQ(1536, PLATFORM_AUDIO_FACTORY_ZERO_PREROLL_FRAMES);
    EXPECT_EQ(100, PLATFORM_AUDIO_FACTORY_WRITE_TIMEOUT_MS);
    EXPECT_EQ(350, PLATFORM_AUDIO_FACTORY_STARTUP_ZERO_MS);
    EXPECT_EQ(32768, PLATFORM_AUDIO_FACTORY_MAX_OUTPUT_ABS_MAGNITUDE);
    EXPECT_EQ(32768, PLATFORM_AUDIO_FACTORY_MAX_OUTPUT_PEAK);
    EXPECT_EQ(32768,
              PLATFORM_AUDIO_FACTORY_POLICY_MAX_OUTPUT_ABS_MAGNITUDE);
    EXPECT_EQ(32768, PLATFORM_AUDIO_FACTORY_POLICY_MAX_OUTPUT_PEAK);
    EXPECT_TRUE(platform_audio_factory_amp_gpio_level(false));
    EXPECT_TRUE(!platform_audio_factory_amp_gpio_level(true));
    EXPECT_TRUE(platform_audio_factory_sample_rate_supported(16000U));
    EXPECT_TRUE(!platform_audio_factory_sample_rate_supported(48000U));
    EXPECT_TRUE(!platform_audio_factory_volume_supported(0U));
    EXPECT_TRUE(platform_audio_factory_volume_supported(1U));
    EXPECT_TRUE(platform_audio_factory_volume_supported(10U));
    EXPECT_TRUE(!platform_audio_factory_volume_supported(11U));
    EXPECT_EQ(0, platform_audio_factory_peak_for_volume(0U));
    EXPECT_EQ(3276, platform_audio_factory_peak_for_volume(1U));
    EXPECT_EQ(16384, platform_audio_factory_peak_for_volume(5U));
    EXPECT_EQ(32768, platform_audio_factory_peak_for_volume(10U));
}

static void test_attenuation_boundaries(void)
{
    static const int16_t input[] = {
        INT16_MIN, -INT16_C(16384), -INT16_C(1), INT16_C(0),
        INT16_C(1), INT16_C(16384), INT16_MAX,
    };
    int16_t original[sizeof(input) / sizeof(input[0])];
    int16_t output[sizeof(input) / sizeof(input[0])];
    memcpy(original, input, sizeof(input));
    EXPECT_TRUE(platform_audio_factory_attenuate_pcm16(
        input, output, sizeof(input) / sizeof(input[0]), UINT8_C(10)
    ));
    EXPECT_TRUE(memcmp(input, original, sizeof(input)) == 0);
    EXPECT_TRUE(memcmp(input, output, sizeof(input)) == 0);

    EXPECT_TRUE(!platform_audio_factory_attenuate_pcm16(
        NULL, output, 1U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_factory_attenuate_pcm16(
        input, NULL, 1U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_factory_attenuate_pcm16(
        input, output, 0U, UINT8_C(10)
    ));
    int16_t overlap[8] = {0};
    EXPECT_TRUE(!platform_audio_factory_attenuate_pcm16(
        overlap, overlap, 4U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_factory_attenuate_pcm16(
        overlap, &overlap[1], 4U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_factory_attenuate_pcm16(
        &overlap[1], overlap, 4U, UINT8_C(10)
    ));
}

static void test_exhaustive_pcm16_caps(void)
{
    enum { SAMPLE_COUNT = UINT16_MAX + 1U };
    int16_t *input = malloc((size_t)SAMPLE_COUNT * sizeof(*input));
    int16_t *original = malloc((size_t)SAMPLE_COUNT * sizeof(*original));
    int16_t *output = malloc((size_t)SAMPLE_COUNT * sizeof(*output));
    EXPECT_TRUE(input != NULL && original != NULL && output != NULL);
    if (input == NULL || original == NULL || output == NULL) {
        free(output);
        free(original);
        free(input);
        return;
    }
    for (size_t index = 0U; index < (size_t)SAMPLE_COUNT; ++index) {
        input[index] = (int16_t)(INT32_C(-32768) + (int32_t)index);
    }
    memcpy(original, input, (size_t)SAMPLE_COUNT * sizeof(*input));

    for (uint8_t volume = UINT8_C(1); volume <= UINT8_C(10); ++volume) {
        const int32_t peak =
            (int32_t)platform_audio_factory_peak_for_volume(volume);
        EXPECT_TRUE(platform_audio_factory_attenuate_pcm16(
            input, output, (size_t)SAMPLE_COUNT, volume
        ));
        EXPECT_TRUE(memcmp(input, original,
                           (size_t)SAMPLE_COUNT * sizeof(*input)) == 0);
        for (size_t index = 0U; index < (size_t)SAMPLE_COUNT; ++index) {
            const int32_t sample = output[index];
            const int32_t magnitude = sample < 0 ? -sample : sample;
            EXPECT_TRUE(magnitude <= peak);
        }
    }
    free(output);
    free(original);
    free(input);
}

int main(void)
{
    test_factory_contract();
    test_attenuation_boundaries();
    test_exhaustive_pcm16_caps();
    if (failures != 0U) {
        fprintf(stderr, "factory audio policy tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_AUDIO_FACTORY POLICY HOST PASS pdm_rx0=1 pdm_clock_hz=1024000 "
         "tx_i2s1=1 tx_mclk=unused rate=16000 settle_ms=350 max_abs=32768 "
         "unity_step10=1 immutable=1 exhaustive=1");
    return 0;
}
