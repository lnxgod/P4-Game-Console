#include "platform_audio_es8311/audio.h"
#include "platform_audio_es8311_policy.h"

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

static void test_electrical_contract(void)
{
    EXPECT_EQ(1, PLATFORM_AUDIO_ES8311_I2S_CONTROLLER);
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    EXPECT_EQ(10, PLATFORM_AUDIO_ES8311_GPIO_LRCLK);
    EXPECT_EQ(12, PLATFORM_AUDIO_ES8311_GPIO_BCLK);
    EXPECT_EQ(9, PLATFORM_AUDIO_ES8311_GPIO_DOUT);
    EXPECT_EQ(13, PLATFORM_AUDIO_ES8311_GPIO_MCLK);
    EXPECT_EQ(53, PLATFORM_AUDIO_ES8311_GPIO_AMP_SHUTDOWN);
    EXPECT_EQ(1, PLATFORM_AUDIO_ES8311_AMP_ACTIVE_LEVEL);
#else
    EXPECT_EQ(21, PLATFORM_AUDIO_ES8311_GPIO_LRCLK);
    EXPECT_EQ(22, PLATFORM_AUDIO_ES8311_GPIO_BCLK);
    EXPECT_EQ(23, PLATFORM_AUDIO_ES8311_GPIO_DOUT);
    EXPECT_EQ(24, PLATFORM_AUDIO_ES8311_GPIO_MCLK);
    EXPECT_EQ(30, PLATFORM_AUDIO_ES8311_GPIO_AMP_SHUTDOWN);
    EXPECT_EQ(0, PLATFORM_AUDIO_ES8311_AMP_ACTIVE_LEVEL);
#endif
    EXPECT_EQ(0x18, PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_7BIT);
    EXPECT_EQ(0x30, PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_WIRE);
    EXPECT_EQ(100000, PLATFORM_AUDIO_ES8311_I2C_SPEED_HZ);
    EXPECT_EQ(16000, PLATFORM_AUDIO_ES8311_SAMPLE_RATE_HZ);
    EXPECT_EQ(350, PLATFORM_AUDIO_ES8311_STARTUP_ZERO_MS);
    EXPECT_EQ(1536, PLATFORM_AUDIO_ES8311_ZERO_PREROLL_FRAMES);
    EXPECT_TRUE(platform_audio_es8311_volume_supported(UINT8_C(0)));
    EXPECT_TRUE(platform_audio_es8311_volume_supported(UINT8_C(1)));
    EXPECT_TRUE(platform_audio_es8311_volume_supported(UINT8_C(10)));
    EXPECT_TRUE(!platform_audio_es8311_volume_supported(UINT8_C(11)));
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    EXPECT_EQ(10, platform_audio_es8311_codec_volume_percent(UINT8_C(1)));
    EXPECT_EQ(60, platform_audio_es8311_codec_volume_percent(UINT8_C(6)));
    EXPECT_EQ(100, platform_audio_es8311_codec_volume_percent(UINT8_C(10)));
    EXPECT_EQ(32768, platform_audio_es8311_peak_for_volume(UINT8_C(1)));
    EXPECT_EQ(32768, platform_audio_es8311_peak_for_volume(UINT8_C(10)));
#else
    EXPECT_EQ(1, platform_audio_es8311_codec_volume_percent(UINT8_C(1)));
    EXPECT_EQ(6, platform_audio_es8311_codec_volume_percent(UINT8_C(6)));
    EXPECT_EQ(10, platform_audio_es8311_codec_volume_percent(UINT8_C(10)));
    EXPECT_EQ(51, platform_audio_es8311_peak_for_volume(UINT8_C(1)));
    EXPECT_EQ(512, platform_audio_es8311_peak_for_volume(UINT8_C(10)));
#endif
    EXPECT_EQ(0, platform_audio_es8311_codec_volume_percent(UINT8_C(0)));
}

static void test_attenuation_is_bounded_and_immutable(void)
{
    const int16_t input[] = {
        INT16_MIN, INT16_MAX, -16384, 16384, -1, 0, 1,
    };
    const int16_t original[] = {
        INT16_MIN, INT16_MAX, -16384, 16384, -1, 0, 1,
    };
    int16_t output[sizeof(input) / sizeof(input[0])] = {0};
    EXPECT_TRUE(platform_audio_es8311_attenuate_pcm16(
        input, output, sizeof(input) / sizeof(input[0]), UINT8_C(10)
    ));
    EXPECT_TRUE(memcmp(input, original, sizeof(input)) == 0);
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    EXPECT_TRUE(memcmp(input, output, sizeof(input)) == 0);
#else
    EXPECT_EQ(-512, output[0]);
    EXPECT_EQ(512, output[1]);
    EXPECT_EQ(-256, output[2]);
    EXPECT_EQ(256, output[3]);
    for (size_t index = 0U; index < sizeof(output) / sizeof(output[0]);
         ++index) {
        const int32_t sample = output[index];
        const int32_t magnitude = sample < 0 ? -sample : sample;
        EXPECT_TRUE(magnitude <= 512);
    }
#endif

    EXPECT_TRUE(platform_audio_es8311_attenuate_pcm16(
        input, output, sizeof(input) / sizeof(input[0]), UINT8_C(0)
    ));
    for (size_t index = 0U; index < sizeof(output) / sizeof(output[0]); ++index) {
        EXPECT_EQ(0, output[index]);
    }
    EXPECT_TRUE(!platform_audio_es8311_attenuate_pcm16(
        input, (int16_t *)(void *)input,
        sizeof(input) / sizeof(input[0]), UINT8_C(10)
    ));
    EXPECT_TRUE(!platform_audio_es8311_attenuate_pcm16(
        NULL, output, 1U, UINT8_C(10)
    ));
}

static void test_large_buffer_math(void)
{
    const size_t count = (size_t)UINT16_MAX + 2U;
    int16_t *input = calloc(count, sizeof(*input));
    int16_t *output = calloc(count, sizeof(*output));
    EXPECT_TRUE(input != NULL && output != NULL);
    if (input == NULL || output == NULL) {
        free(input);
        free(output);
        return;
    }
    for (size_t index = 0U; index < count; ++index) {
        input[index] = (index & 1U) == 0U ? INT16_MIN : INT16_MAX;
    }
    EXPECT_TRUE(platform_audio_es8311_attenuate_pcm16(
        input, output, count, UINT8_C(10)
    ));
    for (size_t index = 0U; index < count; ++index) {
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
        EXPECT_EQ(input[index], output[index]);
#else
        EXPECT_EQ((index & 1U) == 0U ? -512 : 512, output[index]);
#endif
    }
    free(input);
    free(output);
}

int main(void)
{
    test_electrical_contract();
    test_attenuation_is_bounded_and_immutable();
    test_large_buffer_math();
    if (failures != 0U) {
        fprintf(stderr, "ES8311 audio policy tests failed: %u\n", failures);
        return 1;
    }
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    puts("P4_AUDIO_ES8311 WAVE HOST PASS addr7=0x18 mclk_gpio=13 amp_gpio=53 amp_active_high=1 codec_volume_6=60 pcm=full-scale immutable_input=1");
#else
    puts("P4_AUDIO_ES8311 HOST PASS codec_only=1 addr7=0x18 mclk_gpio=24 amp_safe_high=1 startup_zero_ms=350 direct_fallback=0 peak=512 immutable_input=1");
#endif
    return 0;
}
