// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * P4 Console OS MVP: a small FreeRTOS-native foreground shell. Applications
 * are statically registered and consume platform services; this is not a
 * dynamic executable loader. The first Doom integration is an exclusive,
 * one-way handoff because the imported Doom engine has no reviewed reentrant
 * teardown path yet.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "console/shell.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "p4/audio.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/platform.h"
#include "p4_game_registry.h"
#include "platform/display.h"
#include "platform/game_storage.h"
#include "platform/touch.h"
#include "platform_i2c_shared/bus.h"
#include "runtime_gate.h"

enum {
    CONSOLE_APP_DOOM = 1,
    CONSOLE_APP_COLORS = 2,
    CONSOLE_APP_TOUCH = 3,
    CONSOLE_APP_SYSTEM = 4,
    CONSOLE_APP_AUDIO = 5,
    CONSOLE_FRAME_INTERVAL_MS = 16,
    CONSOLE_SUBMIT_TIMEOUT_MS = 250,
    CONSOLE_BACKLIGHT_PERCENT = 25,
    CONSOLE_CLEANUP_ATTEMPTS = 3,
    CONSOLE_NATIVE_AUDIO_VOLUME_STEP = 6,
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK =
        P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ *
        CONSOLE_FRAME_INTERVAL_MS / 1000,
};

static const char *const TAG = "p4_console_os";
static platform_i2c_shared_t *s_shared_bus;
static platform_touch_t *s_touch;
static uint16_t *s_pixels;
static bool s_display_initialized;
static bool s_touch_ready;
static uint32_t s_loop_count;
static uint32_t s_touch_polls;
static uint32_t s_touch_poll_failures;
static uint32_t s_doom_handoff_count;
static bool s_game_storage_initialized;
static bool s_game_storage_status_seen;
static platform_game_storage_status_t s_game_storage_status;
static char s_doom_subtitle[CONSOLE_SHELL_SUBTITLE_MAX_BYTES] =
    "STORAGE CHECKING";
static int16_t s_native_audio_pcm[
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK *
    P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT];

void console_os_launch_doom(void);

static console_app_descriptor_t s_apps[CONSOLE_SHELL_MAX_APPS];
static size_t s_app_count;

static const console_app_descriptor_t s_doom_app = {
    .id = CONSOLE_APP_DOOM,
    .title = "DOOM",
    .subtitle = s_doom_subtitle,
    .accent_rgb565 = UINT16_C(0xF904),
    .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                    CONSOLE_CAPABILITY_TOUCH |
                    CONSOLE_CAPABILITY_AUDIO |
                    CONSOLE_CAPABILITY_STORAGE,
    .page = CONSOLE_PAGE_EXTERNAL,
    .enabled = true,
};

static const console_app_descriptor_t s_builtin_apps[] = {
    {
        .id = CONSOLE_APP_COLORS,
        .title = "COLORS",
        .subtitle = "DISPLAY TEST",
        .accent_rgb565 = UINT16_C(0x5FFF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_COLORS,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_TOUCH,
        .title = "TOUCH",
        .subtitle = "GT911 CONTACTS",
        .accent_rgb565 = UINT16_C(0xFFE0),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_TOUCH,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_SYSTEM,
        .title = "SYSTEM",
        .subtitle = "RTOS STATUS",
        .accent_rgb565 = UINT16_C(0x5FEA),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_SYSTEM,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_AUDIO,
        .title = "AUDIO",
        .subtitle = "DOOM SOUND PATH",
        .accent_rgb565 = UINT16_C(0xF81F),
        .capabilities = CONSOLE_CAPABILITY_AUDIO,
        .page = CONSOLE_PAGE_AUDIO,
        .enabled = true,
    },
};

_Static_assert(CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK == 256,
               "16 ms must produce exactly 256 16 kHz frames");

_Static_assert((int)CONSOLE_SHELL_WIDTH ==
                   (int)PLATFORM_DISPLAY_GAME_WIDTH,
               "console width must match the platform game surface");
_Static_assert((int)CONSOLE_SHELL_HEIGHT ==
                   (int)PLATFORM_DISPLAY_GAME_HEIGHT,
               "console height must match the platform game surface");
_Static_assert(CONSOLE_SHELL_PHYSICAL_WIDTH == PLATFORM_TOUCH_WIDTH,
               "console touch width must match GT911 coordinates");
_Static_assert(CONSOLE_SHELL_PHYSICAL_HEIGHT == PLATFORM_TOUCH_HEIGHT,
               "console touch height must match GT911 coordinates");
_Static_assert(CONSOLE_SHELL_MAX_CONTACTS == PLATFORM_TOUCH_MAX_CONTACTS,
               "console contact bound must match the touch service");

static bool append_app(const console_app_descriptor_t *app)
{
    if (app == NULL || s_app_count >= CONSOLE_SHELL_MAX_APPS) {
        return false;
    }
    s_apps[s_app_count++] = *app;
    return true;
}

static uint32_t shell_capabilities(uint32_t game_capabilities)
{
    uint32_t capabilities = 0U;
    if ((game_capabilities & P4_GAME_CAP_VIDEO) != 0U) {
        capabilities |= CONSOLE_CAPABILITY_DISPLAY;
    }
    if ((game_capabilities & P4_GAME_CAP_CONTROLS) != 0U) {
        capabilities |= CONSOLE_CAPABILITY_TOUCH;
    }
    if ((game_capabilities &
         (P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM)) != 0U) {
        capabilities |= CONSOLE_CAPABILITY_AUDIO;
    }
    if ((game_capabilities & P4_GAME_CAP_STORAGE) != 0U) {
        capabilities |= CONSOLE_CAPABILITY_STORAGE;
    }
    return capabilities;
}

static bool build_app_registry(void)
{
    s_app_count = 0U;
    if (!append_app(&s_doom_app)) {
        return false;
    }
    for (size_t i = 0U; i < p4_generated_game_count; ++i) {
        const p4_game_descriptor_t *const game = p4_generated_games[i];
        if (!p4_game_descriptor_valid(game) ||
            game->launcher_id <= CONSOLE_APP_AUDIO) {
            return false;
        }
        const console_app_descriptor_t launcher = {
            .id = game->launcher_id,
            .title = game->title,
            .subtitle = game->subtitle,
            .accent_rgb565 = game->accent_rgb565,
            .capabilities = shell_capabilities(
                game->required_capabilities | game->optional_capabilities),
            .page = CONSOLE_PAGE_EXTERNAL,
            .enabled = true,
        };
        if (!append_app(&launcher)) {
            return false;
        }
    }
    for (size_t i = 0U;
         i < sizeof(s_builtin_apps) / sizeof(s_builtin_apps[0]); ++i) {
        if (!append_app(&s_builtin_apps[i])) {
            return false;
        }
    }
    return true;
}

static void halt_dark(const char *stage, esp_err_t error)
{
    if (s_display_initialized) {
        (void)platform_display_set_brightness(0U);
    }
    ESP_LOGE(TAG, "P4_CONSOLE_OS HALT stage=%s error=%s",
             stage, esp_err_to_name(error));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static uint32_t uptime_seconds(void)
{
    const int64_t microseconds = esp_timer_get_time();
    if (microseconds <= 0) {
        return 0U;
    }
    const uint64_t seconds = (uint64_t)microseconds / UINT64_C(1000000);
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}

static uint32_t free_kib(uint32_t capabilities)
{
    const size_t bytes = heap_caps_get_free_size(capabilities);
    return (uint32_t)(bytes / 1024U);
}

static console_shell_storage_state_t shell_storage_state(
    platform_game_storage_state_t state)
{
    switch (state) {
    case PLATFORM_GAME_STORAGE_APP_READY:
        return CONSOLE_STORAGE_READY;
    case PLATFORM_GAME_STORAGE_USB_HOST:
        return CONSOLE_STORAGE_USB_HOST;
    case PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED:
        return CONSOLE_STORAGE_FORMAT_REQUIRED;
    case PLATFORM_GAME_STORAGE_APP_MISSING:
        return CONSOLE_STORAGE_MISSING;
    case PLATFORM_GAME_STORAGE_APP_INVALID:
        return CONSOLE_STORAGE_INVALID;
    case PLATFORM_GAME_STORAGE_GAME_LOCKED:
        return CONSOLE_STORAGE_LOCKED;
    case PLATFORM_GAME_STORAGE_FAULT:
        return CONSOLE_STORAGE_FAULT;
    case PLATFORM_GAME_STORAGE_UNINITIALIZED:
    case PLATFORM_GAME_STORAGE_APP_SCANNING:
    case PLATFORM_GAME_STORAGE_TRANSITION:
    default:
        return CONSOLE_STORAGE_STARTING;
    }
}

static void set_doom_storage_state(platform_game_storage_state_t state)
{
    const char *subtitle = "STORAGE CHECKING";
    const bool ready = state == PLATFORM_GAME_STORAGE_APP_READY;
    switch (state) {
    case PLATFORM_GAME_STORAGE_APP_READY:
        subtitle = "SHAREWARE 1.9 / READY";
        break;
    case PLATFORM_GAME_STORAGE_USB_HOST:
        subtitle = "USB STORAGE ACTIVE";
        break;
    case PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED:
        subtitle = "HOST FORMAT REQUIRED";
        break;
    case PLATFORM_GAME_STORAGE_APP_MISSING:
        subtitle = "COPY DOOM1.WAD OVER USB";
        break;
    case PLATFORM_GAME_STORAGE_APP_INVALID:
        subtitle = "DOOM1.WAD INVALID";
        break;
    case PLATFORM_GAME_STORAGE_GAME_LOCKED:
        subtitle = "GAME STORAGE LOCKED";
        break;
    case PLATFORM_GAME_STORAGE_FAULT:
        subtitle = "STORAGE OFFLINE";
        break;
    case PLATFORM_GAME_STORAGE_UNINITIALIZED:
    case PLATFORM_GAME_STORAGE_APP_SCANNING:
    case PLATFORM_GAME_STORAGE_TRANSITION:
    default:
        break;
    }
    const size_t length = strlen(subtitle);
    const size_t copy = length < sizeof(s_doom_subtitle) - 1U
        ? length : sizeof(s_doom_subtitle) - 1U;
    memcpy(s_doom_subtitle, subtitle, copy);
    s_doom_subtitle[copy] = '\0';
    for (size_t index = 0U; index < s_app_count; ++index) {
        if (s_apps[index].id == CONSOLE_APP_DOOM) {
            s_apps[index].enabled = ready;
            break;
        }
    }
}

static void sync_game_storage(void)
{
    platform_game_storage_status_t status = {
        .state = PLATFORM_GAME_STORAGE_FAULT,
        .last_error = ESP_ERR_INVALID_STATE,
    };
    if (s_game_storage_initialized) {
        (void)platform_game_storage_refresh();
        if (platform_game_storage_get_status(&status) != ESP_OK) {
            status.state = PLATFORM_GAME_STORAGE_FAULT;
            status.last_error = ESP_FAIL;
        }
    }
    const bool changed = !s_game_storage_status_seen ||
        status.state != s_game_storage_status.state ||
        status.usb_attached != s_game_storage_status.usb_attached ||
        status.generation != s_game_storage_status.generation ||
        status.last_error != s_game_storage_status.last_error;
    s_game_storage_status = status;
    s_game_storage_status_seen = true;
    set_doom_storage_state(status.state);
    if (changed) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS GAME_STORAGE state=%s usb_attached=%u "
                 "generation=%lu capacity=%llu last_error=%s",
                 platform_game_storage_state_name(status.state),
                 status.usb_attached ? 1U : 0U,
                 (unsigned long)status.generation,
                 (unsigned long long)status.capacity_bytes,
                 esp_err_to_name(status.last_error));
    }
}

static console_shell_runtime_info_t runtime_info(void)
{
    const uint64_t capacity_kib = s_game_storage_status.capacity_bytes / 1024U;
    const console_shell_runtime_info_t info = {
        .uptime_seconds = uptime_seconds(),
        .internal_free_kib = free_kib(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .psram_free_kib = free_kib(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .game_storage_kib = capacity_kib > UINT32_MAX
            ? UINT32_MAX : (uint32_t)capacity_kib,
        .game_storage_state = shell_storage_state(
            s_game_storage_status.state),
        .touch_ready = s_touch_ready,
        /* Compiled handoff only; the shell itself never starts audio. */
        .audio_handoff_ready = true,
        .game_storage_usb_attached = s_game_storage_status.usb_attached,
        .doom_wad_ready =
            s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_READY,
    };
    return info;
}

static void create_touch_or_continue(void)
{
    esp_err_t result = platform_i2c_shared_create(&s_shared_bus);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS TOUCH_DEGRADED stage=shared-bus error=%s",
                 esp_err_to_name(result));
        return;
    }

    platform_touch_config_t config;
    platform_touch_config_init(
        &config, platform_i2c_shared_handle(s_shared_bus));
    result = platform_touch_create(&config, &s_touch);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS TOUCH_DEGRADED stage=create error=%s",
                 esp_err_to_name(result));
        return;
    }
    s_touch_ready = true;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS TOUCH_READY controller=gt911 contacts_max=%u",
             (unsigned)PLATFORM_TOUCH_MAX_CONTACTS);
}

static bool read_touch_frame(platform_touch_frame_t *frame)
{
    if (frame == NULL) {
        return false;
    }
    platform_touch_frame_neutral(frame);
    if (!s_touch_ready || s_touch == NULL) {
        return false;
    }
    ++s_touch_polls;
    const esp_err_t result = platform_touch_poll(s_touch, frame);
    if (result != ESP_OK || frame->valid == 0U ||
        frame->contact_count > PLATFORM_TOUCH_MAX_CONTACTS) {
        ++s_touch_poll_failures;
        if (s_touch_poll_failures == 1U ||
            s_touch_poll_failures % 120U == 0U) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS TOUCH_POLL_FAIL count=%lu error=%s",
                     (unsigned long)s_touch_poll_failures,
                     esp_err_to_name(result));
        }
        platform_touch_frame_neutral(frame);
        return false;
    }
    return true;
}

static console_shell_action_t poll_touch(console_shell_t *shell)
{
    platform_touch_frame_t frame;
    if (!read_touch_frame(&frame)) {
        return console_shell_handle_touch(shell, false, NULL, 0U);
    }
    console_shell_contact_t contacts[CONSOLE_SHELL_MAX_CONTACTS];
    const size_t count = frame.contact_count;
    for (size_t i = 0U; i < count; ++i) {
        contacts[i].x = frame.contacts[i].x;
        contacts[i].y = frame.contacts[i].y;
    }
    return console_shell_handle_touch(
        shell, true, count == 0U ? NULL : contacts, count);
}

static void log_runtime_stats(const console_shell_t *shell)
{
    platform_display_stats_t display = {0};
    const esp_err_t result = platform_display_get_stats(&display);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "P4_CONSOLE_OS STATS_UNAVAILABLE error=%s",
                 esp_err_to_name(result));
        return;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STATS loops=%lu page=%u renders=%lu "
             "touch_ready=%u touch_polls=%lu touch_failures=%lu "
             "display_submits=%lu display_completions=%lu "
             "display_timeouts=%lu display_failures=%lu "
             "amp_energized=0 doom_handoffs=%lu storage=%s "
             "storage_generation=%lu usb_attached=%u",
             (unsigned long)s_loop_count,
             (unsigned)shell->page,
             (unsigned long)shell->render_generation,
             s_touch_ready ? 1U : 0U,
             (unsigned long)s_touch_polls,
             (unsigned long)s_touch_poll_failures,
             (unsigned long)display.submits_started,
             (unsigned long)display.submits_completed,
             (unsigned long)display.submit_timeouts,
             (unsigned long)display.submit_failures,
             (unsigned long)s_doom_handoff_count,
             platform_game_storage_state_name(s_game_storage_status.state),
             (unsigned long)s_game_storage_status.generation,
             s_game_storage_status.usb_attached ? 1U : 0U);
}

static esp_err_t present(console_shell_t *shell)
{
    if (!console_shell_render_rgb565(
            shell, s_pixels, CONSOLE_SHELL_WIDTH)) {
        return ESP_ERR_INVALID_STATE;
    }
    return platform_display_submit_rgb565(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
}

static esp_err_t destroy_touch_for_handoff(void)
{
    if (s_touch == NULL) {
        s_touch_ready = false;
        return ESP_OK;
    }
    esp_err_t result = ESP_FAIL;
    for (unsigned attempt = 0U;
         attempt < CONSOLE_CLEANUP_ATTEMPTS && s_touch != NULL; ++attempt) {
        result = platform_touch_destroy(&s_touch);
        if (result != ESP_OK) {
            vTaskDelay(1U);
        }
    }
    s_touch_ready = false;
    return s_touch == NULL ? ESP_OK : result;
}

static esp_err_t destroy_bus_for_handoff(void)
{
    if (s_shared_bus == NULL) {
        return ESP_OK;
    }
    esp_err_t result = ESP_FAIL;
    for (unsigned attempt = 0U;
         attempt < CONSOLE_CLEANUP_ATTEMPTS && s_shared_bus != NULL;
         ++attempt) {
        result = platform_i2c_shared_destroy(&s_shared_bus);
        if (result != ESP_OK) {
            vTaskDelay(1U);
        }
    }
    return s_shared_bus == NULL ? ESP_OK : result;
}

static bool native_audio_runtime_allowed(void)
{
    doom_touch_audio_runtime_gate_t gate = {0};
    doom_touch_audio_runtime_gate_read(&gate);
    return doom_touch_audio_runtime_gate_mode(&gate) ==
        DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO;
}

static bool pump_native_audio(p4_game_platform_audio_t *audio,
                              p4_audio_mixer_t *mixer)
{
    if (!p4_game_platform_audio_running(audio)) {
        return false;
    }
    if (!p4_audio_mixer_render(
            mixer, s_native_audio_pcm,
            CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK)) {
        halt_dark("native-audio-mix", ESP_ERR_INVALID_SIZE);
    }
    for (size_t offset = 0U;
         offset < CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK;
         offset += P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
        const esp_err_t result = p4_game_platform_audio_write(
            audio,
            &s_native_audio_pcm[
                offset * P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT],
            P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES);
        if (result != ESP_OK) {
            if (!audio->safe_high_proven || audio->backend != NULL) {
                halt_dark("native-audio-write-safety", result);
            }
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS NATIVE_SOUND_DEGRADED "
                     "stage=write error=%s fallback=silent",
                     esp_err_to_name(result));
            p4_audio_mixer_stop_all(mixer);
            return false;
        }
    }
    return true;
}

static void close_native_audio_or_halt(p4_game_platform_audio_t *audio)
{
    if (audio == NULL || !audio->hardware_touched) {
        return;
    }
    const esp_err_t result = p4_game_platform_audio_close(audio);
    if (result != ESP_OK || !audio->safe_high_proven ||
        audio->backend != NULL) {
        halt_dark("native-audio-close-safety",
                  result == ESP_OK ? ESP_ERR_INVALID_STATE : result);
    }
}

static void *allocate_game_state(size_t bytes)
{
    void *state = heap_caps_calloc(
        1U, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (state == NULL) {
        state = heap_caps_calloc(1U, bytes, MALLOC_CAP_8BIT);
    }
    return state;
}

static esp_err_t run_native_game(console_shell_t *shell,
                                 const p4_game_descriptor_t *game)
{
    if (shell == NULL || !p4_game_descriptor_valid(game) ||
        s_pixels == NULL || !s_display_initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    void *const game_state = allocate_game_state(game->state_bytes);
    if (game_state == NULL) {
        return ESP_ERR_NO_MEM;
    }

    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    p4_game_platform_audio_t audio;
    p4_game_platform_audio_init(&audio);
    bool audio_running = false;
    if ((game->required_capabilities | game->optional_capabilities) &
        P4_GAME_CAP_AUDIO_TONE) {
        const esp_err_t audio_result = p4_game_platform_audio_open(
            &audio, native_audio_runtime_allowed(),
            CONSOLE_NATIVE_AUDIO_VOLUME_STEP);
        if (audio_result == ESP_OK) {
            audio_running = true;
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS NATIVE_SOUND_READY app=%s "
                     "rate_hz=16000 format=pcm16-stereo volume_step=6/10",
                     game->id);
        } else if (audio.hardware_touched &&
                   (!audio.safe_high_proven || audio.backend != NULL)) {
            heap_caps_free(game_state);
            halt_dark("native-audio-open-safety", audio_result);
        } else {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS NATIVE_SOUND_DEGRADED app=%s "
                     "stage=open error=%s fallback=silent",
                     game->id, esp_err_to_name(audio_result));
        }
    }

    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
            (audio_running ? P4_GAME_CAP_AUDIO_TONE : 0U),
        .audio_context = &mixer,
        .play_tone = audio_running
            ? p4_audio_mixer_service_play_tone : NULL,
        .submit_pcm16_stereo = NULL,
        .stop_audio = audio_running
            ? p4_audio_mixer_service_stop : NULL,
    };
    p4_game_instance_t instance = {0};
    if (!p4_game_instance_start(
            &instance, game, &services, game_state, game->state_bytes)) {
        close_native_audio_or_halt(&audio);
        heap_caps_free(game_state);
        return ESP_ERR_INVALID_STATE;
    }
    p4_game_surface_t surface = {
        .pixels = s_pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    if (!p4_game_instance_render(&instance, &surface)) {
        p4_game_instance_stop(&instance);
        close_native_audio_or_halt(&audio);
        heap_caps_free(game_state);
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t result = platform_display_submit_rgb565(
        s_pixels, P4_GAME_SURFACE_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
    if (result != ESP_OK) {
        halt_dark("native-first-frame", result);
    }
    if (audio_running) {
        audio_running = pump_native_audio(&audio, &mixer);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS NATIVE_GAME_START app=%s api=%lu "
             "display_owner=console touch_owner=console audio=%s",
             game->id, (unsigned long)game->api_version,
             audio_running ? "ready" : "silent");

    p4_game_input_mapper_t input_mapper;
    p4_game_input_mapper_init(&input_mapper);
    p4_game_result_t game_result = P4_GAME_CONTINUE;
    uint32_t frames = 0U;
    TickType_t last_wake = xTaskGetTickCount();
    while (game_result == P4_GAME_CONTINUE) {
        platform_touch_frame_t frame;
        const bool valid = read_touch_frame(&frame);
        p4_physical_touch_t touches[P4_INPUT_MAX_TOUCHES];
        const size_t touch_count = valid ? frame.contact_count : 0U;
        for (size_t i = 0U; i < touch_count; ++i) {
            touches[i].x = frame.contacts[i].x;
            touches[i].y = frame.contacts[i].y;
        }
        p4_game_input_t input;
        p4_game_input_mapper_update(
            &input_mapper, valid,
            touch_count == 0U ? NULL : touches,
            touch_count, 0U, &input);
        game_result = p4_game_instance_update(
            &instance, &input, CONSOLE_FRAME_INTERVAL_MS);
        if (game_result == P4_GAME_ERROR) {
            result = ESP_FAIL;
            break;
        }
        if (game_result == P4_GAME_CONTINUE) {
            if (!p4_game_instance_render(&instance, &surface)) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            result = platform_display_submit_rgb565(
                s_pixels, P4_GAME_SURFACE_WIDTH,
                CONSOLE_SUBMIT_TIMEOUT_MS);
            if (result != ESP_OK) {
                halt_dark("native-frame-submit", result);
            }
            if (audio_running) {
                audio_running = pump_native_audio(&audio, &mixer);
            }
            if (frames != UINT32_MAX) {
                ++frames;
            }
            if (frames != 0U && frames % 300U == 0U) {
                p4_audio_mixer_stats_t stats = {0};
                p4_audio_mixer_get_stats(&mixer, &stats);
                ESP_LOGI(TAG,
                         "P4_CONSOLE_OS NATIVE_GAME_STATS app=%s "
                         "frames=%lu touch_polls=%lu touch_failures=%lu "
                         "audio_running=%u tones=%lu audio_frames=%lu",
                         game->id, (unsigned long)frames,
                         (unsigned long)s_touch_polls,
                         (unsigned long)s_touch_poll_failures,
                         audio_running ? 1U : 0U,
                         (unsigned long)stats.tones_started,
                         (unsigned long)audio.frames_written);
            }
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    }

    p4_game_instance_stop(&instance);
    close_native_audio_or_halt(&audio);
    heap_caps_free(game_state);
    console_shell_show_home(shell);
    const esp_err_t home_result = present(shell);
    if (home_result != ESP_OK) {
        halt_dark("native-return-home", home_result);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS NATIVE_GAME_STOP app=%s result=%s "
             "return=launcher amp_safe=%u",
             game->id,
             game_result == P4_GAME_EXIT_TO_LAUNCHER ? "user-exit" : "error",
             audio.hardware_touched ? (audio.safe_high_proven ? 1U : 0U) : 1U);
    return game_result == P4_GAME_EXIT_TO_LAUNCHER ? ESP_OK : result;
}

static void launch_doom_exclusive(console_shell_t *shell)
{
    esp_err_t result = platform_game_storage_lock_for_game();
    sync_game_storage();
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS HANDOFF_REJECTED app=doom storage=%s "
                 "error=%s action=eject-usb-and-retry",
                 platform_game_storage_state_name(
                     s_game_storage_status.state),
                 esp_err_to_name(result));
        console_shell_show_home(shell);
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(shell, &current_runtime);
        return;
    }
    ++s_doom_handoff_count;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HANDOFF_BEGIN app=doom mode=exclusive-one-way "
             "audio_owner=doom volume_step=6/10 storage=game-locked "
             "usb_device=stopped");
    result = platform_display_set_brightness(0U);
    if (result != ESP_OK) {
        halt_dark("handoff-backlight", result);
    }
    result = destroy_touch_for_handoff();
    if (result != ESP_OK) {
        halt_dark("handoff-touch-destroy", result);
    }
    result = destroy_bus_for_handoff();
    if (result != ESP_OK) {
        halt_dark("handoff-bus-destroy", result);
    }
    result = platform_display_deinit();
    if (result != ESP_OK) {
        halt_dark("handoff-display-deinit", result);
    }
    s_display_initialized = false;
    heap_caps_free(s_pixels);
    s_pixels = NULL;

    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HANDOFF_COMPLETE app=doom shell_services=released");
    console_os_launch_doom();
    halt_dark("doom-returned-without-reentrant-teardown",
              ESP_ERR_INVALID_STATE);
}

void app_main(void)
{
    const esp_err_t storage_result = platform_game_storage_init();
    s_game_storage_initialized = storage_result == ESP_OK;
    if (!s_game_storage_initialized) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS GAME_STORAGE_DEGRADED error=%s "
                 "native_apps=available doom=disabled",
                 esp_err_to_name(storage_result));
    }
    sync_game_storage();
    if (!build_app_registry()) {
        halt_dark("app-registry", ESP_ERR_INVALID_ARG);
    }
    sync_game_storage();
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS START shell=freertos-native apps=%u "
             "surface=rgb565-320x200 touch=gt911 "
             "native_game_api=1 native_format=p4-native-static-v1 "
             "game_storage=%s execution=build-candidate",
             (unsigned)s_app_count,
             platform_game_storage_state_name(s_game_storage_status.state));

    console_shell_t shell;
    if (!console_shell_init(
            &shell, s_apps, s_app_count)) {
        halt_dark("shell-init", ESP_ERR_INVALID_ARG);
    }

    esp_err_t result = platform_display_init();
    if (result != ESP_OK) {
        halt_dark("display-init", result);
    }
    s_display_initialized = true;
    s_pixels = heap_caps_calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*s_pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pixels == NULL) {
        halt_dark("framebuffer-allocation", ESP_ERR_NO_MEM);
    }

    create_touch_or_continue();
    const console_shell_runtime_info_t initial_runtime = runtime_info();
    console_shell_set_runtime_info(&shell, &initial_runtime);
    result = present(&shell);
    if (result != ESP_OK) {
        halt_dark("first-frame", result);
    }
    result = platform_display_set_brightness(CONSOLE_BACKLIGHT_PERCENT);
    if (result != ESP_OK) {
        halt_dark("backlight", result);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS READY page=home amp_energized=0 "
             "doom_audio=deferred-until-exclusive-handoff");

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        ++s_loop_count;
        sync_game_storage();
        const console_shell_action_t action = poll_touch(&shell);
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(&shell, &current_runtime);
        if (action.type == CONSOLE_ACTION_LAUNCH &&
            action.app_id == CONSOLE_APP_DOOM) {
            launch_doom_exclusive(&shell);
        } else if (action.type == CONSOLE_ACTION_LAUNCH) {
            const p4_game_descriptor_t *const game =
                p4_generated_game_by_launcher_id(action.app_id);
            if (game == NULL) {
                ESP_LOGE(TAG,
                         "P4_CONSOLE_OS NATIVE_GAME_REJECTED id=%lu "
                         "reason=not-registered",
                         (unsigned long)action.app_id);
            } else {
                const esp_err_t game_result = run_native_game(&shell, game);
                if (game_result != ESP_OK) {
                    ESP_LOGW(TAG,
                             "P4_CONSOLE_OS NATIVE_GAME_DEGRADED app=%s "
                             "error=%s return=launcher",
                             game->id, esp_err_to_name(game_result));
                }
                /* Do not make the shell catch up every tick spent in-game. */
                last_wake = xTaskGetTickCount();
            }
        }
        if (console_shell_is_dirty(&shell)) {
            result = present(&shell);
            if (result != ESP_OK) {
                halt_dark("frame-submit", result);
            }
        }
        if (s_loop_count % 300U == 0U) {
            log_runtime_stats(&shell);
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    }
}
