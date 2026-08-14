// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 ESP32-P4 badge platform contributors
 *
 * D1 runtime seam for the GPL-covered pinned doomgeneric engine. WAD bytes
 * remain external on SD and are exact-hash validated before engine startup.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "doom/video.h"
#include "doomgeneric.h"
#include "i_system.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "platform/display.h"
#include "platform/storage.h"

#define DOOM_SUBMIT_TIMEOUT_MS UINT32_C(100)
#define DOOM_FIRST_FRAME_TIMEOUT_MS UINT32_C(250)
#define DOOM_MAX_SLEEP_MS UINT32_C(60000)
#define DOOM_STATS_INTERVAL_FRAMES UINT32_C(300)

static const char *TAG = "p4_doom_runtime";
static esp_err_t s_frame_error = ESP_OK;
static uint32_t s_frame_count;

_Static_assert(sizeof(pixel_t) == sizeof(uint32_t),
               "doom_runtime requires the engine's default 32-bit pixels");

static void halt_dark(const char *stage, esp_err_t error)
{
    (void)platform_display_set_brightness(0);
    ESP_LOGE(TAG, "P4_DOOM D1 HALT stage=%s error=%s", stage,
             esp_err_to_name(error));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void engine_exit_dark(void)
{
    const esp_err_t err = platform_display_set_brightness(0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "P4_DOOM D1 EXIT_DARK");
    } else {
        ESP_LOGE(TAG, "P4_DOOM D1 EXIT_DARK_FAIL error=%s",
                 esp_err_to_name(err));
    }
}

void DG_Init(void)
{
    if (DG_ScreenBuffer == NULL) {
        halt_dark("engine-screen-buffer", ESP_ERR_NO_MEM);
    }
    ESP_LOGI(TAG,
             "P4_DOOM D1 VIDEO_READY input=0x00RRGGBB output=rgb565 "
             "source=320x200 viewport=960x600 margins=32/32");
}

void DG_DrawFrame(void)
{
    if (s_frame_error != ESP_OK) {
        return;
    }
    if (DG_ScreenBuffer == NULL) {
        s_frame_error = ESP_ERR_INVALID_STATE;
        return;
    }

    s_frame_error = doom_video_submit_xrgb8888(
        (const uint32_t *)DG_ScreenBuffer, DOOM_VIDEO_WIDTH,
        DOOM_SUBMIT_TIMEOUT_MS);
    if (s_frame_error != ESP_OK) {
        return;
    }
    ++s_frame_count;
    if ((s_frame_count % DOOM_STATS_INTERVAL_FRAMES) == 0U) {
        platform_display_stats_t stats;
        const esp_err_t stats_error = platform_display_get_stats(&stats);
        if (stats_error == ESP_OK) {
            ESP_LOGI(TAG,
                     "P4_DOOM D1 VIDEO_STATS frames=%" PRIu32
                     " submits=%" PRIu32 " completions=%" PRIu32
                     " timeouts=%" PRIu32 " failures=%" PRIu32
                     " refreshes=%" PRIu32 " underruns=unavailable",
                     s_frame_count, stats.submits_started,
                     stats.submits_completed, stats.submit_timeouts,
                     stats.submit_failures, stats.refresh_completions);
        }
    }
}

void DG_SleepMs(uint32_t milliseconds)
{
    const uint32_t bounded = milliseconds > DOOM_MAX_SLEEP_MS
                                 ? DOOM_MAX_SLEEP_MS
                                 : milliseconds;
    if (bounded == 0U) {
        taskYIELD();
        return;
    }
    const TickType_t ticks = pdMS_TO_TICKS(bounded);
    vTaskDelay(ticks > 0 ? ticks : 1);
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
        *key = 0;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

void app_main(void)
{
    platform_storage_wad_info_t wad;
    platform_storage_card_info_t card;

    ESP_LOGI(TAG,
             "P4_DOOM D1 START wad_embedded=false input=neutral audio=disabled");
    esp_err_t err = platform_display_init();
    if (err != ESP_OK) {
        halt_dark("display-init", err);
    }
    I_AtExit(engine_exit_dark, true);
    ESP_LOGI(TAG, "P4_DOOM D1 EXIT_DARK_REGISTERED");

    err = platform_storage_init();
    if (err != ESP_OK) {
        halt_dark("storage-init", err);
    }
    err = platform_storage_get_card_info(&card);
    if (err != ESP_OK) {
        halt_dark("storage-card-info", err);
    }
    ESP_LOGI(TAG,
             "P4_DOOM D1 SD_READY capacity=%" PRIu64
             " bus_width=%" PRIu32 " configured_khz=%" PRIu32
             " actual_khz=%" PRIu32,
             card.capacity_bytes, card.configured_bus_width,
             card.configured_frequency_khz, card.real_frequency_khz);

    err = platform_storage_find_doom_shareware(&wad);
    if (err != ESP_OK) {
        halt_dark("wad-validate", err);
    }
    if (wad.identity != PLATFORM_STORAGE_WAD_ID_DOOM_SHAREWARE_1_9 ||
        wad.size_bytes != PLATFORM_STORAGE_DOOM_SHAREWARE_1_9_SIZE) {
        halt_dark("wad-identity", ESP_ERR_INVALID_CRC);
    }
    ESP_LOGI(TAG,
             "P4_DOOM D1 WAD_VERIFIED identity=%s path=%s bytes=%" PRIu64
             " sha256=%s lumps=%" PRIu32,
             platform_storage_wad_id_name(wad.identity), wad.path,
             wad.size_bytes, wad.sha256, wad.lump_count);

    if (chdir(PLATFORM_STORAGE_MOUNT_POINT) != 0) {
        halt_dark("storage-chdir", ESP_FAIL);
    }
    err = doom_video_init();
    if (err != ESP_OK) {
        halt_dark("video-adapter-init", err);
    }
    err = doom_video_submit_black(DOOM_FIRST_FRAME_TIMEOUT_MS);
    if (err != ESP_OK) {
        halt_dark("first-black-frame", err);
    }
    err = platform_display_set_brightness(25);
    if (err != ESP_OK) {
        halt_dark("backlight", err);
    }

    char *argv[] = {
        "doom",
        "-iwad",
        wad.path,
        "-gfxmode",
        "rgba8888",
        "-nosound",
        "-nomusic",
    };
    const int argc = (int)(sizeof(argv) / sizeof(argv[0]));
    ESP_LOGI(TAG,
             "P4_DOOM D1 ENGINE_START gfxmode=rgba8888 wad=%s "
             "video_seam=xrgb8888_to_rgb565 storage_mount_retained=true",
             wad.path);
    doomgeneric_Create(argc, argv);
    if (s_frame_error != ESP_OK) {
        halt_dark("engine-first-frame", s_frame_error);
    }

    for (;;) {
        doomgeneric_Tick();
        if (s_frame_error != ESP_OK) {
            halt_dark("engine-frame", s_frame_error);
        }
    }
}
