#include "platform/audio_tone.h"
#include "platform_audio_policy.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    EXPECT_EQ(24, PLATFORM_AUDIO_GPIO_MCLK);
    EXPECT_EQ(30, PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN);
    EXPECT_EQ(45, PLATFORM_AUDIO_GPIO_I2C_SDA);
    EXPECT_EQ(46, PLATFORM_AUDIO_GPIO_I2C_SCL);
    EXPECT_EQ(0x30, PLATFORM_AUDIO_ES8311_WIRE_ADDRESS);
    EXPECT_EQ(0x18, PLATFORM_AUDIO_ES8311_7BIT_ADDRESS);
    EXPECT_TRUE(platform_audio_amp_gpio_level(false));
    EXPECT_TRUE(!platform_audio_amp_gpio_level(true));
    EXPECT_TRUE(platform_audio_sample_rate_supported(11025U));
    EXPECT_TRUE(platform_audio_sample_rate_supported(16000U));
    EXPECT_TRUE(platform_audio_sample_rate_supported(22050U));
    EXPECT_TRUE(!platform_audio_sample_rate_supported(12345U));
    EXPECT_TRUE(platform_audio_bringup_volume_supported(0U));
    EXPECT_TRUE(platform_audio_bringup_volume_supported(10U));
    EXPECT_TRUE(!platform_audio_bringup_volume_supported(11U));
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
    test_bounded_quiet_tone();
    test_large_fade_math_stays_bounded();
    if (failures != 0U) {
        fprintf(stderr, "platform audio policy tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_AUDIO HOST PASS active_low=true safe_level=1 codec_addr7=0x18 tone_hz=440 peak=512 duration_ms=600");
    return 0;
}
