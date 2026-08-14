#include "platform/audio_tone.h"
#include "platform_audio_policy.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;

#define EXPECT_TRUE(expression_)                                                \
    do {                                                                        \
        if (!(expression_)) {                                                   \
            fprintf(stderr, "%s:%d: expected true: %s\n", __FILE__,         \
                    __LINE__, #expression_);                                    \
            failures++;                                                        \
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
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void test_electrical_policy(void)
{
    EXPECT_EQ(1, PLATFORM_AUDIO_I2S_CONTROLLER);
    EXPECT_EQ(21, PLATFORM_AUDIO_GPIO_LRCLK);
    EXPECT_EQ(22, PLATFORM_AUDIO_GPIO_BCLK);
    EXPECT_EQ(23, PLATFORM_AUDIO_GPIO_DOUT);
    EXPECT_EQ(30, PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN);
    EXPECT_EQ(6, PLATFORM_AUDIO_DMA_DESCRIPTOR_COUNT);
    EXPECT_EQ(256, PLATFORM_AUDIO_DMA_FRAMES_PER_DESCRIPTOR);
    EXPECT_EQ(1536, PLATFORM_AUDIO_ZERO_PREROLL_FRAMES);
    EXPECT_EQ(100, PLATFORM_AUDIO_WRITE_TIMEOUT_MS);
    EXPECT_TRUE(PLATFORM_AUDIO_WRITE_TIMEOUT_MS < 250);
    EXPECT_TRUE(platform_audio_amp_gpio_level(false));
    EXPECT_TRUE(!platform_audio_amp_gpio_level(true));
    EXPECT_TRUE(platform_audio_sample_rate_supported(16000U));
    EXPECT_TRUE(!platform_audio_sample_rate_supported(15999U));
    EXPECT_TRUE(!platform_audio_sample_rate_supported(16001U));
    EXPECT_TRUE(!platform_audio_sample_rate_supported(12345U));
    EXPECT_TRUE(!platform_audio_bringup_volume_supported(0U));
    EXPECT_TRUE(platform_audio_bringup_volume_supported(1U));
    EXPECT_TRUE(platform_audio_bringup_volume_supported(10U));
    EXPECT_TRUE(!platform_audio_bringup_volume_supported(11U));
    EXPECT_EQ(0, platform_audio_peak_for_volume(0U));
    EXPECT_EQ(51, platform_audio_peak_for_volume(1U));
    EXPECT_EQ(256, platform_audio_peak_for_volume(5U));
    EXPECT_EQ(512, platform_audio_peak_for_volume(10U));
    EXPECT_EQ(0, platform_audio_peak_for_volume(11U));
}

static void test_immutable_attenuation_boundaries(void)
{
    static const int16_t input[] = {
        INT16_MIN, -INT16_C(16384), -INT16_C(1), INT16_C(0),
        INT16_C(1), INT16_C(16384), INT16_MAX,
    };
    int16_t original[sizeof(input) / sizeof(input[0])];
    int16_t guarded[(sizeof(input) / sizeof(input[0])) + 2U];
    memcpy(original, input, sizeof(input));
    guarded[0] = INT16_C(12345);
    guarded[(sizeof(guarded) / sizeof(guarded[0])) - 1U] = -INT16_C(12345);
    int16_t *const output = &guarded[1];

    EXPECT_TRUE(platform_audio_attenuate_pcm16(
        input, output, sizeof(input) / sizeof(input[0]), UINT8_C(10)
    ));
    EXPECT_TRUE(memcmp(input, original, sizeof(input)) == 0);
    EXPECT_EQ(12345, guarded[0]);
    EXPECT_EQ(-12345,
              guarded[(sizeof(guarded) / sizeof(guarded[0])) - 1U]);
    EXPECT_EQ(-512, output[0]);
    EXPECT_EQ(-256, output[1]);
    EXPECT_EQ(0, output[2]);
    EXPECT_EQ(0, output[3]);
    EXPECT_EQ(0, output[4]);
    EXPECT_EQ(256, output[5]);
    EXPECT_EQ(512, output[6]);

    EXPECT_TRUE(!platform_audio_attenuate_pcm16(
        NULL, output, 1U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_attenuate_pcm16(
        input, NULL, 1U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_attenuate_pcm16(
        input, output, 0U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_attenuate_pcm16(
        input, output, 1U, UINT8_C(0)
    ));
    EXPECT_TRUE(!platform_audio_attenuate_pcm16(
        input, output, 1U, UINT8_C(11)
    ));

    int16_t overlap[8] = {0};
    EXPECT_TRUE(!platform_audio_attenuate_pcm16(
        overlap, overlap, 4U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_attenuate_pcm16(
        overlap, &overlap[1], 4U, UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_attenuate_pcm16(
        &overlap[1], overlap, 4U, UINT8_C(10)
    ));
}

static void test_exhaustive_pcm16_caps(void)
{
    enum { SAMPLE_COUNT = UINT16_MAX + 1U };
    int16_t *input = malloc((size_t)SAMPLE_COUNT * sizeof(*input));
    int16_t *original = malloc((size_t)SAMPLE_COUNT * sizeof(*original));
    int16_t *output = malloc((size_t)SAMPLE_COUNT * sizeof(*output));
    EXPECT_TRUE(input != NULL);
    EXPECT_TRUE(original != NULL);
    EXPECT_TRUE(output != NULL);
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
            (int32_t)platform_audio_peak_for_volume(volume);
        EXPECT_TRUE(platform_audio_attenuate_pcm16(
            input, output, (size_t)SAMPLE_COUNT, volume
        ));
        EXPECT_TRUE(memcmp(input, original,
                           (size_t)SAMPLE_COUNT * sizeof(*input)) == 0);
        EXPECT_EQ(-peak, output[0]);
        EXPECT_EQ(peak, output[SAMPLE_COUNT - 1U]);
        int32_t previous = INT32_MIN;
        for (size_t index = 0U; index < (size_t)SAMPLE_COUNT; ++index) {
            const int32_t sample = output[index];
            const int32_t magnitude = sample < 0 ? -sample : sample;
            EXPECT_TRUE(magnitude <= peak);
            EXPECT_TRUE(sample >= previous);
            previous = sample;
        }
    }

    free(output);
    free(original);
    free(input);
}

static void test_bounded_quiet_tone(void)
{
    enum {
        SAMPLE_RATE = 16000,
        FRAME_COUNT = 9600,
        FADE_FRAMES = 1280,
        PEAK = 512,
    };
    int16_t *samples = calloc(FRAME_COUNT * 2U, sizeof(*samples));
    EXPECT_TRUE(samples != NULL);
    if (samples == NULL) {
        return;
    }
    const platform_audio_tone_config_t config = {
        .sample_rate_hz = SAMPLE_RATE,
        .frequency_hz = 440U,
        .peak_amplitude = PEAK,
        .frame_count = FRAME_COUNT,
        .fade_frames = FADE_FRAMES,
    };
    EXPECT_TRUE(platform_audio_generate_quiet_tone(
        &config, samples, FRAME_COUNT * 2U
    ));
    EXPECT_EQ(0, samples[0]);
    EXPECT_EQ(0, samples[1]);
    EXPECT_EQ(0, samples[(FRAME_COUNT - 1U) * 2U]);
    EXPECT_EQ(0, samples[((FRAME_COUNT - 1U) * 2U) + 1U]);

    int32_t maximum = 0;
    int64_t sum = 0;
    for (size_t frame = 0U; frame < FRAME_COUNT; ++frame) {
        const int32_t left = samples[frame * 2U];
        const int32_t right = samples[(frame * 2U) + 1U];
        EXPECT_EQ(left, right);
        const int32_t magnitude = left < 0 ? -left : left;
        if (magnitude > maximum) {
            maximum = magnitude;
        }
        sum += left;
    }
    EXPECT_TRUE(maximum > 480);
    EXPECT_TRUE(maximum <= PEAK);
    EXPECT_TRUE(sum > -FRAME_COUNT && sum < FRAME_COUNT);

    platform_audio_tone_config_t invalid = config;
    invalid.frequency_hz = SAMPLE_RATE / 2U;
    EXPECT_TRUE(!platform_audio_generate_quiet_tone(
        &invalid, samples, FRAME_COUNT * 2U
    ));
    EXPECT_TRUE(!platform_audio_generate_quiet_tone(
        &config, samples, (FRAME_COUNT * 2U) - 1U
    ));
    free(samples);
}

static void test_large_fade_math_stays_bounded(void)
{
    enum {
        FRAME_COUNT = 131074,
        FADE_FRAMES = 65537,
        PEAK = INT16_MAX,
    };
    int16_t *samples = calloc(FRAME_COUNT * 2U, sizeof(*samples));
    EXPECT_TRUE(samples != NULL);
    if (samples == NULL) {
        return;
    }
    const platform_audio_tone_config_t config = {
        .sample_rate_hz = 16000U,
        .frequency_hz = 440U,
        .peak_amplitude = PEAK,
        .frame_count = FRAME_COUNT,
        .fade_frames = FADE_FRAMES,
    };
    EXPECT_TRUE(platform_audio_generate_quiet_tone(
        &config, samples, FRAME_COUNT * 2U
    ));
    EXPECT_EQ(0, samples[0]);
    EXPECT_EQ(0, samples[(FRAME_COUNT - 1U) * 2U]);
    for (size_t frame = 0U; frame < FRAME_COUNT; ++frame) {
        const int32_t sample = samples[frame * 2U];
        const int32_t magnitude = sample < 0 ? -sample : sample;
        EXPECT_TRUE(magnitude <= PEAK);
        EXPECT_EQ(sample, samples[(frame * 2U) + 1U]);
    }
    free(samples);
}

int main(void)
{
    test_electrical_policy();
    test_immutable_attenuation_boundaries();
    test_exhaustive_pcm16_caps();
    test_bounded_quiet_tone();
    test_large_fade_math_stays_bounded();
    if (failures != 0U) {
        fprintf(stderr, "platform audio policy tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_AUDIO HOST PASS direct_i2s=1 mclk=unused active_low=true safe_level=1 rate=16000 max_frames=128 timeout_ms=100 peak_v10=512 immutable=1 exhaustive_pcm16=1");
    return 0;
}
