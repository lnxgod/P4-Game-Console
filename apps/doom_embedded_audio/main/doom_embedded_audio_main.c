// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 ESP32-P4 badge platform contributors
 *
 * E2 audio composite. The exact user-local shareware WAD is linked only into
 * the local artifact. Board services retain ownership of every raw peripheral.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "doom/audio_mixer.h"
#include "doom/audio_runtime.h"
#include "doom/video.h"
#include "doomgeneric.h"
#include "i_system.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#pragma GCC diagnostic pop
#include "platform/audio.h"
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

static const char *TAG = "p4_doom_audio_e2";
static esp_err_t s_frame_error = ESP_OK;
static uint32_t s_frame_count;
static platform_audio_t *s_audio;
static bool s_audio_bound;
static bool s_display_initialized;
static bool s_video_initialized;
static bool s_cleanup_active;
static bool s_cleanup_complete;
static bool s_engine_heap_logged;
static bool s_engine_invoked;

_Static_assert(sizeof(pixel_t) == sizeof(uint32_t),
               "doom_embedded_audio requires default 32-bit engine pixels");
_Static_assert(DOOM_AUDIO_OUTPUT_RATE_HZ == 16000,
               "E2 platform backend and Doom mixer rate must remain aligned");

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static void log_memory_snapshot(const char *stage)
{
    const size_t internal_free = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_largest = heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_free = heap_caps_get_free_size(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_largest = heap_caps_get_largest_free_block(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const UBaseType_t main_stack_hwm = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG,
             "P4_DOOM_AUDIO E2 MEMORY stage=%s internal_free=%u "
             "internal_largest=%u psram_free=%u psram_largest=%u "
             "main_stack_hwm_bytes=%u",
             stage, (unsigned)internal_free, (unsigned)internal_largest,
             (unsigned)psram_free, (unsigned)psram_largest,
             (unsigned)main_stack_hwm);
}

/*
 * This callback is deliberately conservative. A failed unbind means a worker
 * may still borrow both the backend and cached samples, so those objects are
 * retained instead of being freed underneath it.
 */
static void composite_cleanup(void)
{
    if (s_cleanup_active || s_cleanup_complete) {
        return;
    }
    s_cleanup_active = true;
    const esp_err_t force_safe_result =
        platform_audio_force_safe_shutdown();

    const esp_err_t stop_result = doom_audio_runtime_stop();
    esp_err_t state_result = ESP_ERR_INVALID_STATE;
    platform_audio_state_t audio_state = PLATFORM_AUDIO_STATE_RUNNING;
    bool stop_accepted = !s_audio_bound;
    if (s_audio_bound) {
        if (stop_result == ESP_OK) {
            stop_accepted = true;
        } else if (stop_result == ESP_ERR_INVALID_STATE) {
            /*
             * Before engine entry we know the runtime is still BOUND. After
             * entry, INVALID_STATE is accepted only when the backend proves
             * it is already muted (normally because Doom's earlier LIFO
             * S_Shutdown callback stopped and joined the worker).
             */
            state_result = platform_audio_get_state(s_audio, &audio_state);
            stop_accepted = !s_engine_invoked ||
                (state_result == ESP_OK &&
                 audio_state == PLATFORM_AUDIO_STATE_READY_MUTED);
        }
    }

    esp_err_t unbind_result = ESP_ERR_INVALID_STATE;
    if (s_audio_bound && stop_accepted) {
        unbind_result = doom_audio_runtime_unbind();
        if (unbind_result == ESP_OK) {
            s_audio_bound = false;
        }
    }
    const bool no_worker_confirmed = !s_audio_bound;
    const bool destroy_was_needed = s_audio != NULL;
    esp_err_t destroy_result = ESP_ERR_INVALID_STATE;
    if (s_audio != NULL && no_worker_confirmed) {
        destroy_result = platform_audio_destroy(&s_audio);
    }
    const bool destroy_confirmed = !destroy_was_needed ||
        (destroy_result == ESP_OK && s_audio == NULL);

    /*
     * A failed create can retain an I2S owner without returning a handle.
     * Recover after the worker/handle path, and retain the display-owned
     * rails unless it positively proves that hidden ownership is gone.
     */
    const esp_err_t recover_result = platform_audio_recover();
    const bool retained = s_audio != NULL;
    const bool backend_released = no_worker_confirmed && s_audio == NULL &&
        destroy_confirmed && recover_result == ESP_OK;

    esp_err_t video_result = ESP_ERR_INVALID_STATE;
    esp_err_t dark_result = ESP_ERR_INVALID_STATE;
    esp_err_t display_result = ESP_ERR_INVALID_STATE;
    if (backend_released) {
        if (s_display_initialized) {
            dark_result = platform_display_set_brightness(0U);
        }
        if (s_video_initialized) {
            video_result = doom_video_deinit();
            if (video_result == ESP_OK) {
                s_video_initialized = false;
            }
        }
        if (!s_video_initialized && s_display_initialized) {
            display_result = platform_display_deinit();
            if (display_result == ESP_OK) {
                s_display_initialized = false;
            }
        }
    }

    ESP_LOGI(TAG,
             "P4_DOOM_AUDIO E2 CLEANUP force_safe=%s stop=%s "
             "state=%s audio_state=%u stop_accepted=%u unbind=%s "
             "destroy=%s destroy_confirmed=%u recover=%s retained=%u "
             "backend_released=%u "
             "rails_retained=%u "
             "dark=%s video=%s display=%s",
             esp_err_to_name(force_safe_result), esp_err_to_name(stop_result),
             esp_err_to_name(state_result), (unsigned)audio_state,
             stop_accepted ? 1U : 0U, esp_err_to_name(unbind_result),
             esp_err_to_name(destroy_result), destroy_confirmed ? 1U : 0U,
             esp_err_to_name(recover_result), retained ? 1U : 0U,
             backend_released ? 1U : 0U,
             backend_released ? 0U : 1U,
             esp_err_to_name(dark_result), esp_err_to_name(video_result),
             esp_err_to_name(display_result));
    s_cleanup_complete = backend_released && !retained &&
        !s_display_initialized && !s_video_initialized;
    s_cleanup_active = false;
}

static void halt_dark(const char *stage, esp_err_t error)
{
    composite_cleanup();
    ESP_LOGE(TAG, "P4_DOOM_AUDIO E2 HALT stage=%s error=%s",
             stage, esp_err_to_name(error));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        /* Retained workers/backends/rails stay live until cleanup can prove. */
        composite_cleanup();
    }
}

static void engine_exit_composite(void)
{
    composite_cleanup();
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

static void log_runtime_stats(void)
{
    platform_display_stats_t video_stats;
    doom_audio_runtime_stats_t audio_stats;
    const esp_err_t video_result = platform_display_get_stats(&video_stats);
    const esp_err_t audio_result = doom_audio_runtime_get_stats(&audio_stats);
    if (video_result == ESP_OK && audio_result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_DOOM_AUDIO E2 STATS frames=%" PRIu32
                 " submits=%" PRIu32 " completions=%" PRIu32
                 " video_timeouts=%" PRIu32 " video_failures=%" PRIu32
                 " refreshes=%" PRIu32 " commands=%" PRIu32
                 " command_drops=%" PRIu32 " audio_frames=%" PRIu32
                 " write_failures=%" PRIu32
                 " worker_stack_hwm_bytes=%" PRIu32
                 " underruns=unavailable",
                 s_frame_count, video_stats.submits_started,
                 video_stats.submits_completed, video_stats.submit_timeouts,
                 video_stats.submit_failures, video_stats.refresh_completions,
                 audio_stats.commands_enqueued, audio_stats.commands_dropped,
                 audio_stats.frames_rendered, audio_stats.write_failures,
                 audio_stats.worker_stack_hwm_bytes);
    }
}

void DG_Init(void)
{
    if (DG_ScreenBuffer == NULL) {
        halt_dark("engine-screen-buffer", ESP_ERR_NO_MEM);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_AUDIO E2 VIDEO_READY input=0x00RRGGBB "
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
    if (!s_engine_heap_logged) {
        log_memory_snapshot("post-engine-audio-init");
        log_runtime_stats();
        s_engine_heap_logged = true;
    }
    s_frame_error = doom_video_submit_xrgb8888(
        (const uint32_t *)DG_ScreenBuffer, DOOM_VIDEO_WIDTH,
        DOOM_SUBMIT_TIMEOUT_MS);
    if (s_frame_error != ESP_OK) {
        return;
    }
    ++s_frame_count;
    if ((s_frame_count % DOOM_STATS_INTERVAL_FRAMES) == 0U) {
        log_runtime_stats();
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
    esp_err_t err = platform_audio_force_safe_shutdown();
    if (err != ESP_OK) {
        halt_dark("audio-initial-safe", err);
    }

    ESP_LOGI(TAG,
             "P4_DOOM_AUDIO E2 START wad=embedded-exact vfs=read-only "
             "input=neutral audio=direct-i2s-16khz music=disabled "
             "sd=unused usb=unused i2c=unused codec=none");
    ESP_LOGI(TAG, "P4_DOOM_AUDIO E2 SAFE amp_gpio30=high");

    err = platform_display_init();
    if (err != ESP_OK) {
        halt_dark("display-init", err);
    }
    s_display_initialized = true;
    I_AtExit(engine_exit_composite, true);
    ESP_LOGI(TAG,
             "P4_DOOM_AUDIO E2 CLEANUP_REGISTERED order=audio-first-display-last");

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
             "P4_DOOM_AUDIO E2 WAD_VERIFIED identity=doom-shareware-1.9 "
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
             "P4_DOOM_AUDIO E2 VFS_READY path=%s mode=read-only max_open=%u",
             EMBEDDED_WAD_PATH,
             (unsigned)PLATFORM_READONLY_BLOB_MAX_OPEN_FILES);

    err = doom_video_init();
    if (err != ESP_OK) {
        halt_dark("video-adapter-init", err);
    }
    s_video_initialized = true;
    err = doom_video_submit_black(DOOM_FIRST_FRAME_TIMEOUT_MS);
    if (err != ESP_OK) {
        halt_dark("first-black-frame", err);
    }

    log_memory_snapshot("pre-audio-create");
    const platform_audio_config_t audio_config = {
        .control_bus = NULL,
        .sample_rate_hz = (uint32_t)DOOM_AUDIO_OUTPUT_RATE_HZ,
        .volume_percent = 10U,
    };
    err = platform_audio_create(&audio_config, &s_audio);
    if (err != ESP_OK) {
        halt_dark("audio-create", err);
    }
    log_memory_snapshot("post-audio-create");

    const doom_audio_runtime_config_t doom_audio_config = {
        .platform_audio = s_audio,
        .sample_rate_hz = (uint32_t)DOOM_AUDIO_OUTPUT_RATE_HZ,
        .display_owns_ldo3_ldo4 = true,
    };
    err = doom_audio_runtime_bind(&doom_audio_config);
    if (err != ESP_OK) {
        halt_dark("audio-bind", err);
    }
    s_audio_bound = true;

    err = platform_display_set_brightness(25U);
    if (err != ESP_OK) {
        halt_dark("backlight", err);
    }

    char *argv[] = {
        "doom",
        "-iwad",
        EMBEDDED_WAD_PATH,
        "-gfxmode",
        "rgba8888",
        "-nomusic",
    };
    const int argc = (int)(sizeof(argv) / sizeof(argv[0]));
    ESP_LOGI(TAG,
             "P4_DOOM_AUDIO E2 ENGINE_START gfxmode=rgba8888 wad=%s "
             "sfx=enabled music=disabled audio_bound=1 "
             "video_seam=xrgb8888_to_rgb565",
             EMBEDDED_WAD_PATH);
    s_engine_invoked = true;
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
