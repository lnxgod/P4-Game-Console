// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 ESP32-P4 badge platform contributors
 *
 * E1 fallback runtime. The exact user-local shareware WAD is linked into the
 * local build artifact and exposed to the unmodified engine as one immutable
 * VFS file. No WAD bytes or generated firmware belong in source control.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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
#include "mbedtls/sha256.h"
#pragma GCC diagnostic pop
#include "platform/display.h"
#include "platform/readonly_blob.h"

#define DOOM_SUBMIT_TIMEOUT_MS UINT32_C(100)
#define DOOM_FIRST_FRAME_TIMEOUT_MS UINT32_C(250)
#define DOOM_MAX_SLEEP_MS UINT32_C(60000)
#define DOOM_STATS_INTERVAL_FRAMES UINT32_C(300)
#define EMBEDDED_WAD_BYTES ((size_t)4196020U)
#define EMBEDDED_WAD_PATH "/doom/doom1.wad"

extern const uint8_t _binary_doom_shareware_wad_start[];
extern const uint8_t _binary_doom_shareware_wad_end[];

static const uint8_t s_expected_wad_sha256[32] = {
    0x1d, 0x7d, 0x43, 0xbe, 0x50, 0x1e, 0x67, 0xd9,
    0x27, 0xe4, 0x15, 0xe0, 0xb8, 0xf3, 0xe2, 0x9c,
    0x3b, 0xf3, 0x30, 0x75, 0xe8, 0x59, 0x72, 0x18,
    0x16, 0xf6, 0x52, 0xa5, 0x26, 0xca, 0xc7, 0x71,
};

static const char *TAG = "p4_doom_embedded";
static esp_err_t s_frame_error = ESP_OK;
static uint32_t s_frame_count;

_Static_assert(sizeof(pixel_t) == sizeof(uint32_t),
               "doom_embedded requires the engine's default 32-bit pixels");

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static void halt_dark(const char *stage, esp_err_t error)
{
    (void)platform_display_set_brightness(0);
    ESP_LOGE(TAG, "P4_DOOM_EMBEDDED E1 HALT stage=%s error=%s", stage,
             esp_err_to_name(error));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void engine_exit_dark(void)
{
    const esp_err_t err = platform_display_set_brightness(0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "P4_DOOM_EMBEDDED E1 EXIT_DARK");
    } else {
        ESP_LOGE(TAG, "P4_DOOM_EMBEDDED E1 EXIT_DARK_FAIL error=%s",
                 esp_err_to_name(err));
    }
}

static esp_err_t verify_embedded_wad(const uint8_t *data, size_t size_bytes)
{
    if (data == NULL || size_bytes != EMBEDDED_WAD_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (memcmp(data, "IWAD", 4U) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const uint32_t lump_count = read_u32_le(&data[4]);
    const uint32_t directory_offset = read_u32_le(&data[8]);
    const uint64_t directory_bytes = (uint64_t)lump_count * UINT64_C(16);
    if (lump_count == 0U || (uint64_t)directory_offset > (uint64_t)size_bytes
        || directory_bytes > (uint64_t)size_bytes - (uint64_t)directory_offset) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t digest[32];
    if (mbedtls_sha256(data, size_bytes, digest, 0) != 0) {
        return ESP_FAIL;
    }
    return memcmp(digest, s_expected_wad_sha256, sizeof(digest)) == 0
        ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static esp_err_t verify_readonly_vfs(void)
{
    struct stat metadata;
    if (stat(EMBEDDED_WAD_PATH, &metadata) != 0
        || !S_ISREG(metadata.st_mode)
        || metadata.st_size != (off_t)EMBEDDED_WAD_BYTES
        || (metadata.st_mode & 0222) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    FILE *file = fopen(EMBEDDED_WAD_PATH, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    uint8_t header[12];
    esp_err_t result = ESP_OK;
    if (fread(header, 1U, sizeof(header), file) != sizeof(header)
        || memcmp(header, "IWAD", 4U) != 0
        || fseek(file, 0L, SEEK_END) != 0
        || ftell(file) != (long)EMBEDDED_WAD_BYTES) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (fclose(file) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
    }
    return result;
}

void DG_Init(void)
{
    if (DG_ScreenBuffer == NULL) {
        halt_dark("engine-screen-buffer", ESP_ERR_NO_MEM);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_EMBEDDED E1 VIDEO_READY input=0x00RRGGBB "
             "output=rgb565 source=320x200 viewport=960x600 margins=32/32");
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
        DOOM_SUBMIT_TIMEOUT_MS
    );
    if (s_frame_error != ESP_OK) {
        return;
    }
    ++s_frame_count;
    if ((s_frame_count % DOOM_STATS_INTERVAL_FRAMES) == 0U) {
        platform_display_stats_t stats;
        if (platform_display_get_stats(&stats) == ESP_OK) {
            ESP_LOGI(TAG,
                     "P4_DOOM_EMBEDDED E1 VIDEO_STATS frames=%" PRIu32
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
        ? DOOM_MAX_SLEEP_MS : milliseconds;
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
    ESP_LOGI(TAG,
             "P4_DOOM_EMBEDDED E1 START wad=embedded-exact "
             "vfs=read-only input=neutral audio=disabled sd=unused");
    esp_err_t err = platform_display_init();
    if (err != ESP_OK) {
        halt_dark("display-init", err);
    }
    I_AtExit(engine_exit_dark, true);
    ESP_LOGI(TAG, "P4_DOOM_EMBEDDED E1 EXIT_DARK_REGISTERED");

    const uint8_t *const wad_start = _binary_doom_shareware_wad_start;
    const uint8_t *const wad_end = _binary_doom_shareware_wad_end;
    const uintptr_t wad_start_address = (uintptr_t)wad_start;
    const uintptr_t wad_end_address = (uintptr_t)wad_end;
    if (wad_end_address < wad_start_address) {
        halt_dark("wad-linker-range", ESP_ERR_INVALID_SIZE);
    }
    const size_t wad_size = (size_t)(wad_end_address - wad_start_address);
    err = verify_embedded_wad(wad_start, wad_size);
    if (err != ESP_OK) {
        halt_dark("wad-validate", err);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_EMBEDDED E1 WAD_VERIFIED identity=doom-shareware-1.9 "
             "bytes=%u sha256=%s",
             (unsigned)EMBEDDED_WAD_BYTES,
             "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771");

    const platform_readonly_blob_config_t blob_config = {
        .base_path = "/doom",
        .file_name = "doom1.wad",
        .data = wad_start,
        .size_bytes = wad_size,
    };
    err = platform_readonly_blob_register(&blob_config);
    if (err != ESP_OK) {
        halt_dark("wad-vfs-register", err);
    }
    err = verify_readonly_vfs();
    if (err != ESP_OK) {
        halt_dark("wad-vfs-readback", err);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_EMBEDDED E1 VFS_READY path=%s mode=read-only max_open=%u",
             EMBEDDED_WAD_PATH, (unsigned)PLATFORM_READONLY_BLOB_MAX_OPEN_FILES);

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
        EMBEDDED_WAD_PATH,
        "-gfxmode",
        "rgba8888",
        "-nosound",
        "-nomusic",
    };
    const int argc = (int)(sizeof(argv) / sizeof(argv[0]));
    ESP_LOGI(TAG,
             "P4_DOOM_EMBEDDED E1 ENGINE_START gfxmode=rgba8888 wad=%s "
             "video_seam=xrgb8888_to_rgb565",
             EMBEDDED_WAD_PATH);
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
