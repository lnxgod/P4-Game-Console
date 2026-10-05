// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Isolated E6 Doom composite. Its three app-local source-bound runtime bytes
 * select the exact 1/1/1 touch-and-factory-audio successor. This source is
 * build-only while the board profile's GPIO30-low electrical release remains
 * closed; repository metadata and the central verifier deny every flash/run
 * route. The branch preserves E1's exact WAD/display/video path, routes every
 * audio call through the counted app-local adapter. The standalone successor
 * keeps controller transports absent; the Waveshare Console OS integration
 * consumes the shared platform gamepad snapshot without taking ownership of
 * USB Host or BLE HID.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "degraded_policy.h"
#include "audio_lifecycle.h"
#include "doom/audio_mixer.h"
#include "doom/audio_runtime.h"
#include "doom/video.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_system.h"
#include "m_controls.h"
#include "p4_doom_net.h"
#include "runtime_gate.h"
#include "touch_controls.h"
#ifdef P4_CONSOLE_OS_EMBEDDED
#include "p4/doom_multiplayer.h"
#endif
#if defined(P4_CONSOLE_OS_EMBEDDED) && \
    defined(CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3) && \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
#define P4_DOOM_SHARED_GAMEPAD 1
#include "doom_gamepad/input.h"
#else
#define P4_DOOM_SHARED_GAMEPAD 0
#endif
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#pragma GCC diagnostic pop
#include "platform/audio.h"
#include "platform/board.h"
#include "platform/display.h"
#include "platform/readonly_blob.h"
#include "platform/touch.h"
#include "platform_i2c_shared/bus.h"
#ifdef P4_CONSOLE_OS_EMBEDDED
#include "platform/game_storage.h"
#endif
#if P4_DOOM_SHARED_GAMEPAD
#include "platform/gamepad.h"
#endif

#define DOOM_SUBMIT_TIMEOUT_MS UINT32_C(100)
#define DOOM_FIRST_FRAME_TIMEOUT_MS UINT32_C(250)
#define DOOM_MAX_SLEEP_MS UINT32_C(60000)
#define DOOM_STATS_INTERVAL_FRAMES UINT32_C(150)
#define DOOM_BACKEND_VOLUME_DEFAULT_STEP UINT8_C(8)
#define DOOM_BACKEND_VOLUME_MIN_STEP UINT8_C(1)
#define DOOM_BACKEND_VOLUME_MAX_STEP UINT8_C(10)
#define TOUCH_POLL_INTERVAL_MS UINT32_C(16)
#define TOUCH_RETRY_INTERVAL_MS UINT32_C(5000)
#define TOUCH_DEGRADED_LOG_INTERVAL_MS UINT32_C(2000)
#define EMBEDDED_WAD_BYTES ((size_t)4196020U)
#define EMBEDDED_WAD_PATH "/doom/doom1.wad"
#define OVERLAY_PIXELS \
    ((size_t)DOOM_TOUCH_FRAME_WIDTH * (size_t)DOOM_TOUCH_FRAME_HEIGHT)

#ifndef P4_CONSOLE_OS_EMBEDDED
extern const uint8_t _binary_doom_shareware_wad_start[];
extern const uint8_t _binary_doom_shareware_wad_end[];
static const uint8_t s_expected_wad_sha256[32] = {
    0x1d, 0x7d, 0x43, 0xbe, 0x50, 0x1e, 0x67, 0xd9,
    0x27, 0xe4, 0x15, 0xe0, 0xb8, 0xf3, 0xe2, 0x9c,
    0x3b, 0xf3, 0x30, 0x75, 0xe8, 0x59, 0x72, 0x18,
    0x16, 0xf6, 0x52, 0xa5, 0x26, 0xca, 0xc7, 0x71,
};
#else
static const uint8_t s_console_storage_wad_marker;
#endif

static const char *const TAG = "p4_doom_touch_audio";
static esp_err_t s_frame_error = ESP_OK;
static uint32_t s_frame_count;
static uint32_t *s_overlay_buffer;
static uint8_t s_backend_volume_step = DOOM_BACKEND_VOLUME_DEFAULT_STEP;

static doom_touch_input_t s_touch_input;
static platform_i2c_shared_t *s_shared_bus;
static platform_touch_t *s_touch;
static bool s_touch_ready;
static bool s_touch_cleanup_proven = true;
static uint32_t s_last_touch_poll_ms;
static uint32_t s_last_touch_retry_ms;
static uint32_t s_last_touch_degraded_log_ms;
static uint32_t s_touch_polls;
static uint32_t s_touch_poll_failures;
static uint32_t s_touch_retries;

#if P4_DOOM_SHARED_GAMEPAD
static doom_gamepad_input_t s_gamepad_input;
static bool s_gamepad_connection_known;
static uint8_t s_gamepad_connected;
static uint32_t s_gamepad_session;
static uint32_t s_gamepad_polls;
static uint32_t s_gamepad_poll_failures;
#endif

static bool s_audio_gate_enabled;
static bool s_audio_calls_allowed;
static doom_e6_audio_lifecycle_t s_audio_lifecycle = {
    .released = true,
};
static doom_touch_audio_runtime_gate_t s_runtime_gate;

static bool s_display_initialized;
static bool s_video_initialized;
static bool s_blob_registered;
static bool s_cleanup_active;
static bool s_cleanup_complete;

#ifdef P4_CONSOLE_OS_EMBEDDED
static bool s_console_os_launch_active;
#endif

_Static_assert(sizeof(pixel_t) == sizeof(uint32_t),
               "E5 requires the engine's default 32-bit pixels");
_Static_assert(DOOM_AUDIO_OUTPUT_RATE_HZ == PLATFORM_AUDIO_SAMPLE_RATE_HZ,
               "Doom mixer and factory backend rates must remain aligned");
_Static_assert(PLATFORM_TOUCH_WIDTH == DOOM_TOUCH_SCREEN_WIDTH &&
                   PLATFORM_TOUCH_HEIGHT == DOOM_TOUCH_SCREEN_HEIGHT,
               "touch and overlay coordinate spaces must match");

static uint32_t ticks_ms(void)
{
    return (uint32_t)((uint64_t)esp_timer_get_time() / UINT64_C(1000));
}

static void log_sound_disabled(void)
{
    const uint32_t audio_calls = platform_audio_invocation_count();
    if (platform_board_kind() == PLATFORM_BOARD_M5STACK_TAB5) {
        ESP_LOGI(TAG,"P4_DOOM SOUND_DISABLED board=m5stack-tab5 amp=expander-0x43-p1 audio_calls=%" PRIu32,audio_calls);
        return;
    } else if (platform_board_kind() == PLATFORM_BOARD_WAVESHARE_4_3) {
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 SOUND_DISABLED audio_gate=%u "
                 "audio_calls=%" PRIu32 " amp_gpio=53 state=%s",
                 (unsigned)s_runtime_gate.audio_authorized,
                 audio_calls,
                 s_runtime_gate.audio_authorized == 0U && audio_calls == 0U
                     ? "untouched" : "not-proven");
        return;
    }
    const char *const gpio30_state =
        s_runtime_gate.audio_authorized == 0U && audio_calls == 0U
            ? "untouched" : "not-proven";
    ESP_LOGI(TAG,
             "P4_DOOM_E6 SOUND_DISABLED audio_gate=%u audio_calls=%" PRIu32
             " gpio30=%s hardware_pullup=R71",
             (unsigned)s_runtime_gate.audio_authorized,
             audio_calls, gpio30_state);
}

#ifndef P4_CONSOLE_OS_EMBEDDED
static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) |
        ((uint32_t)bytes[3] << 24U);
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
    if (lump_count == 0U ||
        (uint64_t)directory_offset > (uint64_t)size_bytes ||
        directory_bytes >
            (uint64_t)size_bytes - (uint64_t)directory_offset) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t digest[32];
    if (mbedtls_sha256(data, size_bytes, digest, 0) != 0) {
        return ESP_FAIL;
    }
    return memcmp(digest, s_expected_wad_sha256, sizeof(digest)) == 0
        ? ESP_OK
        : ESP_ERR_INVALID_CRC;
}
#endif

static esp_err_t verify_readonly_vfs(
    const char *wad_path, size_t wad_bytes, bool allow_pwad)
{
    struct stat metadata;
    if (wad_path == NULL || stat(wad_path, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) ||
        metadata.st_size != (off_t)wad_bytes ||
        (metadata.st_mode & 0222) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    FILE *const file = fopen(wad_path, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    uint8_t header[12];
    esp_err_t result = ESP_OK;
    if (fread(header, 1U, sizeof(header), file) != sizeof(header) ||
        (memcmp(header, "IWAD", 4U) != 0 &&
         (!allow_pwad || memcmp(header, "PWAD", 4U) != 0)) ||
        fseek(file, 0L, SEEK_END) != 0 ||
        ftell(file) != (long)wad_bytes) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (fclose(file) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
    }
    return result;
}

static void log_touch_degraded(const char *stage, esp_err_t error,
                               bool force)
{
    const uint32_t now_ms = ticks_ms();
    if (force ||
        (uint32_t)(now_ms - s_last_touch_degraded_log_ms) >=
            TOUCH_DEGRADED_LOG_INTERVAL_MS) {
        ESP_LOGW(TAG,
                 "P4_DOOM_E6 TOUCH_DEGRADED stage=%s error=%s neutral=1 "
                 "overlay=visible cleanup_proven=%u bus_owned=%u",
                 stage, esp_err_to_name(error),
                 s_touch_cleanup_proven ? 1U : 0U,
                 s_shared_bus != NULL ? 1U : 0U);
        s_last_touch_degraded_log_ms = now_ms;
    }
}

static void neutralize_touch_input(void)
{
    (void)doom_touch_audio_force_neutral(&s_touch_input);
}

static bool release_retained_touch(void)
{
    if (s_touch == NULL) {
        s_touch_ready = false;
        s_touch_cleanup_proven = true;
        return true;
    }
    const esp_err_t result = platform_touch_destroy(&s_touch);
    if (result != ESP_OK) {
        s_touch_ready = false;
        s_touch_cleanup_proven = false;
        log_touch_degraded("touch-destroy", result, true);
        return false;
    }
    s_touch_ready = false;
    s_touch_cleanup_proven = true;
    return true;
}

static bool try_touch_create(bool force_log)
{
    ++s_touch_retries;
    if (s_touch != NULL && !s_touch_ready && !release_retained_touch()) {
        return false;
    }
    if (s_shared_bus == NULL) {
        const esp_err_t bus_result =
            platform_i2c_shared_create(&s_shared_bus);
        if (bus_result != ESP_OK) {
            log_touch_degraded("shared-i2c-create", bus_result, force_log);
            return false;
        }
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 SHARED_I2C_READY port=%d sda=%d scl=%d "
                 "hz=%u owner=app borrowers=touch-only",
                 PLATFORM_BOARD_I2C_PORT,
                 PLATFORM_BOARD_I2C_SDA_GPIO,
                 PLATFORM_BOARD_I2C_SCL_GPIO,
                 (unsigned)PLATFORM_I2C_SHARED_CLOCK_HZ);
    }

    platform_touch_config_t config;
    platform_touch_config_init(
        &config, platform_i2c_shared_handle(s_shared_bus));
    const esp_err_t result = platform_touch_create(&config, &s_touch);
    if (result != ESP_OK) {
        s_touch_ready = false;
        s_touch_cleanup_proven = s_touch == NULL;
        if (s_touch != NULL) {
            (void)release_retained_touch();
        }
        log_touch_degraded("gt911-create", result, force_log);
        return false;
    }
    s_touch_ready = true;
    s_touch_cleanup_proven = true;
    ESP_LOGI(TAG,
             "P4_DOOM_E6 TOUCH_READY primary=0x%02x fallback=0x%02x "
             "logical=%ux%u native=%ux%u rotation_cw=%u contacts=%u "
             "poll_ms=%u i2c_device_hz=%u reset_gpio=%d int_gpio=%d "
             "interrupt_callback=none",
             (unsigned)config.address_7bit,
             (unsigned)PLATFORM_TOUCH_GT911_BACKUP_ADDRESS,
             (unsigned)PLATFORM_TOUCH_WIDTH,
             (unsigned)PLATFORM_TOUCH_HEIGHT,
             (unsigned)PLATFORM_TOUCH_NATIVE_WIDTH,
             (unsigned)PLATFORM_TOUCH_NATIVE_HEIGHT,
             (unsigned)PLATFORM_TOUCH_ROTATION_CW_DEGREES,
             (unsigned)PLATFORM_TOUCH_MAX_CONTACTS,
             (unsigned)TOUCH_POLL_INTERVAL_MS,
             (unsigned)PLATFORM_TOUCH_I2C_CLOCK_HZ,
             PLATFORM_TOUCH_RESET_GPIO,
             PLATFORM_TOUCH_INTERRUPT_GPIO);
    return true;
}

static void service_touch(void)
{
    if (!doom_touch_input_idle(&s_touch_input)) {
        return;
    }
    const uint32_t now_ms = ticks_ms();
    if (!s_touch_ready) {
        neutralize_touch_input();
        const bool bus_state_proven =
            s_shared_bus != NULL || s_touch == NULL;
        if (doom_touch_audio_retry_due(
                now_ms, s_last_touch_retry_ms, TOUCH_RETRY_INTERVAL_MS,
                s_touch_cleanup_proven, bus_state_proven)) {
            s_last_touch_retry_ms = now_ms;
            (void)try_touch_create(false);
        }
        return;
    }
    if ((uint32_t)(now_ms - s_last_touch_poll_ms) <
        TOUCH_POLL_INTERVAL_MS) {
        return;
    }
    s_last_touch_poll_ms = now_ms;
    ++s_touch_polls;

    platform_touch_frame_t platform_frame;
    const esp_err_t result = platform_touch_poll(s_touch, &platform_frame);
    doom_touch_frame_t touch_frame;
    const bool converted = doom_touch_audio_frame_from_platform(
        result == ESP_OK ? &platform_frame : NULL, &touch_frame);
    if (result != ESP_OK || !converted) {
        ++s_touch_poll_failures;
        neutralize_touch_input();
        log_touch_degraded("gt911-poll",
                           result != ESP_OK ? result
                                            : ESP_ERR_INVALID_RESPONSE,
                           false);
        s_touch_ready = false;
        s_last_touch_retry_ms = now_ms;
        (void)release_retained_touch();
        return;
    }
    (void)doom_touch_input_update(&s_touch_input, &touch_frame);
}

#if P4_DOOM_SHARED_GAMEPAD
static bool doom_gamepad_action_key(uint8_t action, unsigned char *key)
{
    if (key == NULL) {
        return false;
    }
    switch ((doom_gamepad_action_t)action) {
    case DOOM_GAMEPAD_ACTION_UP:
        *key = (unsigned char)KEY_UPARROW;
        return true;
    case DOOM_GAMEPAD_ACTION_DOWN:
        *key = (unsigned char)KEY_DOWNARROW;
        return true;
    case DOOM_GAMEPAD_ACTION_LEFT:
        *key = (unsigned char)KEY_LEFTARROW;
        return true;
    case DOOM_GAMEPAD_ACTION_RIGHT:
        *key = (unsigned char)KEY_RIGHTARROW;
        return true;
    case DOOM_GAMEPAD_ACTION_FIRE:
        *key = (unsigned char)KEY_FIRE;
        return true;
    case DOOM_GAMEPAD_ACTION_USE:
        *key = (unsigned char)KEY_USE;
        return true;
    case DOOM_GAMEPAD_ACTION_RUN:
        *key = (unsigned char)KEY_RSHIFT;
        return true;
    case DOOM_GAMEPAD_ACTION_STRAFE:
        *key = (unsigned char)KEY_RALT;
        return true;
    case DOOM_GAMEPAD_ACTION_STRAFE_LEFT:
        *key = (unsigned char)KEY_STRAFE_L;
        return true;
    case DOOM_GAMEPAD_ACTION_STRAFE_RIGHT:
        *key = (unsigned char)KEY_STRAFE_R;
        return true;
    case DOOM_GAMEPAD_ACTION_MENU_ACCEPT:
        *key = (unsigned char)KEY_ENTER;
        return true;
    case DOOM_GAMEPAD_ACTION_MENU_BACK:
        *key = (unsigned char)KEY_ESCAPE;
        return true;
    case DOOM_GAMEPAD_ACTION_MAP:
        *key = (unsigned char)KEY_TAB;
        return true;
    case DOOM_GAMEPAD_ACTION_WEAPON_NEXT:
        *key = (unsigned char)']';
        return true;
    case DOOM_GAMEPAD_ACTION_WEAPON_PREVIOUS:
        *key = (unsigned char)'[';
        return true;
    case DOOM_GAMEPAD_ACTION_PAUSE:
        *key = (unsigned char)KEY_PAUSE;
        return true;
    case DOOM_GAMEPAD_ACTION_COUNT:
    default:
        return false;
    }
}

static bool gamepad_snapshot_valid(
    const platform_gamepad_snapshot_t *snapshot)
{
    return snapshot != NULL &&
        snapshot->version == PLATFORM_GAMEPAD_SNAPSHOT_VERSION &&
        snapshot->size == sizeof(*snapshot) &&
        snapshot->state.version == GAMEPAD_STATE_VERSION &&
        snapshot->state.size == sizeof(snapshot->state) &&
        snapshot->state.connected <= 1U &&
        (snapshot->state.dpad &
         ~(GAMEPAD_DPAD_UP | GAMEPAD_DPAD_RIGHT | GAMEPAD_DPAD_DOWN |
           GAMEPAD_DPAD_LEFT)) == 0U;
}

static void service_gamepad(void)
{
    if (!doom_gamepad_input_idle(&s_gamepad_input)) {
        return;
    }
    if (s_gamepad_polls != UINT32_MAX) {
        ++s_gamepad_polls;
    }

    platform_gamepad_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    const esp_err_t result = platform_gamepad_get_snapshot(&snapshot);
    const bool valid = result == ESP_OK && gamepad_snapshot_valid(&snapshot);
    gamepad_state_t neutral;
    gamepad_state_init(&neutral);
    const gamepad_state_t *const state = valid ? &snapshot.state : &neutral;
    const uint32_t session = valid ? snapshot.session : 0U;

    if (!valid) {
        if (s_gamepad_poll_failures != UINT32_MAX) {
            ++s_gamepad_poll_failures;
        }
        if (s_gamepad_poll_failures == 1U ||
            (s_gamepad_poll_failures % 300U) == 0U) {
            ESP_LOGW(TAG,
                     "P4_DOOM_E6 GAMEPAD_POLL_FAIL count=%" PRIu32
                     " error=%s state=neutral",
                     s_gamepad_poll_failures, esp_err_to_name(result));
        }
    }

    if (!s_gamepad_connection_known ||
        state->connected != s_gamepad_connected ||
        session != s_gamepad_session) {
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 GAMEPAD session=%" PRIu32
                 " connected=%u sequence=%" PRIu32
                 " source=shared-platform-snapshot",
                 session, (unsigned)state->connected, state->sequence);
        s_gamepad_connection_known = true;
        s_gamepad_connected = state->connected;
        s_gamepad_session = session;
    }

    const gamepad_status_t input_result =
        doom_gamepad_input_update(&s_gamepad_input, state);
    if (input_result != GAMEPAD_OK) {
        ESP_LOGW(TAG,
                 "P4_DOOM_E6 GAMEPAD_UPDATE_FAIL error=%s state=neutral",
                 gamepad_status_name(input_result));
        doom_gamepad_input_init(&s_gamepad_input);
    }
}
#endif

static bool release_audio(void)
{
    doom_e6_audio_release_result_t release_result;
    const esp_err_t result = doom_e6_audio_release(
        &s_audio_lifecycle, &release_result);
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_DOOM_E6 AUDIO_SAFETY_FAULT stage=cleanup "
                 "stop=%s unbind=%s destroy=%s recover=%s "
                 "runtime_bound=%u handle_retained=%u safe_high=%u",
                 esp_err_to_name(release_result.stop_result),
                 esp_err_to_name(release_result.unbind_result),
                 esp_err_to_name(release_result.destroy_result),
                 esp_err_to_name(release_result.recover_result),
                 s_audio_lifecycle.runtime_bound ? 1U : 0U,
                 s_audio_lifecycle.audio != NULL ? 1U : 0U,
                 s_audio_lifecycle.safe_high_proven ? 1U : 0U);
    }
    return release_result.complete;
}

static void composite_cleanup(void)
{
    if (s_cleanup_active || s_cleanup_complete) {
        return;
    }
    s_cleanup_active = true;
    const bool audio_released = release_audio();
    const bool touch_released = release_retained_touch();

    esp_err_t bus_result = ESP_OK;
    if (touch_released && s_shared_bus != NULL) {
        bus_result = platform_i2c_shared_destroy(&s_shared_bus);
    }
    const bool bus_released = s_shared_bus == NULL;

    esp_err_t dark_result = ESP_OK;
    if (s_display_initialized) {
        dark_result = platform_display_set_brightness(0U);
    }
    esp_err_t video_result = ESP_OK;
    if (audio_released && bus_released && s_video_initialized) {
        video_result = doom_video_deinit();
        if (video_result == ESP_OK) {
            s_video_initialized = false;
            free(s_overlay_buffer);
            s_overlay_buffer = NULL;
        }
    }
    esp_err_t display_result = ESP_OK;
    if (audio_released && bus_released && !s_video_initialized &&
        s_display_initialized) {
        display_result = platform_display_deinit();
        if (display_result == ESP_OK) {
            s_display_initialized = false;
        }
    }
    esp_err_t blob_result = ESP_OK;
    if (audio_released && bus_released && !s_video_initialized &&
        !s_display_initialized && s_blob_registered) {
        blob_result = platform_readonly_blob_unregister();
        if (blob_result == ESP_OK) {
            s_blob_registered = false;
        }
    }
    s_cleanup_complete = audio_released && touch_released && bus_released &&
        !s_video_initialized && !s_display_initialized && !s_blob_registered;
    ESP_LOGI(TAG,
             "P4_DOOM_E6 CLEANUP audio_released=%u touch_released=%u "
             "bus=%s dark=%s video=%s display=%s blob=%s complete=%u "
             "retained=%u",
             audio_released ? 1U : 0U, touch_released ? 1U : 0U,
             esp_err_to_name(bus_result), esp_err_to_name(dark_result),
             esp_err_to_name(video_result), esp_err_to_name(display_result),
             esp_err_to_name(blob_result),
             s_cleanup_complete ? 1U : 0U,
             s_cleanup_complete ? 0U : 1U);
    s_cleanup_active = false;
}

static void halt_dark(const char *stage, esp_err_t error)
{
    composite_cleanup();
    ESP_LOGE(TAG, "P4_DOOM_E6 HALT stage=%s error=%s",
             stage, esp_err_to_name(error));
#ifdef P4_CONSOLE_OS_EMBEDDED
    if (s_console_os_launch_active && s_cleanup_complete) {
        ESP_LOGE(TAG,
                 "P4_DOOM_E6 RECOVERY action=restart-to-console "
                 "stage=%s delay_ms=500",
                 stage);
        vTaskDelay(pdMS_TO_TICKS(500U));
        esp_restart();
    }
#endif
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
        composite_cleanup();
    }
}

static esp_err_t submit_startup_frame(void)
{
    if (s_overlay_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t y = 0U; y < DOOM_TOUCH_FRAME_HEIGHT; ++y) {
        for (size_t x = 0U; x < DOOM_TOUCH_FRAME_WIDTH; ++x) {
            const bool border = x < 4U || x >= DOOM_TOUCH_FRAME_WIDTH - 4U ||
                y < 4U || y >= DOOM_TOUCH_FRAME_HEIGHT - 4U;
            const bool scanline = (y % 16U) == 0U;
            const bool center_bar = y >= 94U && y < 106U &&
                x >= 48U && x < DOOM_TOUCH_FRAME_WIDTH - 48U;
            s_overlay_buffer[y * DOOM_TOUCH_FRAME_WIDTH + x] = border
                ? UINT32_C(0x0000ffff)
                : center_bar
                    ? UINT32_C(0x000080ff)
                    : scanline
                        ? UINT32_C(0x00001838)
                        : UINT32_C(0x00000818);
        }
    }
    return doom_video_submit_xrgb8888(
        s_overlay_buffer, DOOM_VIDEO_WIDTH, DOOM_FIRST_FRAME_TIMEOUT_MS);
}

static void engine_exit_composite(void)
{
    composite_cleanup();
}

static bool try_audio_enable(void)
{
    if (!s_audio_calls_allowed) {
        if (!s_audio_gate_enabled) {
            log_sound_disabled();
        }
        return false;
    }
    const platform_audio_config_t config = {
        .control_bus = platform_board_kind() == PLATFORM_BOARD_WAVESHARE_4_3
            ? (void *)platform_i2c_shared_handle(s_shared_bus)
            : NULL,
        .sample_rate_hz = (uint32_t)DOOM_AUDIO_OUTPUT_RATE_HZ,
        .volume_percent = s_backend_volume_step,
    };
    s_audio_lifecycle.released = false;
    esp_err_t result = platform_audio_create(
        &config, &s_audio_lifecycle.audio);
    if (result != ESP_OK) {
        doom_e6_audio_release_result_t release_result;
        const esp_err_t recover_result = doom_e6_audio_release(
            &s_audio_lifecycle, &release_result);
        if (recover_result != ESP_OK) {
            ESP_LOGE(TAG,
                     "P4_DOOM_E6 AUDIO_SAFETY_FAULT "
#if CONFIG_P4_BOARD_M5STACK_TAB5
                     "stage=tab5-audio-create error=%s recover=%s halt=1",
#else
                     "stage=factory-audio-create error=%s recover=%s halt=1",
#endif
                     esp_err_to_name(result),
                     esp_err_to_name(recover_result));
            halt_dark("audio-create-safety", recover_result);
        }
        ESP_LOGW(TAG,
#if CONFIG_P4_BOARD_M5STACK_TAB5
                 "P4_DOOM_E6 SOUND_DEGRADED stage=tab5-audio-create "
#else
                 "P4_DOOM_E6 SOUND_DEGRADED stage=factory-audio-create "
#endif
                 "error=%s recover=%s fallback=silent auto_fallback=0",
                 esp_err_to_name(result), esp_err_to_name(recover_result));
        return false;
    }
    const doom_audio_runtime_config_t runtime_config = {
        .platform_audio = s_audio_lifecycle.audio,
        .sample_rate_hz = (uint32_t)DOOM_AUDIO_OUTPUT_RATE_HZ,
        .display_owns_ldo3_ldo4 = true,
    };
    result = doom_audio_runtime_bind(&runtime_config);
    if (result != ESP_OK) {
        doom_e6_audio_release_result_t release_result;
        const esp_err_t recover_result = doom_e6_audio_release(
            &s_audio_lifecycle, &release_result);
        if (recover_result != ESP_OK) {
            ESP_LOGE(TAG,
                     "P4_DOOM_E6 AUDIO_SAFETY_FAULT "
                     "stage=doom-audio-bind error=%s recover=%s halt=1",
                     esp_err_to_name(result),
                     esp_err_to_name(recover_result));
            halt_dark("audio-bind-safety", recover_result);
        }
        ESP_LOGW(TAG,
                 "P4_DOOM_E6 SOUND_DEGRADED stage=doom-audio-bind "
                 "error=%s recover=%s fallback=silent auto_fallback=0",
                 esp_err_to_name(result), esp_err_to_name(recover_result));
        return false;
    }
    s_audio_lifecycle.runtime_bound = true;
    if (platform_board_kind() == PLATFORM_BOARD_M5STACK_TAB5) {
        ESP_LOGI(TAG,"P4_DOOM SOUND_BOUND backend=tab5-es8388 rate_hz=16000 format=pcm16-stereo amp=expander-0x43-p1 codec_addr=0x10");
    } else if (platform_board_kind() == PLATFORM_BOARD_WAVESHARE_4_3) {
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 SOUND_BOUND backend=waveshare-es8311 "
                 "state=ready-muted amp_gpio=53 safe_level=low "
                 "speaker_i2s_port=1 rate_hz=%u format=pcm16-stereo "
                 "channels=%u mclk_gpio=13 bclk_gpio=12 lrclk_gpio=10 "
                 "dout_gpio=9 codec_i2c_addr=0x18 codec_volume=%u/100 "
                 "pcm_gain=unity required_startup_zero_ms=350 "
                 "music=wad-mus synth=procedural-16voice "
                 "activation=doom-sfx-init-pending audio_calls=%" PRIu32,
                 (unsigned)DOOM_AUDIO_OUTPUT_RATE_HZ,
                 (unsigned)PLATFORM_AUDIO_CHANNEL_COUNT,
                 (unsigned)s_backend_volume_step * 10U,
                 platform_audio_invocation_count());
    } else {
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 SOUND_BOUND backend=factory-complete-audio-init "
                 "state=ready-muted gpio30=high-pad-readback-proven "
                 "pdm_i2s_port=0 pdm_clk_gpio=24 pdm_clk_hz=1024000 "
                 "pdm_din_gpio=26 gpio24_may_feed_codec_mclk=1 "
                 "speaker_i2s_port=1 rate_hz=%u format=pcm16-stereo "
                 "channels=%u lrclk_gpio=21 bclk_gpio=22 dout_gpio=23 "
                 "tx_mclk=none codec_i2c_transactions=0 "
                 "required_startup_zero_ms=350 "
                 "backend_volume_step=%u/10 gain=volume-step-linear-pcm "
                 "music=wad-mus synth=procedural-16voice "
                 "activation=doom-sfx-init-pending audio_calls=%" PRIu32,
                 (unsigned)DOOM_AUDIO_OUTPUT_RATE_HZ,
                 (unsigned)PLATFORM_AUDIO_CHANNEL_COUNT,
                 (unsigned)s_backend_volume_step,
                 platform_audio_invocation_count());
    }
    return true;
}

static void log_runtime_stats(void)
{
    platform_display_stats_t video_stats;
    if (platform_display_get_stats(&video_stats) != ESP_OK) {
        return;
    }
    doom_audio_runtime_stats_t audio_stats = {0};
    const bool have_audio_stats = s_audio_lifecycle.runtime_bound &&
        doom_audio_runtime_get_stats(&audio_stats) == ESP_OK;
    platform_audio_adapter_stats_t adapter_stats = {0};
    platform_audio_adapter_get_stats(&adapter_stats);
    platform_audio_telemetry_t backend_stats = {0};
    const bool have_backend_stats =
        platform_audio_get_telemetry(&backend_stats) == ESP_OK;
    const char *const start_proof =
        adapter_stats.running_low_readback_proven_at_start
            ? "low-readback-proven-at-start" : "not-proven";
    ESP_LOGI(TAG,
             "P4_DOOM_E6 STATS frames=%" PRIu32 " submits=%" PRIu32
             " completions=%" PRIu32 " video_timeouts=%" PRIu32
             " video_failures=%" PRIu32
             " video_accelerated=%" PRIu32
             " video_accelerator_failures=%" PRIu32
             " touch_polls=%" PRIu32
             " touch_failures=%" PRIu32 " touch_retries=%" PRIu32
             " composite_gate=%u touch_gate=%u audio_gate=%u"
             " audio_mutating_calls=%" PRIu32
             " audio_telemetry_snapshot_valid=%u audio_start_proof=%s"
             " audio_write_calls=%" PRIu32
             " audio_frames_forwarded=%" PRIu32
             " audio_nonzero_frames=%" PRIu32
             " audio_nonzero_samples=%" PRIu32
             " audio_peak=%u backend_telemetry_valid=%u"
             " backend_snapshot_sequence=%" PRIu32
             " backend_gpio30_high_attempts=%" PRIu32
             " backend_gpio30_high_successes=%" PRIu32
             " backend_gpio30_high_readbacks=%" PRIu32
             " backend_state=%u backend_running=%u"
             " pdm_created=%u pdm_enabled=%u"
             " pdm_create_successes=%" PRIu32
             " pdm_enable_successes=%" PRIu32
             " tx_created=%u tx_enabled=%u"
             " tx_create_successes=%" PRIu32
             " tx_enable_successes=%" PRIu32
             " zero_preload_frames=%" PRIu32
             " gpio30_low_attempts=%" PRIu32
             " gpio30_low_successes=%" PRIu32
             " gpio30_low_initial_readbacks=%" PRIu32
             " measured_settle_us=%" PRIu32
             " gpio30_low_second_readbacks=%" PRIu32
             " backend_write_successes=%" PRIu32
             " backend_write_failures=%" PRIu32
             " backend_frames_written=%" PRIu32
             " backend_samples_written=%" PRIu32
             " backend_nonzero_frames=%" PRIu32
             " backend_nonzero_samples=%" PRIu32
             " backend_max_abs=%" PRIu32
             " backend_rollback_attempts=%" PRIu32
             " backend_rollback_successes=%" PRIu32
             " backend_rollback_high_proofs=%" PRIu32
             " backend_resources_retained=%u backend_resources_owned=%" PRIu32
             " audio_frames=%" PRIu32 " audio_write_failures=%" PRIu32
             " audio_worker_stack_hwm=%" PRIu32
             " music_playing=%u music_paused=%u"
             " music_songs=%" PRIu32 " music_events=%" PRIu32
             " music_notes=%" PRIu32 " music_loops=%" PRIu32
             " music_frames=%" PRIu32
             " music_parse_failures=%" PRIu32
             " music_peak=%" PRIu32,
             s_frame_count, video_stats.submits_started,
             video_stats.submits_completed, video_stats.submit_timeouts,
             video_stats.submit_failures,
             video_stats.accelerated_submits,
             video_stats.accelerator_failures, s_touch_polls,
             s_touch_poll_failures, s_touch_retries,
             (unsigned)s_runtime_gate.composite_authorized,
             (unsigned)s_runtime_gate.touch_authorized,
             (unsigned)s_runtime_gate.audio_authorized,
             adapter_stats.invocations, have_backend_stats ? 1U : 0U,
             start_proof,
             adapter_stats.write_calls_succeeded,
             adapter_stats.frames_forwarded,
             adapter_stats.nonzero_frames_forwarded,
             adapter_stats.nonzero_samples_forwarded,
             (unsigned)adapter_stats.observed_absolute_peak,
             have_backend_stats ? 1U : 0U,
             have_backend_stats ? backend_stats.snapshot_sequence : 0U,
             have_backend_stats ? backend_stats.gpio30_high_attempts : 0U,
             have_backend_stats ? backend_stats.gpio30_high_successes : 0U,
             have_backend_stats
                 ? backend_stats.gpio30_high_readback_successes : 0U,
             have_backend_stats ? (unsigned)backend_stats.state : 2U,
             have_backend_stats && backend_stats.running ? 1U : 0U,
             have_backend_stats && backend_stats.pdm_created ? 1U : 0U,
             have_backend_stats && backend_stats.pdm_enabled ? 1U : 0U,
             have_backend_stats ? backend_stats.pdm_create_successes : 0U,
             have_backend_stats ? backend_stats.pdm_enable_successes : 0U,
             have_backend_stats && backend_stats.tx_created ? 1U : 0U,
             have_backend_stats && backend_stats.tx_enabled ? 1U : 0U,
             have_backend_stats ? backend_stats.tx_create_successes : 0U,
             have_backend_stats ? backend_stats.tx_enable_successes : 0U,
             have_backend_stats ? backend_stats.zero_preload_frames : 0U,
             have_backend_stats ? backend_stats.gpio30_low_attempts : 0U,
             have_backend_stats ? backend_stats.gpio30_low_successes : 0U,
             have_backend_stats
                 ? backend_stats.gpio30_low_initial_readback_successes : 0U,
             have_backend_stats ? backend_stats.measured_settle_us : 0U,
             have_backend_stats
                 ? backend_stats.gpio30_low_second_readback_successes : 0U,
             have_backend_stats ? backend_stats.write_successes : 0U,
             have_backend_stats ? backend_stats.write_failures : UINT32_MAX,
             have_backend_stats ? backend_stats.frames_written : 0U,
             have_backend_stats ? backend_stats.samples_written : 0U,
             have_backend_stats ? backend_stats.nonzero_frames : 0U,
             have_backend_stats ? backend_stats.nonzero_samples : 0U,
             have_backend_stats
                 ? backend_stats.maximum_absolute_magnitude : 0U,
             have_backend_stats ? backend_stats.rollback_attempts : 0U,
             have_backend_stats ? backend_stats.rollback_successes : 0U,
             have_backend_stats ? backend_stats.rollback_high_proofs : 0U,
             have_backend_stats && backend_stats.resources_retained ? 1U : 0U,
             have_backend_stats ? backend_stats.resources_owned : UINT32_MAX,
             have_audio_stats ? audio_stats.frames_rendered : 0U,
             have_audio_stats ? audio_stats.write_failures : 0U,
             have_audio_stats ? audio_stats.worker_stack_hwm_bytes : UINT32_MAX,
             have_audio_stats && audio_stats.music_playing ? 1U : 0U,
             have_audio_stats && audio_stats.music_paused ? 1U : 0U,
             have_audio_stats ? audio_stats.music_songs_started : 0U,
             have_audio_stats ? audio_stats.music_events_processed : 0U,
             have_audio_stats ? audio_stats.music_notes_started : 0U,
             have_audio_stats ? audio_stats.music_loops_completed : 0U,
             have_audio_stats ? audio_stats.music_mixed_frames : 0U,
             have_audio_stats ? audio_stats.music_parse_failures : UINT32_MAX,
             have_audio_stats
                 ? audio_stats.music_maximum_absolute_mix : 0U);
}

static void log_sound_ready(uint32_t audio_calls)
{
    if (platform_board_kind() == PLATFORM_BOARD_M5STACK_TAB5) {
        ESP_LOGI(TAG,"P4_DOOM SOUND_READY board=m5stack-tab5 codec=es8388 audio_calls=%" PRIu32,audio_calls);
    } else if (platform_board_kind() == PLATFORM_BOARD_WAVESHARE_4_3) {
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 SOUND_READY state=running amp_gpio=53 "
                 "enabled_level=high codec=es8311 codec_volume=%u/100 "
                 "pcm_gain=unity music_pipeline=wad-mus-procedural-16voice "
                 "audio_calls=%" PRIu32,
                 (unsigned)s_backend_volume_step * 10U,
                 audio_calls);
    } else {
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 SOUND_READY state=running "
                 "gpio30=low-readback-proven-at-start "
                 "backend_volume_step=%u/10 gain=volume-step-linear-pcm "
                 "music_pipeline=wad-mus-procedural-16voice "
                 "audio_calls=%" PRIu32,
                 (unsigned)s_backend_volume_step,
                 audio_calls);
    }
}

static void verify_audio_start_or_safe_degrade(bool sound_requested)
{
    if (!sound_requested) {
        return;
    }
    platform_audio_telemetry_t backend = {0};
    const esp_err_t telemetry_result = platform_audio_get_telemetry(&backend);
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if (telemetry_result == ESP_OK && backend.running && backend.codec_open &&
        backend.tx_created && backend.tx_enabled && !backend.resources_retained) {
        log_sound_ready(platform_audio_invocation_count());
        return;
    }
    doom_e6_audio_release_result_t release_result;
    const esp_err_t result=doom_e6_audio_release(&s_audio_lifecycle,&release_result);
    if (result!=ESP_OK) halt_dark("tab5-audio-start-cleanup",result);
    ESP_LOGW(TAG,"P4_DOOM SOUND_DEGRADED board=m5stack-tab5 fallback=silent");
#else
    const doom_touch_audio_factory_start_witness_t witness = {
        .snapshot_valid = telemetry_result == ESP_OK,
        .state = (uint32_t)backend.state,
        .running = backend.running,
        .pdm_created = backend.pdm_created,
        .pdm_enabled = backend.pdm_enabled,
        .tx_created = backend.tx_created,
        .tx_enabled = backend.tx_enabled,
        .zero_preload_frames = backend.zero_preload_frames,
        .gpio30_low_attempts = backend.gpio30_low_attempts,
        .gpio30_low_successes = backend.gpio30_low_successes,
        .gpio30_low_initial_readbacks =
            backend.gpio30_low_initial_readback_successes,
        .measured_settle_us = backend.measured_settle_us,
        .gpio30_low_second_readbacks =
            backend.gpio30_low_second_readback_successes,
        .resources_retained = backend.resources_retained,
        .resources_owned = backend.resources_owned,
    };
    if (doom_touch_audio_factory_start_proven(&witness)) {
        log_sound_ready(platform_audio_invocation_count());
        return;
    }
    platform_audio_adapter_stats_t adapter_stats = {0};
    platform_audio_adapter_get_stats(&adapter_stats);
    if (adapter_stats.running_low_readback_proven_at_start) {
        log_sound_ready(adapter_stats.invocations);
        return;
    }
    if (adapter_stats.ready_muted_zero_dma_proven) {
        ESP_LOGW(TAG,
                 "P4_DOOM_E6 SOUND_DEGRADED stage=doom-sfx-start "
                 "safety=ready-muted-zero-dma-proven fallback=silent "
                 "auto_fallback=0 audio_calls=%" PRIu32,
                 adapter_stats.invocations);
        return;
    }
    ESP_LOGE(TAG,
             "P4_DOOM_E6 AUDIO_SAFETY_FAULT stage=doom-sfx-start "
             "running_low_proven=0 ready_muted_zero_dma_proven=0 "
             "backend_snapshot_valid=%u backend_state=%u "
             "backend_running=%u halt=1",
             telemetry_result == ESP_OK ? 1U : 0U,
             telemetry_result == ESP_OK ? (unsigned)backend.state : 2U,
             telemetry_result == ESP_OK && backend.running ? 1U : 0U);
    halt_dark("audio-start-safety", ESP_ERR_INVALID_STATE);
#endif
}

void DG_Init(void)
{
    if (DG_ScreenBuffer == NULL || s_overlay_buffer == NULL) {
        halt_dark("engine-frame-buffer", ESP_ERR_NO_MEM);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_E6 VIDEO_READY input=0x00RRGGBB overlay=touch "
             "output=rgb565 source=320x200 logical=%ux%u native=%ux%u "
             "rotation_cw=%u viewport=%ux%u margins=%u/%u/%u/%u",
             (unsigned)PLATFORM_DISPLAY_WIDTH,
             (unsigned)PLATFORM_DISPLAY_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_NATIVE_WIDTH,
             (unsigned)PLATFORM_DISPLAY_NATIVE_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_ROTATION_CW_DEGREES,
             (unsigned)PLATFORM_DISPLAY_GAME_VIEWPORT_WIDTH,
             (unsigned)PLATFORM_DISPLAY_GAME_VIEWPORT_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_GAME_MARGIN_LEFT,
             (unsigned)PLATFORM_DISPLAY_GAME_MARGIN_RIGHT,
             (unsigned)PLATFORM_DISPLAY_GAME_MARGIN_TOP,
             (unsigned)PLATFORM_DISPLAY_GAME_MARGIN_BOTTOM);
}

void DG_DrawFrame(void)
{
    P4_DoomNetPoll();
    if (s_frame_error != ESP_OK) {
        return;
    }
    if (DG_ScreenBuffer == NULL || s_overlay_buffer == NULL) {
        s_frame_error = ESP_ERR_INVALID_STATE;
        return;
    }
    if (!doom_touch_audio_compose_frame(
            (const uint32_t *)DG_ScreenBuffer, DOOM_VIDEO_WIDTH,
            s_overlay_buffer, DOOM_VIDEO_WIDTH, &s_touch_input)) {
        doom_touch_input_init(&s_touch_input);
        if (!doom_touch_audio_compose_frame(
                (const uint32_t *)DG_ScreenBuffer, DOOM_VIDEO_WIDTH,
                s_overlay_buffer, DOOM_VIDEO_WIDTH, &s_touch_input)) {
            s_frame_error = ESP_ERR_INVALID_STATE;
            return;
        }
        ESP_LOGW(TAG,
                 "P4_DOOM_E6 TOUCH_DEGRADED stage=input-model-reset "
                 "error=ESP_ERR_INVALID_STATE neutral=1 overlay=visible");
    }
    s_frame_error = doom_video_submit_xrgb8888(
        s_overlay_buffer, DOOM_VIDEO_WIDTH, DOOM_SUBMIT_TIMEOUT_MS);
    P4_DoomNetPoll();
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
    P4_DoomNetPoll();
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
    return ticks_ms();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (pressed == NULL || key == NULL) {
        return 0;
    }
    *pressed = 0;
    *key = 0U;
#if P4_DOOM_SHARED_GAMEPAD
    service_gamepad();
    doom_gamepad_event_t gamepad_event;
    while (doom_gamepad_input_next(&s_gamepad_input, &gamepad_event)) {
        unsigned char mapped_key = 0U;
        if (!doom_gamepad_action_key(gamepad_event.action, &mapped_key)) {
            continue;
        }
        *pressed = gamepad_event.pressed == 1U ? 1 : 0;
        *key = mapped_key;
        return 1;
    }
#endif
    service_touch();
    doom_touch_event_t event;
    while (doom_touch_input_next(&s_touch_input, &event)) {
        unsigned char mapped_key = 0U;
        if (!doom_touch_audio_action_key(event.action, &mapped_key)) {
            continue;
        }
        *pressed = event.pressed == 1U ? 1 : 0;
        *key = mapped_key;
        return 1;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

#ifdef P4_CONSOLE_OS_EMBEDDED
void console_os_launch_doom(
    uint8_t master_volume_step,
    platform_game_storage_doom_title_t title,
    const p4_doom_mp_launch_config_t *multiplayer)
#else
void app_main(void)
#endif
{
#ifdef P4_CONSOLE_OS_EMBEDDED
    if (title >= PLATFORM_GAME_STORAGE_DOOM_TITLE_COUNT) {
        halt_dark("wad-title", ESP_ERR_INVALID_ARG);
    }
    const bool multiplayer_enabled =
        multiplayer != NULL && multiplayer->enabled;
    s_console_os_launch_active = true;
    s_backend_volume_step =
        master_volume_step >= DOOM_BACKEND_VOLUME_MIN_STEP &&
        master_volume_step <= DOOM_BACKEND_VOLUME_MAX_STEP
            ? master_volume_step : DOOM_BACKEND_VOLUME_DEFAULT_STEP;
#endif
    doom_touch_audio_runtime_gate_t gate = {0};
    doom_touch_audio_runtime_gate_read(&gate);
    s_runtime_gate = gate;
    const doom_touch_audio_runtime_mode_t mode =
        doom_touch_audio_runtime_gate_mode(&gate);
    if (platform_board_kind() == PLATFORM_BOARD_M5STACK_TAB5) {
        ESP_LOGI(TAG,"P4_DOOM START board=m5stack-tab5 input=panel-matched-touch sound=es8388 runtime=build-candidate");
    } else if (platform_board_kind() == PLATFORM_BOARD_WAVESHARE_4_3) {
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 START board=%s input=gt911-multitouch "
                 "sound=es8311-i2s1-speaker-sfx-mus amp_gpio=53 "
                 "codec_i2c_addr=0x18 music=wad-mus-procedural-16voice "
                 "controller=shared-platform-gamepad multiplayer=%s "
                 "runtime=exact-unit-waveshare-audio",
                 platform_board_name(),
#ifdef P4_CONSOLE_OS_EMBEDDED
                 multiplayer_enabled ? "p4mp-lockstep" : "single-player"
#else
                 "single-player"
#endif
        );
    } else {
        ESP_LOGI(TAG,
                 "P4_DOOM_E6 START board=%s input=gt911-multitouch "
                 "sound=factory-complete-i2s0-pdm-rx-i2s1-speaker-tx-sfx-mus "
                 "pdm_clk_gpio24_may_feed_codec_mclk=1 "
                 "codec_i2c_transactions=0 tx_mclk=none "
                 "music=wad-mus-procedural-16voice usb=absent "
                 "runtime=exact-unit-factory-audio",
                 platform_board_name());
    }
    if (mode == DOOM_TOUCH_AUDIO_RUNTIME_BLOCKED) {
        ESP_LOGW(TAG,
                 "P4_DOOM_E6 BLOCKED composite_gate=%u touch_gate=%u "
                 "audio_gate=%u display_initialized=0 shared_i2c_created=0 "
                 "touch_initialized=0 audio_initialized=0 doom_started=0",
                 (unsigned)gate.composite_authorized,
                 (unsigned)gate.touch_authorized,
                 (unsigned)gate.audio_authorized);
        return;
    }
    ESP_LOGI(TAG,
             "P4_DOOM_E6 MODE composite_gate=%u touch_gate=%u audio_gate=%u "
             "mode=%s",
             (unsigned)s_runtime_gate.composite_authorized,
             (unsigned)s_runtime_gate.touch_authorized,
             (unsigned)s_runtime_gate.audio_authorized,
             mode == DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_ONLY
                 ? "touch-only" : "touch-and-audio");

    doom_touch_input_init(&s_touch_input);
#if P4_DOOM_SHARED_GAMEPAD
    doom_gamepad_input_init(&s_gamepad_input);
#endif
    s_audio_gate_enabled =
        mode == DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO;
    s_audio_calls_allowed = s_audio_gate_enabled;
    if (s_audio_calls_allowed) {
        s_audio_lifecycle.hardware_touched = true;
        const esp_err_t safe_result = platform_audio_force_safe_shutdown();
        if (safe_result != ESP_OK) {
            s_audio_calls_allowed = false;
            ESP_LOGE(TAG,
                     "P4_DOOM_E6 AUDIO_SAFETY_FAULT stage=initial-safe "
#if CONFIG_P4_BOARD_M5STACK_TAB5
                     "error=%s amplifier-off-not-proven halt=1",
#else
                     "error=%s gpio30=high-not-proven halt=1",
#endif
                     esp_err_to_name(safe_result));
            halt_dark("audio-initial-safe", safe_result);
        } else {
            s_audio_lifecycle.safe_high_proven = true;
            if (platform_board_kind() == PLATFORM_BOARD_M5STACK_TAB5) {
                ESP_LOGI(TAG,"P4_DOOM AUDIO_SAFE board=m5stack-tab5 amplifier=disabled expander_readback=proven");
            } else if (platform_board_kind() == PLATFORM_BOARD_WAVESHARE_4_3) {
                ESP_LOGI(TAG,
                         "P4_DOOM_E6 AUDIO_SAFE amp_gpio=53 "
                         "safe_level=low pad_readback=proven "
                         "source=platform-first-call audio_calls=%" PRIu32,
                         platform_audio_invocation_count());
            } else {
                ESP_LOGI(TAG,
                         "P4_DOOM_E6 AUDIO_SAFE "
                         "gpio30=high-pad-readback-proven "
                         "source=platform-first-call audio_calls=%" PRIu32,
                         platform_audio_invocation_count());
            }
        }
    } else {
        log_sound_disabled();
    }

    esp_err_t result = platform_display_init();
    if (result != ESP_OK) {
        halt_dark("display-init", result);
    }
    s_display_initialized = true;
    I_AtExit(engine_exit_composite, true);
    ESP_LOGI(TAG,
             "P4_DOOM_E6 CLEANUP_REGISTERED "
             "order=audio-touch-bus-dark-video-display-blob");

    s_last_touch_retry_ms = ticks_ms();
    (void)try_touch_create(true);

#ifdef P4_CONSOLE_OS_EMBEDDED
    const uint8_t *const wad_start = &s_console_storage_wad_marker;
    const bool chex =
        title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST;
    const size_t wad_size = chex
        ? (size_t)PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES
        : (size_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
    const char *const wad_file_name = chex ? "chex.wad" : "doom1.wad";
    const char *const wad_path = chex ? "/doom/chex.wad" : EMBEDDED_WAD_PATH;
    const char *const wad_identity = chex
        ? "chex-quest-1.0" : "doom-shareware-1.9";
    const char *const wad_sha256 = chex
        ? "d8eb5277918883f490fb1a4be3c9a8588df2dbaee6dc4beb8df4929148bbffb1"
        : "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771";
#else
    const bool chex = false;
    const uint8_t *const wad_start = _binary_doom_shareware_wad_start;
    const uint8_t *const wad_end = _binary_doom_shareware_wad_end;
    const uintptr_t wad_start_address = (uintptr_t)wad_start;
    const uintptr_t wad_end_address = (uintptr_t)wad_end;
    if (wad_end_address < wad_start_address) {
        halt_dark("wad-linker-range", ESP_ERR_INVALID_SIZE);
    }
    const size_t wad_size =
        (size_t)(wad_end_address - wad_start_address);
    const char *const wad_file_name = "doom1.wad";
    const char *const wad_path = EMBEDDED_WAD_PATH;
    const char *const wad_identity = "doom-shareware-1.9";
    const char *const wad_sha256 =
        "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771";
    result = verify_embedded_wad(wad_start, wad_size);
    if (result != ESP_OK) {
        halt_dark("wad-validate", result);
    }
#endif
    ESP_LOGI(TAG,
             "P4_DOOM_E6 WAD_VERIFIED identity=%s "
             "bytes=%u sha256=%s",
             wad_identity, (unsigned)wad_size, wad_sha256);

    const platform_readonly_blob_config_t blob_config = {
        .base_path = "/doom",
        .file_name = wad_file_name,
        .data = wad_start,
        .size_bytes = wad_size,
    };
    result = platform_readonly_blob_register(&blob_config);
    if (result != ESP_OK) {
        halt_dark("wad-vfs-register", result);
    }
    s_blob_registered = true;
    result = verify_readonly_vfs(wad_path, wad_size, chex);
    if (result != ESP_OK) {
        halt_dark("wad-vfs-readback", result);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_E6 VFS_READY path=%s mode=read-only max_open=%u",
             wad_path,
             (unsigned)PLATFORM_READONLY_BLOB_MAX_OPEN_FILES);

    result = doom_video_init();
    if (result != ESP_OK) {
        halt_dark("video-adapter-init", result);
    }
    s_video_initialized = true;
    s_overlay_buffer = heap_caps_calloc(
        OVERLAY_PIXELS, sizeof(*s_overlay_buffer),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_overlay_buffer == NULL) {
        halt_dark("overlay-buffer", ESP_ERR_NO_MEM);
    }
    result = submit_startup_frame();
    if (result != ESP_OK) {
        halt_dark("startup-frame", result);
    }

    const bool sound_enabled = try_audio_enable();
    result = platform_display_set_brightness(25U);
    if (result != ESP_OK) {
        halt_dark("backlight", result);
    }

    char *sound_argv[] = {
        "doom", "-iwad", (char *)wad_path,
        "-gfxmode", "rgba8888",
    };
    char *silent_argv[] = {
        "doom", "-iwad", (char *)wad_path,
        "-gfxmode", "rgba8888", "-nosound", "-nomusic",
    };
#if P4_DOOM_SHARED_GAMEPAD
    ESP_LOGI(TAG,
             "P4_DOOM_E6 ENGINE_START wad=%s touch=%s overlay=visible "
             "sfx_request=%s music_request=%s "
             "music_synth=procedural-16voice "
             "controller=shared-platform-gamepad",
             wad_path, s_touch_ready ? "ready" : "degraded",
             sound_enabled ? "enabled" : "fallback-silent",
             sound_enabled ? "enabled" : "fallback-silent");
#else
    ESP_LOGI(TAG,
             "P4_DOOM_E6 ENGINE_START wad=%s touch=%s overlay=visible "
             "sfx_request=%s music_request=%s "
             "music_synth=procedural-16voice usb=absent",
             wad_path, s_touch_ready ? "ready" : "degraded",
             sound_enabled ? "enabled" : "fallback-silent",
             sound_enabled ? "enabled" : "fallback-silent");
#endif
    key_prevweapon = '[';
    key_nextweapon = ']';
    if (sound_enabled) {
        /* Doom sound Init may transition GPIO30 from proven-high to low. */
        s_audio_lifecycle.safe_high_proven = false;
    }
    if (sound_enabled) {
        doomgeneric_Create(
            (int)(sizeof(sound_argv) / sizeof(sound_argv[0])), sound_argv);
    } else {
        doomgeneric_Create(
            (int)(sizeof(silent_argv) / sizeof(silent_argv[0])), silent_argv);
    }
    verify_audio_start_or_safe_degrade(sound_enabled);
    key_prevweapon = '[';
    key_nextweapon = ']';
    if (s_frame_error != ESP_OK) {
        halt_dark("engine-first-frame", s_frame_error);
    }
    for (;;) {
        doomgeneric_Tick();
        if (doomgeneric_QuitRequested()) {
            ESP_LOGI(TAG,
                     "P4_DOOM_E6 EXIT status=confirmed action=restart-to-home "
                     "cleanup_complete=%u",
                     s_cleanup_complete ? 1U : 0U);
            esp_restart();
        }
        if (s_frame_error != ESP_OK) {
            halt_dark("engine-frame", s_frame_error);
        }
    }
}
