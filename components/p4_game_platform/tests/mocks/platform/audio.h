#ifndef P4_GAME_PLATFORM_TEST_PLATFORM_AUDIO_H
#define P4_GAME_PLATFORM_TEST_PLATFORM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define PLATFORM_AUDIO_CHANNEL_COUNT 2U
#define PLATFORM_AUDIO_SAMPLE_RATE_HZ 16000U
#define PLATFORM_AUDIO_MAX_WRITE_FRAMES 128U
#define PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT 10U

typedef struct platform_audio {
    uint32_t marker;
} platform_audio_t;

typedef struct {
    void *control_bus;
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
} platform_audio_config_t;

esp_err_t platform_audio_force_safe_shutdown(void);
esp_err_t platform_audio_recover(void);
esp_err_t platform_audio_create(const platform_audio_config_t *config,
                                platform_audio_t **out_audio);
esp_err_t platform_audio_start(platform_audio_t *audio);
esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *interleaved_pcm,
                                      size_t frame_count);
esp_err_t platform_audio_stop(platform_audio_t *audio);
esp_err_t platform_audio_destroy(platform_audio_t **audio);

#endif
