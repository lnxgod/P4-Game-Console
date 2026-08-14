#ifndef DOOM_AUDIO_TEST_PLATFORM_AUDIO_H
#define DOOM_AUDIO_TEST_PLATFORM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct platform_audio platform_audio_t;

typedef enum {
    PLATFORM_AUDIO_STATE_READY_MUTED = 0,
    PLATFORM_AUDIO_STATE_RUNNING,
} platform_audio_state_t;

esp_err_t platform_audio_force_safe_shutdown(void);
esp_err_t platform_audio_start(platform_audio_t *audio);
esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *interleaved_pcm,
                                      size_t frame_count);
esp_err_t platform_audio_stop(platform_audio_t *audio);
esp_err_t platform_audio_get_state(const platform_audio_t *audio,
                                   platform_audio_state_t *out_state);

#endif
