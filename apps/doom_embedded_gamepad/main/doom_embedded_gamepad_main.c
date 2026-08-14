// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 ESP32-P4 badge platform contributors
 *
 * E3 native-gamepad composite. The exact user-local shareware WAD is linked
 * only into the local artifact. Games consume canonical platform snapshots;
 * no USB handle, report descriptor, callback, or board pin is owned here.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "doom/video.h"
#include "doom_gamepad/input.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "gamepad/gamepad.h"
#include "i_system.h"
#include "m_controls.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"
#pragma GCC diagnostic pop
#include "platform/display.h"
#include "platform/readonly_blob.h"
#include "platform_gamepad_usb/platform_gamepad_usb.h"
#include "platform_usb_host/platform_usb_host.h"

#define DOOM_SUBMIT_TIMEOUT_MS UINT32_C(100)
#define DOOM_FIRST_FRAME_TIMEOUT_MS UINT32_C(250)
#define DOOM_MAX_SLEEP_MS UINT32_C(60000)
#define DOOM_STATS_INTERVAL_FRAMES UINT32_C(300)
#define DOOM_USB_STOP_TIMEOUT_MS UINT32_C(2000)
#define EMBEDDED_WAD_BYTES ((size_t)4196020U)
#define EMBEDDED_WAD_PATH "/doom/doom1.wad"

#if CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED
#define DOOM_GAMEPAD_FIXTURE_AUTHORIZED 1
#else
#define DOOM_GAMEPAD_FIXTURE_AUTHORIZED 0
#endif

extern const uint8_t _binary_doom_shareware_wad_start[];
extern const uint8_t _binary_doom_shareware_wad_end[];

static const uint8_t s_expected_wad_sha256[32] = {
    0x1d, 0x7d, 0x43, 0xbe, 0x50, 0x1e, 0x67, 0xd9,
    0x27, 0xe4, 0x15, 0xe0, 0xb8, 0xf3, 0xe2, 0x9c,
    0x3b, 0xf3, 0x30, 0x75, 0xe8, 0x59, 0x72, 0x18,
    0x16, 0xf6, 0x52, 0xa5, 0x26, 0xca, 0xc7, 0x71,
};

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
    /*
     * Invalid by construction. Even if the app-local volatile gate were
     * bypassed, the reusable host rejects this before touching USB hardware;
     * its own CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED policy is also off.
     */
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

/*
 * Volatile prevents the dormant USB branch from being dead-stripped. The
 * value itself is immutable flash data derived from the false committed
 * fixture authorization. Doom/display do not depend on this USB-only gate.
 */
static const volatile uint8_t s_usb_runtime_authorization_gate =
    DOOM_GAMEPAD_FIXTURE_AUTHORIZED;

static bool __attribute__((noinline)) native_gamepad_authorized(void)
{
    return s_usb_runtime_authorization_gate == 1U;
}

static const char *const TAG = "p4_doom_gamepad_e3";
static esp_err_t s_frame_error = ESP_OK;
static uint32_t s_frame_count;
static doom_gamepad_input_t s_input;
static bool s_input_ready;
static bool s_input_connection_known;
static uint32_t s_input_session;
static uint8_t s_input_connected;
static bool s_display_initialized;
static bool s_video_initialized;
static bool s_blob_registered;
static bool s_host_started;
static bool s_host_quiesced;
static bool s_gamepad_started;
static bool s_cleanup_active;
static bool s_cleanup_complete;

_Static_assert(sizeof(pixel_t) == sizeof(uint32_t),
               "doom_embedded_gamepad requires default 32-bit engine pixels");

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static void composite_cleanup(void)
{
    if (s_cleanup_active || s_cleanup_complete) {
        return;
    }
    s_cleanup_active = true;

    esp_err_t dark_result = ESP_OK;
    if (s_display_initialized) {
        dark_result = platform_display_set_brightness(0U);
    }

    esp_err_t quiesce_result = ESP_OK;
    if (s_host_started && !s_host_quiesced) {
        quiesce_result = platform_usb_host_quiesce();
        if (quiesce_result == ESP_OK) {
            s_host_quiesced = true;
        }
    }

    esp_err_t gamepad_result = ESP_OK;
    if (s_gamepad_started && s_host_quiesced) {
        gamepad_result = platform_gamepad_usb_stop(
            pdMS_TO_TICKS(DOOM_USB_STOP_TIMEOUT_MS));
        if (gamepad_result == ESP_OK) {
            s_gamepad_started = false;
        }
    }

    esp_err_t host_result = ESP_OK;
    if (s_host_started && s_host_quiesced && !s_gamepad_started) {
        host_result = platform_usb_host_stop(
            pdMS_TO_TICKS(DOOM_USB_STOP_TIMEOUT_MS));
        if (host_result == ESP_OK) {
            s_host_started = false;
            s_host_quiesced = false;
        }
    }

    esp_err_t video_result = ESP_OK;
    if (s_video_initialized) {
        video_result = doom_video_deinit();
        if (video_result == ESP_OK) {
            s_video_initialized = false;
        }
    }

    esp_err_t display_result = ESP_OK;
    if (s_display_initialized && !s_video_initialized) {
        display_result = platform_display_deinit();
        if (display_result == ESP_OK) {
            s_display_initialized = false;
        }
    }

    esp_err_t blob_result = ESP_OK;
    if (s_blob_registered) {
        blob_result = platform_readonly_blob_unregister();
        if (blob_result == ESP_OK) {
            s_blob_registered = false;
        }
    }

    s_cleanup_complete = !s_host_started && !s_gamepad_started &&
        !s_video_initialized && !s_display_initialized && !s_blob_registered;
    ESP_LOGI(TAG,
             "P4_DOOM_GAMEPAD E3 CLEANUP dark=%s quiesce=%s gamepad=%s "
             "host=%s video=%s display=%s vfs=%s complete=%u",
             esp_err_to_name(dark_result), esp_err_to_name(quiesce_result),
             esp_err_to_name(gamepad_result), esp_err_to_name(host_result),
             esp_err_to_name(video_result), esp_err_to_name(display_result),
             esp_err_to_name(blob_result), s_cleanup_complete ? 1U : 0U);
    s_cleanup_active = false;
}

static void halt_dark(const char *stage, esp_err_t error)
{
    composite_cleanup();
    ESP_LOGE(TAG, "P4_DOOM_GAMEPAD E3 HALT stage=%s error=%s", stage,
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
    gamepad_state_t neutral;
    const esp_err_t snapshot_result =
        platform_gamepad_usb_get_snapshot(&snapshot);
    const gamepad_state_t *state = &snapshot.state;
    if (snapshot_result != ESP_OK) {
        gamepad_state_init(&neutral);
        state = &neutral;
    }

    const uint32_t session = snapshot_result == ESP_OK ? snapshot.session : 0U;
    const uint8_t connected = state->connected;
    if (!s_input_connection_known || connected != s_input_connected
        || session != s_input_session) {
        if (snapshot_result == ESP_OK) {
            ESP_LOGI(TAG,
                     "P4_DOOM_GAMEPAD E3 INPUT session=%" PRIu32
                     " connected=%u sequence=%" PRIu32
                     " vid=%04x pid=%04x capabilities=%08" PRIx32,
                     session, (unsigned)connected, state->sequence,
                     (unsigned)snapshot.identity.vendor_id,
                     (unsigned)snapshot.identity.product_id,
                     snapshot.capabilities);
        } else {
            ESP_LOGE(TAG,
                     "P4_DOOM_GAMEPAD E3 INPUT_SNAPSHOT_FAIL error=%s "
                     "state=neutral",
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
                 "P4_DOOM_GAMEPAD E3 INPUT_UPDATE_FAIL error=%s state=neutral",
                 gamepad_status_name(input_result));
    }
}

static void log_runtime_stats(void)
{
    platform_display_stats_t display_stats;
    const esp_err_t display_result =
        platform_display_get_stats(&display_stats);
    if (!s_input_ready) {
        if (display_result == ESP_OK) {
            ESP_LOGI(TAG,
                     "P4_DOOM_GAMEPAD E3 STATS frames=%" PRIu32
                     " submits=%" PRIu32 " completions=%" PRIu32
                     " video_timeouts=%" PRIu32 " video_failures=%" PRIu32
                     " input=blocked usb_api_calls=0",
                     s_frame_count, display_stats.submits_started,
                     display_stats.submits_completed,
                     display_stats.submit_timeouts,
                     display_stats.submit_failures);
        }
        return;
    }

    platform_gamepad_usb_stats_t gamepad_stats;
    platform_gamepad_snapshot_t snapshot;
    const esp_err_t gamepad_result =
        platform_gamepad_usb_get_stats(&gamepad_stats);
    const esp_err_t snapshot_result =
        platform_gamepad_usb_get_snapshot(&snapshot);
    if (display_result == ESP_OK && gamepad_result == ESP_OK
        && snapshot_result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_DOOM_GAMEPAD E3 STATS frames=%" PRIu32
                 " submits=%" PRIu32 " completions=%" PRIu32
                 " video_timeouts=%" PRIu32 " video_failures=%" PRIu32
                 " connected=%u session=%" PRIu32 " sequence=%" PRIu32
                 " connections=%" PRIu32 " disconnections=%" PRIu32
                 " reports=%" PRIu32 " dropped=%" PRIu32
                 " malformed=%" PRIu32 " callback_faults=%" PRIu32,
                 s_frame_count, display_stats.submits_started,
                 display_stats.submits_completed,
                 display_stats.submit_timeouts, display_stats.submit_failures,
                 (unsigned)snapshot.state.connected, snapshot.session,
                 snapshot.state.sequence, gamepad_stats.connections,
                 gamepad_stats.disconnections, gamepad_stats.reports_committed,
                 gamepad_stats.reports_dropped, gamepad_stats.malformed_reports,
                 gamepad_stats.callback_faults);
    }
}

void DG_Init(void)
{
    if (DG_ScreenBuffer == NULL) {
        halt_dark("engine-screen-buffer", ESP_ERR_NO_MEM);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_GAMEPAD E3 VIDEO_READY input=0x00RRGGBB "
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

void app_main(void)
{
    ESP_LOGI(TAG,
             "P4_DOOM_GAMEPAD E3 START wad=embedded-exact vfs=read-only "
             "input=native-p4-hs-usb-dormant-by-default "
             "audio=disabled sd=unused");
    esp_err_t result = platform_display_init();
    if (result != ESP_OK) {
        halt_dark("display-init", result);
    }
    s_display_initialized = true;
    I_AtExit(engine_exit_composite, true);
    ESP_LOGI(TAG, "P4_DOOM_GAMEPAD E3 CLEANUP_REGISTERED");

    const uint8_t *const wad_start = _binary_doom_shareware_wad_start;
    const uint8_t *const wad_end = _binary_doom_shareware_wad_end;
    const uintptr_t wad_start_address = (uintptr_t)wad_start;
    const uintptr_t wad_end_address = (uintptr_t)wad_end;
    if (wad_end_address < wad_start_address) {
        halt_dark("wad-linker-range", ESP_ERR_INVALID_SIZE);
    }
    const size_t wad_size = (size_t)(wad_end_address - wad_start_address);
    result = verify_embedded_wad(wad_start, wad_size);
    if (result != ESP_OK) {
        halt_dark("wad-validate", result);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_GAMEPAD E3 WAD_VERIFIED identity=doom-shareware-1.9 "
             "bytes=%u sha256=%s",
             (unsigned)EMBEDDED_WAD_BYTES,
             "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771");

    const platform_readonly_blob_config_t blob_config = {
        .base_path = "/doom",
        .file_name = "doom1.wad",
        .data = wad_start,
        .size_bytes = wad_size,
    };
    result = platform_readonly_blob_register(&blob_config);
    if (result != ESP_OK) {
        halt_dark("wad-vfs-register", result);
    }
    s_blob_registered = true;
    result = verify_readonly_vfs();
    if (result != ESP_OK) {
        halt_dark("wad-vfs-readback", result);
    }
    ESP_LOGI(TAG,
             "P4_DOOM_GAMEPAD E3 VFS_READY path=%s mode=read-only max_open=%u",
             EMBEDDED_WAD_PATH,
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

    if (native_gamepad_authorized()) {
        doom_gamepad_input_init(&s_input);
        result = start_native_gamepad();
        if (result != ESP_OK) {
            halt_dark("native-gamepad-start", result);
        }
        s_input_ready = true;
        ESP_LOGI(TAG,
                 "P4_DOOM_GAMEPAD E3 USB_READY controller=p4-hs "
                 "root_port_enabled=1 tier=1-generic-hid fixture=%s "
                 "evidence_sha256=%s",
                 s_fixture_evidence.evidence_id,
                 s_fixture_evidence.evidence_sha256);
    } else {
        ESP_LOGW(TAG,
                 "P4_DOOM_GAMEPAD E3 INPUT_BLOCKED "
                 "reason=powered-backfeed-safe-fixture-not-authorized "
                 "input=neutral usb_api_calls=0 usb_host_installed=0 "
                 "root_port_enabled=0");
    }

    result = platform_display_set_brightness(25U);
    if (result != ESP_OK) {
        halt_dark("backlight", result);
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
    key_prevweapon = '[';
    key_nextweapon = ']';
    ESP_LOGI(TAG,
             "P4_DOOM_GAMEPAD E3 ENGINE_START gfxmode=rgba8888 wad=%s "
             "video_seam=xrgb8888_to_rgb565 input_seam=%s",
             EMBEDDED_WAD_PATH,
             s_input_ready ? "canonical-snapshot" : "neutral");
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
