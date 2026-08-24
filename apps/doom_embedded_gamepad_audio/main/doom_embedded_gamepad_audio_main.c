// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 ESP32-P4 badge platform contributors
 *
 * E4 native-gamepad and sound compile/link composite. The exact user-local
 * shareware WAD is linked only into the local artifact. Reusable services own
 * all raw display, USB, HID, and audio resources.
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
#include "doom_gamepad/input.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "gamepad/gamepad.h"
#include "i_system.h"
#include "m_controls.h"
#include "p4_doom_net.h"
#ifdef P4_CONSOLE_OS_EMBEDDED
#include "p4/doom_multiplayer.h"
#endif
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
#include "sdkconfig.h"
#pragma GCC diagnostic pop
#include "platform/audio.h"
#include "platform/display.h"
#include "platform/readonly_blob.h"
#ifdef P4_CONSOLE_OS_EMBEDDED
#include "platform/game_storage.h"
#endif
#include "platform_gamepad_usb/platform_gamepad_usb.h"
#include "platform_usb_host/platform_usb_host.h"
#ifndef P4_CONSOLE_OS_EMBEDDED
#include "runtime_gate.h"
#endif

#define DOOM_SUBMIT_TIMEOUT_MS UINT32_C(100)
#define DOOM_FIRST_FRAME_TIMEOUT_MS UINT32_C(250)
#define DOOM_MAX_SLEEP_MS UINT32_C(60000)
#define DOOM_STATS_INTERVAL_FRAMES UINT32_C(300)
#define DOOM_USB_STOP_TIMEOUT_MS UINT32_C(2000)
#define EMBEDDED_WAD_BYTES ((size_t)4196020U)
#define EMBEDDED_WAD_PATH "/doom/doom1.wad"

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

#ifndef P4_CONSOLE_OS_EMBEDDED
static const platform_usb_fixture_evidence_t s_fixture_evidence = {
    .version = PLATFORM_USB_FIXTURE_EVIDENCE_VERSION,
    .size = (uint16_t)sizeof(platform_usb_fixture_evidence_t),
#if CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED
    .current_limit_ma = CONFIG_PLATFORM_USB_HOST_FIXTURE_CURRENT_LIMIT_MA,
    .externally_powered_vbus = CONFIG_PLATFORM_USB_HOST_FIXTURE_EXTERNAL_VBUS,
    .current_limited = CONFIG_PLATFORM_USB_HOST_FIXTURE_CURRENT_LIMITED,
    .backfeed_blocked = CONFIG_PLATFORM_USB_HOST_FIXTURE_BACKFEED_BLOCKED,
    .common_ground = CONFIG_PLATFORM_USB_HOST_FIXTURE_COMMON_GROUND,
    .data_pair_direct = CONFIG_PLATFORM_USB_HOST_FIXTURE_DATA_PAIR_DIRECT,
    .source_role_compliant =
        CONFIG_PLATFORM_USB_HOST_FIXTURE_SOURCE_ROLE_COMPLIANT,
    .overcurrent_fault_visible =
        CONFIG_PLATFORM_USB_HOST_FIXTURE_FAULT_VISIBLE,
    .board_path_reviewed = CONFIG_PLATFORM_USB_HOST_BOARD_PATH_REVIEWED,
    .evidence_id = CONFIG_PLATFORM_USB_HOST_FIXTURE_EVIDENCE_ID,
    .evidence_sha256 = CONFIG_PLATFORM_USB_HOST_FIXTURE_EVIDENCE_SHA256,
#else
    /* Independently invalid even if an app-local volatile gate is bypassed. */
    .current_limit_ma = 0U,
    .externally_powered_vbus = false,
    .current_limited = false,
    .backfeed_blocked = false,
    .common_ground = false,
    .data_pair_direct = false,
    .source_role_compliant = false,
    .overcurrent_fault_visible = false,
    .board_path_reviewed = false,
    .evidence_id = "UNAUTHORIZED",
    .evidence_sha256 = "",
#endif
};
#endif

static const char *const TAG = "p4_doom_e4";
static esp_err_t s_frame_error = ESP_OK;
static uint32_t s_frame_count;
static doom_gamepad_input_t s_input;
static bool s_input_ready;
static bool s_input_connection_known;
static uint32_t s_input_session;
static uint8_t s_input_connected;
static platform_audio_t *s_audio;
static bool s_audio_bound;
static bool s_display_initialized;
static bool s_video_initialized;
static bool s_blob_registered;
#ifndef P4_CONSOLE_OS_EMBEDDED
static bool s_host_started;
static bool s_host_quiesced;
static bool s_gamepad_started;
#endif
static bool s_cleanup_active;
static bool s_cleanup_complete;
static bool s_engine_heap_logged;
static bool s_engine_invoked;

_Static_assert(sizeof(pixel_t) == sizeof(uint32_t),
               "doom_embedded_gamepad_audio requires 32-bit engine pixels");
_Static_assert(DOOM_AUDIO_OUTPUT_RATE_HZ == 16000,
               "E4 Doom mixer and compile-only backend rate must align");

#ifndef P4_CONSOLE_OS_EMBEDDED
static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}
#endif

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
             "P4_DOOM_E4 MEMORY stage=%s internal_free=%u "
             "internal_largest=%u psram_free=%u psram_largest=%u "
             "main_stack_hwm_bytes=%u",
             stage, (unsigned)internal_free, (unsigned)internal_largest,
             (unsigned)psram_free, (unsigned)psram_largest,
             (unsigned)main_stack_hwm);
}

/*
 * The audio worker can borrow the backend and cached samples. Never destroy
 * either underneath it. USB teardown is independent and may still complete;
 * display-owned rails and the read-only WAD remain retained until the audio
 * side proves that every borrowed object has been released.
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
    const bool no_audio_worker_confirmed = !s_audio_bound;
    const bool destroy_was_needed = s_audio != NULL;
    esp_err_t destroy_result = ESP_ERR_INVALID_STATE;
    if (s_audio != NULL && no_audio_worker_confirmed) {
        destroy_result = platform_audio_destroy(&s_audio);
    }
    const bool destroy_confirmed = !destroy_was_needed ||
        (destroy_result == ESP_OK && s_audio == NULL);
    const esp_err_t recover_result = platform_audio_recover();
    const bool audio_retained = s_audio != NULL;
    const bool audio_backend_released = no_audio_worker_confirmed &&
        s_audio == NULL && destroy_confirmed && recover_result == ESP_OK;

    esp_err_t quiesce_result = ESP_OK;
    esp_err_t gamepad_result = ESP_OK;
    esp_err_t host_result = ESP_OK;
#ifndef P4_CONSOLE_OS_EMBEDDED
    if (s_host_started && !s_host_quiesced) {
        quiesce_result = platform_usb_host_quiesce();
        if (quiesce_result == ESP_OK) {
            s_host_quiesced = true;
        }
    }

    if (s_gamepad_started && s_host_quiesced) {
        gamepad_result = platform_gamepad_usb_stop(
            pdMS_TO_TICKS(DOOM_USB_STOP_TIMEOUT_MS));
        if (gamepad_result == ESP_OK) {
            s_gamepad_started = false;
        }
    }

    if (s_host_started && s_host_quiesced && !s_gamepad_started) {
        host_result = platform_usb_host_stop(
            pdMS_TO_TICKS(DOOM_USB_STOP_TIMEOUT_MS));
        if (host_result == ESP_OK) {
            s_host_started = false;
            s_host_quiesced = false;
        }
    }
#endif

    esp_err_t dark_result = ESP_ERR_INVALID_STATE;
    esp_err_t video_result = ESP_ERR_INVALID_STATE;
    esp_err_t display_result = ESP_ERR_INVALID_STATE;
    esp_err_t blob_result = ESP_ERR_INVALID_STATE;
    if (audio_backend_released) {
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
        if (!s_display_initialized && s_blob_registered) {
            blob_result = platform_readonly_blob_unregister();
            if (blob_result == ESP_OK) {
                s_blob_registered = false;
            }
        }
    }

    s_cleanup_complete = audio_backend_released && !audio_retained &&
#ifndef P4_CONSOLE_OS_EMBEDDED
        !s_host_started && !s_gamepad_started &&
#endif
        !s_video_initialized && !s_display_initialized &&
        !s_blob_registered;
    ESP_LOGI(TAG,
             "P4_DOOM_E4 CLEANUP force_safe=%s audio_stop=%s "
             "audio_state_result=%s audio_state=%u stop_accepted=%u "
             "audio_unbind=%s audio_destroy=%s destroy_confirmed=%u "
             "audio_recover=%s audio_retained=%u backend_released=%u "
             "quiesce=%s gamepad=%s host=%s dark=%s video=%s "
             "display=%s vfs=%s complete=%u",
             esp_err_to_name(force_safe_result), esp_err_to_name(stop_result),
             esp_err_to_name(state_result), (unsigned)audio_state,
             stop_accepted ? 1U : 0U, esp_err_to_name(unbind_result),
             esp_err_to_name(destroy_result), destroy_confirmed ? 1U : 0U,
             esp_err_to_name(recover_result), audio_retained ? 1U : 0U,
             audio_backend_released ? 1U : 0U,
             esp_err_to_name(quiesce_result), esp_err_to_name(gamepad_result),
             esp_err_to_name(host_result), esp_err_to_name(dark_result),
             esp_err_to_name(video_result), esp_err_to_name(display_result),
             esp_err_to_name(blob_result), s_cleanup_complete ? 1U : 0U);
    s_cleanup_active = false;
}

static void halt_dark(const char *stage, esp_err_t error)
{
    composite_cleanup();
    ESP_LOGE(TAG, "P4_DOOM_E4 HALT stage=%s error=%s", stage,
             esp_err_to_name(error));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        composite_cleanup();
    }
}

static void engine_exit_composite(void)
{
    composite_cleanup();
}

#ifndef P4_CONSOLE_OS_EMBEDDED
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
#endif

static esp_err_t verify_readonly_vfs(
    const char *wad_path, size_t wad_bytes, bool allow_pwad)
{
    struct stat metadata;
    if (wad_path == NULL || stat(wad_path, &metadata) != 0
        || !S_ISREG(metadata.st_mode)
        || metadata.st_size != (off_t)wad_bytes
        || (metadata.st_mode & 0222) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    FILE *file = fopen(wad_path, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    uint8_t header[12];
    esp_err_t result = ESP_OK;
    if (fread(header, 1U, sizeof(header), file) != sizeof(header)
        || (memcmp(header, "IWAD", 4U) != 0
            && (!allow_pwad || memcmp(header, "PWAD", 4U) != 0))
        || fseek(file, 0L, SEEK_END) != 0
        || ftell(file) != (long)wad_bytes) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (fclose(file) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
    }
    return result;
}

#ifndef P4_CONSOLE_OS_EMBEDDED
static esp_err_t start_native_gamepad(void)
{
    esp_err_t result = platform_usb_host_start(&s_fixture_evidence);
    if (result != ESP_OK) {
        return result;
    }
    s_host_started = true;

    result = platform_gamepad_usb_start();
    if (result != ESP_OK) {
        return result;
    }
    s_gamepad_started = true;

    result = platform_usb_host_enable_root_port();
    if (result != ESP_OK) {
        return result;
    }
    return ESP_OK;
}
#endif

static bool action_key(uint8_t action, unsigned char *key)
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

static void refresh_input(void)
{
    platform_gamepad_snapshot_t snapshot;
    gamepad_state_t merged;
    const esp_err_t snapshot_result =
        platform_gamepad_usb_get_snapshot(&snapshot);
    if (snapshot_result == ESP_OK) {
        merged = snapshot.state;
    } else {
        gamepad_state_init(&merged);
    }

    platform_usb_input_snapshot_t auxiliary;
    memset(&auxiliary, 0, sizeof(auxiliary));
    const esp_err_t auxiliary_result =
        platform_gamepad_usb_get_input_snapshot(&auxiliary);
    const bool keyboard_connected = auxiliary_result == ESP_OK &&
        auxiliary.version == PLATFORM_USB_INPUT_SNAPSHOT_VERSION &&
        auxiliary.size == sizeof(auxiliary) &&
        auxiliary.keyboard.connected != 0U;
    const bool mouse_connected = auxiliary_result == ESP_OK &&
        auxiliary.version == PLATFORM_USB_INPUT_SNAPSHOT_VERSION &&
        auxiliary.size == sizeof(auxiliary) &&
        auxiliary.mouse.connected != 0U;
    if ((keyboard_connected || mouse_connected) && merged.connected == 0U) {
        (void)gamepad_state_connect(&merged, auxiliary.keyboard.timestamp_us);
    }
    if (keyboard_connected) {
        const platform_usb_keyboard_state_t *const keyboard =
            &auxiliary.keyboard;
        const bool up = platform_usb_keyboard_key_down(
            keyboard, PLATFORM_USB_KEY_UP) ||
            platform_usb_keyboard_key_down(keyboard, PLATFORM_USB_KEY_W);
        const bool down = platform_usb_keyboard_key_down(
            keyboard, PLATFORM_USB_KEY_DOWN) ||
            platform_usb_keyboard_key_down(keyboard, PLATFORM_USB_KEY_S);
        const bool left = platform_usb_keyboard_key_down(
            keyboard, PLATFORM_USB_KEY_LEFT) ||
            platform_usb_keyboard_key_down(keyboard, PLATFORM_USB_KEY_A);
        const bool right = platform_usb_keyboard_key_down(
            keyboard, PLATFORM_USB_KEY_RIGHT) ||
            platform_usb_keyboard_key_down(keyboard, PLATFORM_USB_KEY_D);
        if (up != down) {
            merged.dpad |= up ? GAMEPAD_DPAD_UP : GAMEPAD_DPAD_DOWN;
        }
        if (left != right) {
            merged.dpad |= left ? GAMEPAD_DPAD_LEFT : GAMEPAD_DPAD_RIGHT;
        }
        if (platform_usb_keyboard_key_down(keyboard, PLATFORM_USB_KEY_Z) ||
            platform_usb_keyboard_key_down(keyboard, PLATFORM_USB_KEY_SPACE) ||
            (keyboard->modifier & UINT8_C(0x11)) != 0U) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH);
        }
        if (platform_usb_keyboard_key_down(keyboard, PLATFORM_USB_KEY_X)) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST);
        }
        if ((keyboard->modifier & UINT8_C(0x22)) != 0U) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_WEST);
        }
        if (platform_usb_keyboard_key_down(
                keyboard, PLATFORM_USB_KEY_ENTER)) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_START);
        }
        if (platform_usb_keyboard_key_down(
                keyboard, PLATFORM_USB_KEY_ESCAPE) ||
            platform_usb_keyboard_key_down(
                keyboard, PLATFORM_USB_KEY_BACKSPACE)) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_BACK);
        }
    }
    if (mouse_connected) {
        if ((auxiliary.mouse.buttons & UINT8_C(0x01)) != 0U) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH);
        }
        if ((auxiliary.mouse.buttons & UINT8_C(0x02)) != 0U) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST);
        }
        if ((auxiliary.mouse.buttons & UINT8_C(0x04)) != 0U) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_GUIDE);
        }
        if (auxiliary.mouse.wheel > 0) {
            merged.buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_NORTH);
        } else if (auxiliary.mouse.wheel < 0) {
            merged.buttons |=
                GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_RIGHT_STICK);
        }
    }

    const gamepad_state_t *const state = &merged;
    const uint32_t session = snapshot_result == ESP_OK
        ? snapshot.session
        : (keyboard_connected ? auxiliary.keyboard.session
                              : auxiliary.mouse.session);
    const uint8_t connected = state->connected;
    if (!s_input_connection_known || connected != s_input_connected
        || session != s_input_session) {
        if (snapshot_result == ESP_OK) {
            ESP_LOGI(TAG,
                     "P4_DOOM_E4 INPUT session=%" PRIu32
                     " connected=%u sequence=%" PRIu32
                     " vid=%04x pid=%04x capabilities=%08" PRIx32,
                     session, (unsigned)connected, state->sequence,
                     (unsigned)snapshot.identity.vendor_id,
                     (unsigned)snapshot.identity.product_id,
                     snapshot.capabilities);
        } else {
            ESP_LOGE(TAG,
                     "P4_DOOM_E4 INPUT_SNAPSHOT_FAIL error=%s state=neutral",
                     esp_err_to_name(snapshot_result));
        }
        s_input_connection_known = true;
        s_input_connected = connected;
        s_input_session = session;
    }

    const gamepad_status_t input_result =
        doom_gamepad_input_update(&s_input, state);
    if (input_result != GAMEPAD_OK) {
        ESP_LOGE(TAG,
                 "P4_DOOM_E4 INPUT_UPDATE_FAIL error=%s state=neutral",
                 gamepad_status_name(input_result));
    }
}

static void log_runtime_stats(void)
{
    platform_display_stats_t display_stats;
    platform_gamepad_usb_stats_t gamepad_stats;
    platform_gamepad_snapshot_t snapshot;
    doom_audio_runtime_stats_t audio_stats;
    const esp_err_t display_result =
        platform_display_get_stats(&display_stats);
    const esp_err_t gamepad_result =
        platform_gamepad_usb_get_stats(&gamepad_stats);
    const esp_err_t snapshot_result =
        platform_gamepad_usb_get_snapshot(&snapshot);
    const esp_err_t audio_result =
        doom_audio_runtime_get_stats(&audio_stats);
    if (display_result == ESP_OK && gamepad_result == ESP_OK &&
        snapshot_result == ESP_OK && audio_result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_DOOM_E4 STATS frames=%" PRIu32
                 " submits=%" PRIu32 " completions=%" PRIu32
                 " video_timeouts=%" PRIu32 " video_failures=%" PRIu32
                 " connected=%u session=%" PRIu32 " sequence=%" PRIu32
                 " connections=%" PRIu32 " disconnections=%" PRIu32
                 " reports=%" PRIu32 " dropped=%" PRIu32
                 " malformed=%" PRIu32 " callback_faults=%" PRIu32
                 " audio_commands=%" PRIu32
                 " audio_command_drops=%" PRIu32
                 " audio_frames=%" PRIu32
                 " audio_write_failures=%" PRIu32
                 " audio_worker_stack_hwm_bytes=%" PRIu32,
                 s_frame_count, display_stats.submits_started,
                 display_stats.submits_completed,
                 display_stats.submit_timeouts, display_stats.submit_failures,
                 (unsigned)snapshot.state.connected, snapshot.session,
                 snapshot.state.sequence, gamepad_stats.connections,
                 gamepad_stats.disconnections, gamepad_stats.reports_committed,
                 gamepad_stats.reports_dropped, gamepad_stats.malformed_reports,
                 gamepad_stats.callback_faults, audio_stats.commands_enqueued,
                 audio_stats.commands_dropped, audio_stats.frames_rendered,
                 audio_stats.write_failures,
                 audio_stats.worker_stack_hwm_bytes);
    }
}

void DG_Init(void)
{
    if (DG_ScreenBuffer == NULL) {
        halt_dark("engine-screen-buffer", ESP_ERR_NO_MEM);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_E4 VIDEO_READY input=0x00RRGGBB output=rgb565 "
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
    P4_DoomNetPoll();
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
    if (!s_input_ready || pressed == NULL || key == NULL) {
        return 0;
    }

    for (;;) {
        doom_gamepad_event_t event;
        if (!doom_gamepad_input_next(&s_input, &event)) {
            refresh_input();
            if (!doom_gamepad_input_next(&s_input, &event)) {
                return 0;
            }
        }
        if (action_key(event.action, key)) {
            *pressed = event.pressed != 0U ? 1 : 0;
            return 1;
        }
    }
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

#ifdef P4_CONSOLE_OS_EMBEDDED
void console_os_launch_doom(
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
#endif
#ifndef P4_CONSOLE_OS_EMBEDDED
    doom_gamepad_audio_runtime_gate_t gate = {0};
    doom_gamepad_audio_runtime_gate_read(&gate);
#endif
#ifdef P4_CONSOLE_OS_EMBEDDED
    ESP_LOGI(TAG,
             "P4_DOOM_E4 START wad=storage-exact vfs=read-only "
             "input=usb-gamepad-keyboard-mouse sfx=enabled music=enabled "
             "audio_backend=olimex-es8311-i2s1 multiplayer=%s",
             multiplayer != NULL && multiplayer->enabled
                 ? "p4mp-lockstep" : "single-player");
#else
    ESP_LOGI(TAG,
             "P4_DOOM_E4 START wad=embedded-exact vfs=read-only "
             "input=native-p4-hs-usb sfx=compiled music=disabled "
             "audio_backend=compile-link-only");
#endif
#ifndef P4_CONSOLE_OS_EMBEDDED
    if (gate.composite_authorized != 1U || gate.usb_authorized != 1U ||
        gate.audio_authorized != 1U) {
        ESP_LOGW(TAG,
                 "P4_DOOM_E4 BLOCKED composite_gate=%u usb_gate=%u "
                 "audio_gate=%u display_initialized=0 usb_host_installed=0 "
                 "root_port_enabled=0 gpio30_configured=0 i2s_created=0",
                 (unsigned)gate.composite_authorized,
                 (unsigned)gate.usb_authorized,
                 (unsigned)gate.audio_authorized);
        return;
    }
#endif

    /* First hardware action after every independent authorization gate. */
    esp_err_t result = platform_audio_force_safe_shutdown();
    if (result != ESP_OK) {
        halt_dark("audio-initial-safe", result);
    }
    ESP_LOGI(TAG, "P4_DOOM_E4 SAFE amplifier=disabled");

    result = platform_display_init();
    if (result != ESP_OK) {
        halt_dark("display-init", result);
    }
    s_display_initialized = true;
    I_AtExit(engine_exit_composite, true);
    ESP_LOGI(TAG,
             "P4_DOOM_E4 CLEANUP_REGISTERED order=audio-usb-display-vfs");

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
    const size_t wad_size = (size_t)(wad_end_address - wad_start_address);
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
             "P4_DOOM_E4 WAD_VERIFIED identity=%s "
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
             "P4_DOOM_E4 VFS_READY path=%s mode=read-only max_open=%u",
             wad_path,
             (unsigned)PLATFORM_READONLY_BLOB_MAX_OPEN_FILES);

    result = doom_video_init();
    if (result != ESP_OK) {
        halt_dark("video-adapter-init", result);
    }
    s_video_initialized = true;
    result = doom_video_submit_black(DOOM_FIRST_FRAME_TIMEOUT_MS);
    if (result != ESP_OK) {
        halt_dark("first-black-frame", result);
    }

    doom_gamepad_input_init(&s_input);
    s_input_ready = true;
#ifndef P4_CONSOLE_OS_EMBEDDED
    result = start_native_gamepad();
    if (result != ESP_OK) {
        halt_dark("native-gamepad-start", result);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_E4 USB_READY controller=p4-hs root_port_enabled=1 "
             "tier=1-generic-hid fixture=%s evidence_sha256=%s",
             s_fixture_evidence.evidence_id,
             s_fixture_evidence.evidence_sha256);
#else
    ESP_LOGI(TAG,
             "P4_DOOM_E4 USB_READY owner=console-os "
             "classes=gamepad,keyboard,mouse topology=integrated-hub");
#endif

    log_memory_snapshot("pre-audio-create");
    const platform_audio_config_t audio_config = {
        .control_bus = NULL,
        .sample_rate_hz = (uint32_t)DOOM_AUDIO_OUTPUT_RATE_HZ,
        .volume_percent = 10U,
    };
    result = platform_audio_create(&audio_config, &s_audio);
    if (result != ESP_OK) {
        halt_dark("audio-create", result);
    }
    log_memory_snapshot("post-audio-create");

    const doom_audio_runtime_config_t doom_audio_config = {
        .platform_audio = s_audio,
        .sample_rate_hz = (uint32_t)DOOM_AUDIO_OUTPUT_RATE_HZ,
        .display_owns_ldo3_ldo4 = true,
    };
    result = doom_audio_runtime_bind(&doom_audio_config);
    if (result != ESP_OK) {
        halt_dark("audio-bind", result);
    }
    s_audio_bound = true;

    result = platform_display_set_brightness(25U);
    if (result != ESP_OK) {
        halt_dark("backlight", result);
    }

#ifdef P4_CONSOLE_OS_EMBEDDED
    char *argv[] = {
        "doom",
        "-iwad",
        (char *)wad_path,
        "-gfxmode",
        "rgba8888",
    };
#else
    char *argv[] = {
        "doom",
        "-iwad",
        (char *)wad_path,
        "-gfxmode",
        "rgba8888",
        "-nomusic",
    };
#endif
    const int argc = (int)(sizeof(argv) / sizeof(argv[0]));
    key_prevweapon = '[';
    key_nextweapon = ']';
#ifdef P4_CONSOLE_OS_EMBEDDED
    ESP_LOGI(TAG,
             "P4_DOOM_E4 ENGINE_START gfxmode=rgba8888 wad=%s "
             "sfx=enabled music=enabled input=canonical-snapshot "
             "audio_bound=1 video_seam=xrgb8888_to_rgb565",
             wad_path);
#else
    ESP_LOGI(TAG,
             "P4_DOOM_E4 ENGINE_START gfxmode=rgba8888 wad=%s "
             "sfx=enabled music=disabled input=canonical-snapshot "
             "audio_bound=1 video_seam=xrgb8888_to_rgb565",
             wad_path);
#endif
    s_engine_invoked = true;
    doomgeneric_Create(argc, argv);
    /* M_LoadDefaults runs inside Create; enforce nonzero weapon bindings. */
    key_prevweapon = '[';
    key_nextweapon = ']';
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
