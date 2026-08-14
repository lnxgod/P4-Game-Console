// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Link-only proof for the pinned Doom engine's opt-in sound-module seam.
 * It deliberately performs no rail, GPIO, I2C, I2S, display, WAD, or engine
 * startup operation and is permanently denied by app metadata.
 */

#include <stddef.h>
#include <stdint.h>

#include "doom/audio_mixer.h"
#include "doom/audio_runtime.h"
#include "doomgeneric.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "i_sound.h"

#define P4_DOOM_MAX_SLEEP_MS UINT32_C(60000)

static const char *const TAG = "doom_audio_probe";
extern sound_module_t DG_sound_module;
extern music_module_t DG_music_module;
static sound_module_t *volatile const s_sound_anchor = &DG_sound_module;
static music_module_t *volatile const s_music_anchor = &DG_music_module;
static void (*volatile const s_engine_sound_init_anchor)(boolean) = I_InitSound;
static esp_err_t (*volatile const s_runtime_bind_anchor)(
    const doom_audio_runtime_config_t *) = doom_audio_runtime_bind;

void DG_Init(void)
{
}

void DG_DrawFrame(void)
{
}

void DG_SleepMs(uint32_t milliseconds)
{
    const uint32_t bounded = milliseconds > P4_DOOM_MAX_SLEEP_MS
        ? P4_DOOM_MAX_SLEEP_MS : milliseconds;
    if (bounded == 0U) {
        taskYIELD();
        return;
    }
    const TickType_t ticks = pdMS_TO_TICKS(bounded);
    vTaskDelay(ticks > 0U ? ticks : 1U);
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)((uint64_t)esp_timer_get_time() / UINT64_C(1000));
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (pressed != NULL) {
        *pressed = 0;
    }
    if (key != NULL) {
        *key = 0U;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

void app_main(void)
{
    ESP_LOGI(TAG,
             "P4_DOOM_AUDIO P0 BUILD_ONLY engine_module=%s output_hz=%u "
             "channels=%u runtime_started=false hardware_services=none",
             s_sound_anchor != NULL && s_music_anchor != NULL &&
                     s_engine_sound_init_anchor != NULL &&
                     s_runtime_bind_anchor != NULL
                 ? "linked" : "missing",
             (unsigned)DOOM_AUDIO_OUTPUT_RATE_HZ,
             (unsigned)DOOM_AUDIO_CHANNEL_COUNT);
}
