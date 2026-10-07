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
#include "m_config.h"
#include "doomkeys.h"
#include "i_system.h"
#include "i_video.h"
#include "m_controls.h"
#include "p4_doom_net.h"
#include "runtime_gate.h"
#include "touch_controls.h"
#ifdef P4_CONSOLE_OS_EMBEDDED
#include "p4/doom_multiplayer.h"
#include "w_wad.h"
#include "doom_arena_ui.h"
#endif
#if defined(P4_CONSOLE_OS_EMBEDDED) && CONFIG_P4_BOARD_M5STACK_TAB5
#define P4_DOOM_USB_DEBUG 1
#include "console_debug.h"
#include "p4/game.h"
#else
#define P4_DOOM_USB_DEBUG 0
#endif
/* Product loading feedback is independent of optional USB diagnostics. */
#if defined(P4_CONSOLE_OS_EMBEDDED) && CONFIG_P4_BOARD_M5STACK_TAB5
#define P4_DOOM_STARTUP_UI 1
#include "p4/presentation_font.h"
#else
#define P4_DOOM_STARTUP_UI 0
#endif
#if defined(P4_CONSOLE_OS_EMBEDDED) && \
    (CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || \
     CONFIG_P4_BOARD_M5STACK_TAB5)
#define P4_DOOM_SHARED_GAMEPAD 1
#include "doom_gamepad/input.h"
#include "doom_gamepad/key_merge.h"
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
#if CONFIG_P4_BOARD_M5STACK_TAB5
#include "platform/touch_sampler.h"
#endif
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
#define DOOM_BACKEND_VOLUME_MAX_STEP UINT8_C(10)
#define TOUCH_POLL_INTERVAL_MS UINT32_C(16)
#define TOUCH_RETRY_INTERVAL_MS UINT32_C(5000)
#define TOUCH_DEGRADED_LOG_INTERVAL_MS UINT32_C(2000)
#define EMBEDDED_WAD_BYTES ((size_t)4196020U)
#define EMBEDDED_WAD_PATH "/doom/doom1.wad"
#define OVERLAY_PIXELS \
    ((size_t)DOOM_VIDEO_WIDTH * (size_t)DOOM_VIDEO_HEIGHT)

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
#if CONFIG_P4_BOARD_M5STACK_TAB5
static uint64_t s_frame_acquire_us;
#endif
static uint8_t s_backend_volume_step = DOOM_BACKEND_VOLUME_DEFAULT_STEP;

/* Legacy measurement-only opt-in. Native pixels stay in the PSRAM-backed
 * engine zone; a 768x480 indexed frame cannot consume internal DMA memory. */
#ifndef P4_DOOM_INDEXED_DRAM_EXPERIMENT
#define P4_DOOM_INDEXED_DRAM_EXPERIMENT 0
#endif
#if P4_DOOM_INDEXED_DRAM_EXPERIMENT && !DOOM_VIDEO_NATIVE_TAB5 && \
    defined(P4_CONSOLE_OS_EMBEDDED) && CONFIG_P4_BOARD_M5STACK_TAB5
#define P4_DOOM_INDEXED_DRAM_ENABLED 1
#else
#define P4_DOOM_INDEXED_DRAM_ENABLED 0
#endif
#if P4_DOOM_INDEXED_DRAM_ENABLED
#define DOOM_INDEXED_FRAMEBUFFER_BYTES ((size_t)64000U)
_Static_assert(SCREENWIDTH * SCREENHEIGHT == DOOM_INDEXED_FRAMEBUFFER_BYTES,
               "The indexed DRAM experiment is exactly 320 by 200 bytes");
static void *s_indexed_framebuffer;
static bool s_indexed_framebuffer_borrowed;
static bool s_indexed_engine_live;

static void indexed_framebuffer_heap(const char *stage)
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma = internal | MALLOC_CAP_DMA;
    ESP_LOGI(TAG,
             "P4_DOOM INDEXED_DRAM stage=%s ptr=%p bytes=%u borrowed=%u "
             "internal_free=%u internal_largest=%u internal_min=%u "
             "dma_free=%u dma_largest=%u dma_min=%u",
             stage, s_indexed_framebuffer,
             s_indexed_framebuffer ? (unsigned)DOOM_INDEXED_FRAMEBUFFER_BYTES : 0U,
             s_indexed_framebuffer_borrowed ? 1U : 0U,
             (unsigned)heap_caps_get_free_size(internal),
             (unsigned)heap_caps_get_largest_free_block(internal),
             (unsigned)heap_caps_get_minimum_free_size(internal),
             (unsigned)heap_caps_get_free_size(dma),
             (unsigned)heap_caps_get_largest_free_block(dma),
             (unsigned)heap_caps_get_minimum_free_size(dma));
}

static void indexed_framebuffer_reserve(void)
{
    if (s_indexed_engine_live || s_indexed_framebuffer != NULL) return;
    indexed_framebuffer_heap("before-reserve");
    s_indexed_framebuffer = heap_caps_malloc(DOOM_INDEXED_FRAMEBUFFER_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    indexed_framebuffer_heap(s_indexed_framebuffer ? "reserved" : "zone-fallback");
}

void *DG_AllocIndexedFramebuffer(size_t bytes)
{
    if (bytes != DOOM_INDEXED_FRAMEBUFFER_BYTES || !s_indexed_engine_live ||
        s_indexed_framebuffer == NULL || s_indexed_framebuffer_borrowed) return NULL;
    s_indexed_framebuffer_borrowed = true;
    indexed_framebuffer_heap("borrowed");
    return s_indexed_framebuffer;
}

void DG_ReleaseIndexedFramebuffer(void *buffer)
{
    /* The engine drops its alias here. Physical free belongs to terminal
     * composite cleanup, including if shutdown is called inside a live tick. */
    if (buffer == s_indexed_framebuffer && s_indexed_framebuffer_borrowed) {
        s_indexed_framebuffer_borrowed = false;
    }
}

static bool indexed_framebuffer_cleanup(void)
{
    if (s_indexed_engine_live) return false;
    /* Shutdown detaches first, and knows whether to release our loan or zone. */
    I_ShutdownGraphics();
    if (s_indexed_framebuffer_borrowed) return false;
    if (s_indexed_framebuffer != NULL) {
        heap_caps_free(s_indexed_framebuffer);
        s_indexed_framebuffer = NULL;
        indexed_framebuffer_heap("released");
    }
    return true;
}
#endif


static doom_touch_input_t s_touch_input;
static platform_i2c_shared_t *s_shared_bus;
static platform_touch_t *s_touch;
#if CONFIG_P4_BOARD_M5STACK_TAB5
static platform_touch_sampler_t *s_touch_sampler;
#endif
static bool s_touch_ready;
static bool s_touch_cleanup_proven = true;
#if !CONFIG_P4_BOARD_M5STACK_TAB5
static uint32_t s_last_touch_poll_ms;
#endif
static uint32_t s_last_touch_retry_ms;
static uint32_t s_last_touch_degraded_log_ms;
static uint32_t s_touch_polls;
static uint32_t s_touch_poll_failures;
static uint32_t s_touch_retries;

#if P4_DOOM_SHARED_GAMEPAD
static doom_gamepad_input_t s_gamepad_input;
static doom_key_merge_t s_merged_keys;
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

_Static_assert(SCREENWIDTH == DOOM_VIDEO_WIDTH && SCREENHEIGHT == DOOM_VIDEO_HEIGHT,
               "engine raster and presentation packet dimensions must match");
#if CONFIG_P4_BOARD_M5STACK_TAB5
_Static_assert(DOOM_VIDEO_WIDTH == 768U && DOOM_VIDEO_HEIGHT == 480U,
               "maintained Tab5 renders the native game surface");
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

/* Cumulative foreground measurements, in microseconds. Each phase is
 * inclusive: net covers only these DG wrappers (including adapter USB polls),
 * and DG includes the subphases plus intentional sleep. Other engine network
 * calls remain outside DG, so elapsed minus DG is not pure rendering time.
 * Do not add overlapping phase totals. Counters start at the first gameplay
 * DrawFrame; display maxima retain their separate display-init history.
 * Logging is measured separately because its own report can only include
 * the preceding reports. No additional per-frame display lock is acquired. */
#if !P4_DOOM_USB_DEBUG
typedef enum {
    DOOM_PERF_INTERVAL,
    DOOM_PERF_DG,
    DOOM_PERF_COMPOSE,
    DOOM_PERF_SUBMIT,
    DOOM_PERF_NET,
    DOOM_PERF_DEBUG,
    DOOM_PERF_TOUCH,
    DOOM_PERF_GAMEPAD,
    DOOM_PERF_SLEEP,
    DOOM_PERF_LOG,
    DOOM_PERF_COUNT
} doom_perf_phase_t;

#endif
#if P4_DOOM_USB_DEBUG
#include "doom_diagnostics.h"
static doom_perf_state_t s_perf;

static uint64_t doom_perf_now(void)
{
    return (uint64_t)esp_timer_get_time();
}

/* Sole engine-owner state. No collector is called from map/checkpoint hooks.
 * Deferred diagnostics receives only value copies of these observations. */
static doom_memory_diag_t s_memory_diag;
static uint64_t s_memory_pending_us[DOOM_MEMORY_PHASE_COUNT];
static uint32_t s_memory_pending_mask;

static void doom_memory_observe(doom_memory_phase_t phase, uint64_t event_us)
{
    if (!s_memory_diag.present || (unsigned)phase >= DOOM_MEMORY_PHASE_COUNT) return;
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const uint64_t observed_us = doom_perf_now();
    s_memory_diag.phases[phase] = (doom_memory_observation_t){
        .event_us = event_us, .observed_us = observed_us,
        .free_bytes = info.total_free_bytes,
        .largest_bytes = info.largest_free_block,
        .boot_min_free = info.minimum_free_bytes,
    };
    s_memory_diag.valid_mask |= UINT32_C(1) << (unsigned)phase;
    if (!s_memory_diag.sample_count ||
        info.total_free_bytes < s_memory_diag.sampled_min_free)
        s_memory_diag.sampled_min_free = info.total_free_bytes;
    if (s_memory_diag.sample_count < UINT32_MAX) ++s_memory_diag.sample_count;
}

static void doom_memory_begin(void)
{
    memset(&s_memory_diag, 0, sizeof(s_memory_diag));
    memset(s_memory_pending_us, 0, sizeof(s_memory_pending_us));
    s_memory_pending_mask = 0U;
    s_memory_diag.present = true;
    s_memory_diag.sampled_since_us = doom_perf_now();
    doom_memory_observe(DOOM_MEMORY_ENGINE_START, s_memory_diag.sampled_since_us);
}

static void doom_memory_mark(doom_memory_phase_t phase)
{
    if (!s_memory_diag.present || (unsigned)phase >= DOOM_MEMORY_PHASE_COUNT) return;
    const uint32_t bit = UINT32_C(1) << (unsigned)phase;
    if ((s_memory_diag.valid_mask | s_memory_pending_mask) & bit) return;
    s_memory_pending_us[phase] = doom_perf_now();
    s_memory_pending_mask |= bit;
}

static void doom_memory_service_events(void)
{
    if (!s_memory_pending_mask) return;
    for (unsigned phase = DOOM_MEMORY_FIRST_MAP;
         phase <= DOOM_MEMORY_CHECKPOINT_RESTORE; ++phase) {
        const uint32_t bit = UINT32_C(1) << phase;
        if (!(s_memory_pending_mask & bit)) continue;
        doom_memory_observe((doom_memory_phase_t)phase, s_memory_pending_us[phase]);
        s_memory_pending_mask &= ~bit;
    }
}

void P4_DoomBeforeZoneAllocation(size_t requested_bytes)
{
    if (!s_memory_diag.present) return;
    size_t resident_bytes = 0U;
    s_memory_diag.zone_admission_error = platform_game_storage_prepare_doom_zone(
        requested_bytes, &resident_bytes);
    s_memory_diag.zone_requested_bytes = requested_bytes;
    s_memory_diag.arena_resident_bytes = resident_bytes;
    doom_memory_observe(DOOM_MEMORY_PRE_ZONE, doom_perf_now());
}

void P4_DoomAfterZoneAllocation(size_t requested_bytes, int allocated)
{
    if (!s_memory_diag.present) return;
    s_memory_diag.zone_requested_bytes = requested_bytes;
    if (s_memory_diag.zone_allocation_attempts < UINT32_MAX)
        ++s_memory_diag.zone_allocation_attempts;
    s_memory_diag.zone_allocation_succeeded = allocated != 0;
    doom_memory_observe(DOOM_MEMORY_POST_ZONE, doom_perf_now());
}

void p4_doom_memory_checkpoint(bool restored)
{
    doom_memory_mark(restored ? DOOM_MEMORY_CHECKPOINT_RESTORE
                              : DOOM_MEMORY_CHECKPOINT_CAPTURE);
}

/* Called only by this engine owner task. No external work or recursion. */
uint64_t P4_DoomEnginePerfNow(void)
{ return s_perf.active ? doom_perf_now() : 0; }

void P4_DoomEnginePerfRecord(p4_doom_engine_perf_phase_t phase,
                           uint64_t elapsed_us)
{
    if (!s_perf.active || (unsigned)phase >= P4_DOOM_ENGINE_PHASE_COUNT) return;
    doom_perf_sample_t *const sample = &s_perf.engine[phase];
    sample->total_us = UINT64_MAX - sample->total_us < elapsed_us
                    ? UINT64_MAX : sample->total_us + elapsed_us;
    if (elapsed_us > sample->max_us) sample->max_us = elapsed_us;
    if (sample->calls < UINT32_MAX) ++sample->calls;
}

static void doom_perf_record(doom_perf_phase_t phase, uint64_t elapsed)
{
    if (!s_perf.active) return;
    doom_perf_sample_t *const sample = &s_perf.phases[phase];
    sample->total_us += elapsed;
    if (elapsed > sample->max_us) sample->max_us = elapsed;
    ++sample->calls;
}

static void doom_perf_end(doom_perf_phase_t phase, uint64_t started)
{
    doom_perf_record(phase, doom_perf_now() - started);
}

static void doom_perf_frame_begin(void)
{
    const uint64_t now = doom_perf_now();
    if (!s_perf.active) {
        s_perf.active = true;
        s_perf.started_us = now;
    } else {
        doom_perf_record(DOOM_PERF_INTERVAL, now - s_perf.previous_frame_us);
    }
    s_perf.previous_frame_us = now;
    ++s_perf.frames;
}

static void doom_perf_dg_begin(void)
{
    if (!s_perf.active) return;
    if (s_perf.dg_depth++ == 0U) s_perf.dg_started_us = doom_perf_now();
}

static void doom_perf_dg_end(void)
{
    if (s_perf.active && s_perf.dg_depth > 0U && --s_perf.dg_depth == 0U)
        doom_perf_end(DOOM_PERF_DG, s_perf.dg_started_us);
}

#else
#define doom_perf_now() UINT64_C(0)
#define doom_perf_record(phase, elapsed) do { (void)(phase); (void)(elapsed); } while (0)
#define doom_perf_end(phase, started) do { (void)(phase); (void)(started); } while (0)
#define doom_perf_frame_begin() ((void)0)
#define doom_perf_dg_begin() ((void)0)
#define doom_perf_dg_end() ((void)0)
#define log_performance_stats(display) ((void)(display))
#define doom_diagnostics_begin() ((void)0)
#define doom_diagnostics_end() ((void)0)
#define doom_memory_begin() ((void)0)
#define doom_memory_service_events() ((void)0)
#endif

/* The engine owner alone captures counters and serializes sampler lifetime.
 * Retain the last copied observation after stop; it is not a final-worker
 * receipt because a sample may complete between this copy and quiescence. */
#if P4_DOOM_USB_DEBUG && CONFIG_P4_BOARD_M5STACK_TAB5
static doom_touch_sampler_diag_t s_touch_sampler_diag;
static uint32_t s_touch_sampler_epoch;

static void touch_sampler_diag_capture(void)
{
    if (s_touch_sampler == NULL) return;
    platform_touch_sampler_stats_t stats;
    if (platform_touch_sampler_snapshot(s_touch_sampler, &stats) != ESP_OK) {
        return; /* Preserve the old observation and its original timestamp. */
    }
    const int64_t observed_us = esp_timer_get_time();
    doom_touch_sampler_diag_t next = {0};
    next.present = true;
    next.active = true; /* Handle ownership, not proof of a running worker. */
    next.stats_valid = true;
    next.epoch = s_touch_sampler_diag.epoch;
    next.captured_us = observed_us >= 0 ? (uint64_t)observed_us : 0U;
    next.samples = stats.samples;
    next.poll_calls = stats.poll_calls;
    next.poll_total_us = stats.poll_total_us;
    next.poll_max_us = stats.poll_max_us;
    next.overflows = stats.overflows;
    next.stale_neutralizations = stats.stale_neutralizations;
    next.producer_stalls = stats.producer_stalls;
    next.recoveries = stats.recoveries;
    next.last_completed_us = stats.last_completed_us;
    next.fault = (int32_t)stats.fault;
    next.age_valid = stats.last_completed_us > 0 &&
        observed_us >= stats.last_completed_us;
    if (next.age_valid) {
        next.age_us = (uint64_t)(observed_us - stats.last_completed_us);
    }
    s_touch_sampler_diag = next;
}

static void touch_sampler_diag_start_result(esp_err_t result)
{
    if (result != ESP_OK && s_touch_sampler == NULL) return;
    memset(&s_touch_sampler_diag, 0, sizeof(s_touch_sampler_diag));
    s_touch_sampler_diag.present = s_touch_sampler != NULL;
    s_touch_sampler_diag.active = s_touch_sampler != NULL;
    if (result == ESP_OK) {
        /* Zero denotes a failed-start teardown-only handle. Epochs are also
         * bound to the existing diagnostic generation and sequence. */
        ++s_touch_sampler_epoch;
        if (s_touch_sampler_epoch == 0U) ++s_touch_sampler_epoch;
        s_touch_sampler_diag.epoch = s_touch_sampler_epoch;
    }
    touch_sampler_diag_capture();
}

static doom_touch_sampler_diag_t touch_sampler_diag_snapshot(void)
{
    touch_sampler_diag_capture();
    doom_touch_sampler_diag_t snapshot = s_touch_sampler_diag;
    /* This field describes ownership now, including retained failed stops;
     * captured_us continues to describe only the counter observation. */
    snapshot.active = s_touch_sampler != NULL;
    return snapshot;
}
#else
#define touch_sampler_diag_capture() ((void)0)
#define touch_sampler_diag_start_result(result) ((void)(result))
#endif

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
#ifdef P4_CONSOLE_OS_EMBEDDED
    p4_doom_arena_score_touch(false);
#endif
}

#ifdef P4_CONSOLE_OS_EMBEDDED
static void service_arena_score_touch(const doom_touch_frame_t *frame)
{
    bool pressed = false;
    for (unsigned i = 0U; i < frame->contact_count; ++i) {
        /* Unsigned offsets also reject contacts before a letterbox margin. */
        const uint32_t x = (uint32_t)frame->contacts[i].x - DOOM_TOUCH_VIEWPORT_LEFT;
        const uint32_t y = (uint32_t)frame->contacts[i].y - DOOM_TOUCH_VIEWPORT_TOP;
        if (x >= DOOM_TOUCH_VIEWPORT_WIDTH || y >= DOOM_TOUCH_VIEWPORT_HEIGHT) continue;
        const uint32_t fx = x *
            DOOM_TOUCH_FRAME_WIDTH / DOOM_TOUCH_VIEWPORT_WIDTH;
        const uint32_t fy = y *
            DOOM_TOUCH_FRAME_HEIGHT / DOOM_TOUCH_VIEWPORT_HEIGHT;
        pressed |= fx >= P4_DOOM_SCORE_LEFT &&
            fx < P4_DOOM_SCORE_LEFT + P4_DOOM_SCORE_WIDTH &&
            fy >= P4_DOOM_SCORE_TOP &&
            fy < P4_DOOM_SCORE_TOP + P4_DOOM_SCORE_HEIGHT;
    }
    p4_doom_arena_score_touch(pressed);
}
#endif

static bool release_retained_touch(void)
{
#if CONFIG_P4_BOARD_M5STACK_TAB5
    /* No client/bus destruction until the sole worker proves quiescence. */
    touch_sampler_diag_capture();
    const esp_err_t stopped = platform_touch_sampler_stop(&s_touch_sampler, 100U);
    if (stopped != ESP_OK) {
        s_touch_ready = false;
        s_touch_cleanup_proven = false;
        log_touch_degraded("touch-sampler-stop", stopped, true);
        return false;
    }
#endif
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
    esp_err_t result = platform_touch_create(&config, &s_touch);
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if (result == ESP_OK) {
        result = platform_touch_sampler_start(s_touch, &s_touch_sampler);
        touch_sampler_diag_start_result(result);
    }
#endif
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
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if (!doom_touch_input_idle(&s_touch_input)) {
        return;
    }
    const uint32_t now_ms = ticks_ms();
    platform_touch_frame_t platform_frame;
    bool debug_frame = false;
#if P4_DOOM_USB_DEBUG
    debug_frame = console_os_debug_touch(&platform_frame);
#endif
    if (debug_frame) {
        /* A debug lease owns this source until its explicit neutral frame.
         * Drop earlier physical history so it cannot replay after release. */
        platform_touch_sampler_discard(s_touch_sampler);
    } else if (!s_touch_ready) {
        neutralize_touch_input();
        /* Controller creation/deletion can wait on the SDK bus lock.
         * Keep all such lifecycle work outside the renderer, including a
         * retry after failed initial creation. Remote input remains usable. */
        return;
    }

    /* Consume copied transitions only. Hardware sampling runs at 60 Hz in
     * the platform worker; neither a fresh read nor an error waits for I2C.
     * Stop at the first emitted Doom transition, then DG_GetKey drains it. */
    for (unsigned i = 0U; i < PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY; ++i) {
        bool available = debug_frame;
        const esp_err_t result = debug_frame ? ESP_OK :
            platform_touch_sampler_read(s_touch_sampler, &platform_frame,
                                        &available);
        if (result == ESP_OK && !available) {
            return;
        }
        ++s_touch_polls; /* Consumed reports; raw samples have worker counters. */
        doom_touch_frame_t touch_frame;
        const bool converted = doom_touch_audio_frame_from_platform(
            result == ESP_OK ? &platform_frame : NULL, &touch_frame);
        if (result != ESP_OK || !converted) {
            ++s_touch_poll_failures;
            neutralize_touch_input();
            log_touch_degraded(debug_frame ? "usb-debug-touch" : "touch-sampler",
                result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE, false);
            if (!debug_frame) {
                s_touch_ready = false;
                s_touch_cleanup_proven = false;
                s_last_touch_retry_ms = now_ms;
                /* Retain touch and bus. SDK device deletion itself has an
                 * unbounded bus-lock wait, so it is never attempted here.
                 * A zero-wait stop also retains a still-active worker. */
                touch_sampler_diag_capture();
                (void)platform_touch_sampler_stop(&s_touch_sampler, 0U);
            }
            return;
        }
#ifdef P4_CONSOLE_OS_EMBEDDED
        service_arena_score_touch(&touch_frame);
#endif
        (void)doom_touch_input_update(&s_touch_input, &touch_frame);
        if (debug_frame || !doom_touch_input_idle(&s_touch_input)) {
            return;
        }
    }
#else

    if (!doom_touch_input_idle(&s_touch_input)) {
        return;
    }
    const uint32_t now_ms = ticks_ms();
    if ((uint32_t)(now_ms - s_last_touch_poll_ms) <
        TOUCH_POLL_INTERVAL_MS) {
        return;
    }
    platform_touch_frame_t platform_frame;
    bool debug_frame = false;
#if P4_DOOM_USB_DEBUG
    debug_frame = console_os_debug_touch(&platform_frame);
#endif
    if (!s_touch_ready && !debug_frame) {
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
    s_last_touch_poll_ms = now_ms;
    ++s_touch_polls;

    const esp_err_t result = debug_frame
        ? ESP_OK : platform_touch_poll(s_touch, &platform_frame);
    doom_touch_frame_t touch_frame;
    const bool converted = doom_touch_audio_frame_from_platform(
        result == ESP_OK ? &platform_frame : NULL, &touch_frame);
    if (result != ESP_OK || !converted) {
        ++s_touch_poll_failures;
        neutralize_touch_input();
        log_touch_degraded(debug_frame ? "usb-debug-touch" : "gt911-poll",
                           result != ESP_OK ? result
                                            : ESP_ERR_INVALID_RESPONSE,
                           false);
        if (!debug_frame) {
            s_touch_ready = false;
            s_last_touch_retry_ms = now_ms;
            (void)release_retained_touch();
        }
        return;
    }
#ifdef P4_CONSOLE_OS_EMBEDDED
    service_arena_score_touch(&touch_frame);
#endif
    (void)doom_touch_input_update(&s_touch_input, &touch_frame);

#endif
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

#if P4_DOOM_USB_DEBUG
static void merge_debug_gamepad(gamepad_state_t *state, uint32_t buttons)
{
    buttons &= P4_BUTTON_MASK;
    if (buttons == 0U) {
        return;
    }
    /* This is an application-local copy. Do not publish a synthetic USB/BLE
     * connection or resurrect controls from a disconnected physical pad. */
    if (state->connected == 0U) {
        gamepad_state_init(state);
    }
    state->connected = 1U;
    if ((buttons & P4_BUTTON_UP) != 0U) state->dpad |= GAMEPAD_DPAD_UP;
    if ((buttons & P4_BUTTON_DOWN) != 0U) state->dpad |= GAMEPAD_DPAD_DOWN;
    if ((buttons & P4_BUTTON_LEFT) != 0U) state->dpad |= GAMEPAD_DPAD_LEFT;
    if ((buttons & P4_BUTTON_RIGHT) != 0U) state->dpad |= GAMEPAD_DPAD_RIGHT;
    if ((buttons & P4_BUTTON_A) != 0U)
        state->buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH);
    if ((buttons & P4_BUTTON_B) != 0U)
        state->buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST);
    if ((buttons & P4_BUTTON_START) != 0U)
        state->buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_START);
    if ((buttons & P4_BUTTON_BACK) != 0U)
        state->buttons |= GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_BACK);
}
#endif

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

    gamepad_state_t combined = *state;
#if P4_DOOM_USB_DEBUG
    merge_debug_gamepad(&combined, console_os_debug_buttons());
#endif
    const gamepad_status_t input_result =
        doom_gamepad_input_update(&s_gamepad_input, &combined);
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

#if P4_DOOM_STARTUP_UI
static void startup_failure_before_cleanup(void);
#endif
#ifdef P4_CONSOLE_OS_EMBEDDED
static bool s_terminal_close_engine_wads;
static void close_engine_wads(void);
#endif

static void composite_cleanup(void)
{
    if (s_cleanup_active || s_cleanup_complete) {
        return;
    }
    s_cleanup_active = true;
    doom_diagnostics_end();
    const bool audio_released = release_audio();
#ifdef P4_CONSOLE_OS_EMBEDDED
    if (audio_released && s_terminal_close_engine_wads) {
        close_engine_wads();
        s_terminal_close_engine_wads = false;
    }
#endif
#if P4_DOOM_STARTUP_UI
    /* A terminal join failure must remain legible before scanout is retired.
     * The audio path is already stopped; normal exits never wait here. */
    if (audio_released) startup_failure_before_cleanup();
#endif
    const bool touch_released = release_retained_touch();
#if P4_DOOM_INDEXED_DRAM_ENABLED
    const bool indexed_released = indexed_framebuffer_cleanup();
#else
    const bool indexed_released = true;
#endif


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
#if !CONFIG_P4_BOARD_M5STACK_TAB5
            free(s_overlay_buffer);
#endif
            s_overlay_buffer = NULL;
#if CONFIG_P4_BOARD_M5STACK_TAB5
            DG_ScreenBuffer = NULL;
#endif
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
    s_cleanup_complete = indexed_released && audio_released && touch_released && bus_released &&
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

static _Noreturn void halt_dark(const char *stage, esp_err_t error)
{
#if P4_DOOM_INDEXED_DRAM_ENABLED
    /* This path never returns to a suspended engine stack. Error/quit
     * callbacks that may return must not clear this guard. */
    s_indexed_engine_live = false;
#endif
    composite_cleanup();
    ESP_LOGE(TAG, "P4_DOOM_E6 HALT stage=%s error=%s",
             stage, esp_err_to_name(error));
    for (;;) {
#ifdef P4_CONSOLE_OS_EMBEDDED
        /* A retained audio/resource owner may release on a later retry.
         * Reevaluate the same safe-restart gate after every cleanup attempt. */
        if (s_console_os_launch_active && s_cleanup_complete) {
            ESP_LOGE(TAG,
                     "P4_DOOM_E6 RECOVERY action=restart-to-console "
                     "stage=%s delay_ms=500",
                     stage);
            vTaskDelay(pdMS_TO_TICKS(500U));
            esp_restart();
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(1000U));
        composite_cleanup();
    }
}

#if P4_DOOM_STARTUP_UI
/* Arena startup presentation state. */
#define DOOM_STARTUP_REFRESH_MS UINT32_C(500)
#define DOOM_STARTUP_FAILURE_HOLD_MS UINT32_C(2000)
static bool s_startup_ui_active;
static bool s_startup_ui_waiting;
static bool s_startup_ui_drawing;
static uint32_t s_startup_ui_started_ms;
static uint32_t s_startup_ui_last_ms;
static p4_doom_engine_loading_phase_t s_startup_engine_phase;
static bool s_startup_engine_created;
static uint32_t s_startup_engine_completed, s_startup_engine_total;
static p4_doom_loading_progress_t s_startup_net_progress;
static unsigned s_startup_ui_last_phase;
static bool s_startup_failure_visible;
static uint32_t s_startup_failure_visible_ms;
#endif

#if P4_DOOM_STARTUP_UI
static void startup_text_scaled(const char *text, unsigned y, unsigned scale)
{
    if (text == NULL || y >= DOOM_TOUCH_FRAME_HEIGHT || scale == 0U) return;
    /* Layout and touch units remain canonical. Sample the original font at
     * the actual destination resolution instead of enlarging a painted frame. */
    unsigned width = 0U;
    size_t length = 0U;
    while (length < 48U && text[length] != '\0') {
        const unsigned char ch = (unsigned char)text[length++];
        width += p4_ui_font_advance[ch >= 32U && ch <= 126U ? ch - 32U : 31U] / scale;
    }
    unsigned x = width < DOOM_TOUCH_FRAME_WIDTH
        ? (DOOM_TOUCH_FRAME_WIDTH - width) / 2U : 0U;
    const unsigned top = y * DOOM_VIDEO_HEIGHT / DOOM_TOUCH_FRAME_HEIGHT;
    const unsigned glyph_h = (28U / scale) * DOOM_VIDEO_HEIGHT / DOOM_TOUCH_FRAME_HEIGHT;
    const unsigned glyph_w = (24U / scale) * DOOM_VIDEO_WIDTH / DOOM_TOUCH_FRAME_WIDTH;
    for (size_t i = 0U; i < length && x < DOOM_TOUCH_FRAME_WIDTH; ++i) {
        const unsigned char ch = (unsigned char)text[i];
        const unsigned left = x * DOOM_VIDEO_WIDTH / DOOM_TOUCH_FRAME_WIDTH;
        for (unsigned gy = 0U; gy < glyph_h && gy < DOOM_VIDEO_HEIGHT - top; ++gy) {
            uint32_t *row = s_overlay_buffer + (top + gy) * DOOM_VIDEO_WIDTH + left;
            for (unsigned gx = 0U; gx < glyph_w && gx < DOOM_VIDEO_WIDTH - left; ++gx) {
                const unsigned fx = gx * DOOM_TOUCH_FRAME_WIDTH * scale / DOOM_VIDEO_WIDTH;
                const unsigned fy = gy * DOOM_TOUCH_FRAME_HEIGHT * scale / DOOM_VIDEO_HEIGHT;
                if (p4_ui_glyph_alpha(ch, fx, fy) >= 8U) row[gx] = UINT32_C(0x00ffffff);
            }
        }
        x += p4_ui_font_advance[ch >= 32U && ch <= 126U ? ch - 32U : 31U] / scale;
    }
}

/* Engine loops only copy bounded scalar progress. They do not redraw each
 * item or enter the network/storage service from a loading callback. */
void P4_DoomLoadingProgress(p4_doom_engine_loading_phase_t phase,
                          uint32_t completed, uint32_t total)
{
#if P4_DOOM_USB_DEBUG
    if (phase == P4_DOOM_ENGINE_LOADING_MAP && total && completed >= total)
        doom_memory_mark(DOOM_MEMORY_FIRST_MAP);
#endif
    if (!s_startup_ui_active) return;
    /* Checkpoint restore may call G_InitNew after Create returns. Its map
     * callbacks must not hide the authoritative transfer/catch-up stage. */
    if (phase == P4_DOOM_ENGINE_LOADING_READY) s_startup_engine_created = true;
    s_startup_engine_phase = phase;
    s_startup_engine_completed = total && completed > total ? total : completed;
    s_startup_engine_total = total;
}

static void startup_progress_info(const char **stage, const char **detail,
                                 const char **unit, uint32_t *completed,
                                 uint32_t *total, unsigned *phase)
{
    *stage = "2/3 Prepare engine";
    *detail = "Reading game data";
    *unit = "items";
    *completed = s_startup_engine_completed;
    *total = s_startup_engine_total;
    *phase = (unsigned)s_startup_engine_phase;
    switch (s_startup_engine_phase) {
        case P4_DOOM_ENGINE_LOADING_TEXTURES:
            *stage = "2/3 Load textures"; *detail = "Texture setup steps"; break;
        case P4_DOOM_ENGINE_LOADING_SPRITES:
            *stage = "2/3 Load sprites"; *detail = "Sprite headers loaded"; break;
        case P4_DOOM_ENGINE_LOADING_MAP:
            *stage = "3/3 Load match"; *detail = "Map setup steps"; break;
        case P4_DOOM_ENGINE_LOADING_READY:
            *stage = "3/3 Start match"; *detail = "Preparing first frame";
            *completed = *total = 0U; break;
        default: break;
    }
    const p4_doom_loading_phase_t net_phase = s_startup_net_progress.phase;
    if (net_phase == P4_DOOM_LOADING_FAILED ||
        ((s_startup_ui_waiting || s_startup_engine_created) &&
         net_phase != P4_DOOM_LOADING_IDLE)) {
        *phase = 16U + (unsigned)net_phase;
        *completed = s_startup_net_progress.completed;
        *total = s_startup_net_progress.total;
        *stage = "3/3 Sync match";
        switch (net_phase) {
            case P4_DOOM_LOADING_CHECKPOINT:
                *detail = "Receiving match state"; *unit = "bytes"; break;
            case P4_DOOM_LOADING_CATCHUP:
                *detail = "Catching up to host"; *unit = "tics"; break;
            case P4_DOOM_LOADING_READY:
                *stage = "3/3 Start match"; *detail = "Preparing first frame";
                *completed = *total = 0U; break;
            case P4_DOOM_LOADING_FAILED:
                *stage = "Join failed"; *detail = "Returning Home...";
                *completed = *total = 0U; break;
            default:
                *detail = "Waiting for host"; *completed = *total = 0U; break;
        }
    }
    if (*total && *completed > *total) *completed = *total;
}
#endif

static esp_err_t submit_startup_frame(void)
{
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const esp_err_t acquired = doom_video_acquire_xrgb8888(
        &s_overlay_buffer, DOOM_FIRST_FRAME_TIMEOUT_MS);
    if (acquired != ESP_OK) return acquired;
#else
    if (s_overlay_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
#if P4_DOOM_STARTUP_UI
    const uint32_t elapsed_ms = ticks_ms() - s_startup_ui_started_ms;
    const uint32_t phase = (elapsed_ms / DOOM_STARTUP_REFRESH_MS) % 4U;
    const char *stage, *detail, *unit;
    uint32_t completed, total;
    unsigned display_phase;
    startup_progress_info(&stage, &detail, &unit, &completed, &total, &display_phase);
    const bool failed = s_startup_net_progress.phase == P4_DOOM_LOADING_FAILED;
    const unsigned percent = total ? (unsigned)((uint64_t)completed * 100U / total) : 0U;
    const size_t bar_top = s_startup_ui_active ? 104U : 94U;
#else
    const size_t bar_top = 94U;
#endif
    for (size_t y = 0U; y < DOOM_VIDEO_HEIGHT; ++y) {
        const size_t cy = y * DOOM_TOUCH_FRAME_HEIGHT / DOOM_VIDEO_HEIGHT;
        for (size_t x = 0U; x < DOOM_VIDEO_WIDTH; ++x) {
            const size_t cx = x * DOOM_TOUCH_FRAME_WIDTH / DOOM_VIDEO_WIDTH;
            const bool border = cx < 4U || cx >= DOOM_TOUCH_FRAME_WIDTH - 4U ||
                cy < 4U || cy >= DOOM_TOUCH_FRAME_HEIGHT - 4U;
            const bool scanline = (cy % 16U) == 0U;
            const bool center_bar = cy >= bar_top && cy < bar_top + 12U &&
                cx >= 48U && cx < DOOM_TOUCH_FRAME_WIDTH - 48U;
            s_overlay_buffer[y * DOOM_VIDEO_WIDTH + x] = border
                ? UINT32_C(0x0000ffff)
                : center_bar
                    ? UINT32_C(0x000080ff)
                    : scanline
                        ? UINT32_C(0x00001838)
                        : UINT32_C(0x00000818);
#if P4_DOOM_STARTUP_UI
            if (s_startup_ui_active && center_bar) {
                const size_t offset = cx - 48U;
                s_overlay_buffer[y * DOOM_VIDEO_WIDTH + x] =
                    failed ? UINT32_C(0x00802020) :
                    (total ? offset < (size_t)((uint64_t)completed * 224U / total)
                           : offset / 56U == phase && offset % 56U < 40U)
                        ? UINT32_C(0x000080ff) : UINT32_C(0x00001838);
            }
#endif
        }
    }
#if P4_DOOM_STARTUP_UI
    if (s_startup_ui_active) {
        char elapsed[32], progress[64];
        (void)snprintf(elapsed, sizeof(elapsed), "Elapsed %" PRIu32 " s",
            elapsed_ms / 1000U);
        if (total) {
            (void)snprintf(progress, sizeof(progress), "%u%%  %" PRIu32 " / %" PRIu32 " %s",
                           percent, completed, total, unit);
        } else {
            (void)snprintf(progress, sizeof(progress), "%s",
                failed ? "Connection did not complete" : "Working - please wait");
        }
        startup_text_scaled("Doom Arena by Game Changers", 14U, 2U);
        startup_text_scaled(stage, 44U, 1U);
        startup_text_scaled(detail, 78U, 2U);
        startup_text_scaled(progress, 130U, 2U);
        startup_text_scaled(elapsed, 164U, 2U);
    }
#endif
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const esp_err_t submitted = doom_video_publish_xrgb8888(
        true, DOOM_FIRST_FRAME_TIMEOUT_MS);
    s_overlay_buffer = NULL; /* No alias to a worker-owned frame. */
    return submitted;
#else
    return doom_video_submit_xrgb8888(
        s_overlay_buffer, DOOM_VIDEO_WIDTH, DOOM_FIRST_FRAME_TIMEOUT_MS);
#endif
}

#if P4_DOOM_STARTUP_UI
/* Called only by the foreground Arena service, including safe WAD-read
 * heartbeats. Use compiled-in glyphs and the existing overlay: never enter
 * the engine, input, networking or storage from this presentation callback. */
void p4_doom_startup_status(bool waiting)
{
    if (!s_startup_ui_active || s_startup_ui_drawing ||
#if !CONFIG_P4_BOARD_M5STACK_TAB5
        s_overlay_buffer == NULL ||
#endif
        !s_video_initialized || s_cleanup_active ||
        s_frame_error != ESP_OK) return;
    const uint32_t now = ticks_ms();
    s_startup_ui_waiting = waiting;
    /* This accessor is a scalar owner-task snapshot, not a network poll. */
    P4_DoomNetGetLoadingProgress(&s_startup_net_progress);
    const char *stage, *detail, *unit;
    uint32_t completed, total;
    unsigned phase;
    startup_progress_info(&stage, &detail, &unit, &completed, &total, &phase);
    if (phase == s_startup_ui_last_phase &&
        now - s_startup_ui_last_ms < DOOM_STARTUP_REFRESH_MS) return;
    if (phase == 16U + P4_DOOM_LOADING_FAILED && phase == s_startup_ui_last_phase) return;
    s_startup_ui_last_phase = phase;
    s_startup_ui_last_ms = now;
    s_startup_ui_drawing = true;
    const esp_err_t result = submit_startup_frame();
    s_startup_ui_drawing = false;
    if (result != ESP_OK) {
        s_startup_ui_active = false;
        ESP_LOGW(TAG, "P4_DOOM_ARENA STARTUP_UI_DEGRADED error=%d", (int)result);
    } else if (phase == 16U + P4_DOOM_LOADING_FAILED) {
        s_startup_failure_visible = true;
        s_startup_failure_visible_ms = ticks_ms();
    }
}

static void startup_failure_before_cleanup(void)
{
    if (!s_startup_failure_visible) return;
    s_startup_failure_visible = false; /* Cleanup retries never repeat the hold. */
    if (!s_startup_ui_active || !s_video_initialized || s_frame_error != ESP_OK) return;
    while (ticks_ms() - s_startup_failure_visible_ms < DOOM_STARTUP_FAILURE_HOLD_MS) {
#if P4_DOOM_USB_DEBUG
        /* Terminal owner-task boundary only: do not enter engine input or
         * network replay. Raw Back/B can dismiss the automatic return early. */
        console_os_debug_poll();
        if ((console_os_debug_buttons() & (P4_BUTTON_BACK | P4_BUTTON_B)) != 0U) break;
#endif
        const TickType_t delay = pdMS_TO_TICKS(10U);
        vTaskDelay(delay > 0 ? delay : 1);
    }
}
#endif

static void engine_exit_composite(void)
{
#ifndef P4_CONSOLE_OS_EMBEDDED
    composite_cleanup();
#elif P4_DOOM_STARTUP_UI
    p4_doom_loading_progress_t progress;
    P4_DoomNetGetLoadingProgress(&progress);
    if (progress.phase == P4_DOOM_LOADING_FAILED) {
        /* I_Error runs this callback before process exit, so a configure
         * failure cannot return to the outer Tick loop. Take the established
        * non-returning owner cleanup path while the failure frame is valid. */
        p4_doom_startup_status(false);
        s_terminal_close_engine_wads = true;
        halt_dark("join-failed", ESP_FAIL);
    }
#endif
    /* Console cleanup runs after Tick returns. Doom may still invoke input
     * and rendering from the tick containing I_Quit. */
}
#ifdef P4_CONSOLE_OS_EMBEDDED
static void close_engine_wads(void)
{
    /* The tick has finished or the terminal error path cannot return to it,
     * and the audio worker is stopped. Detach aliases before closing each WAD. */
    for (unsigned i = 0; i < numlumps; ++i) {
        wad_file_t *file = lumpinfo[i].wad_file;
        if (!file) continue;
        for (unsigned j = i; j < numlumps; ++j) {
            if (lumpinfo[j].wad_file == file) lumpinfo[j].wad_file = NULL;
        }
        W_CloseFile(file);
    }
}
#endif

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
#if P4_DOOM_USB_DEBUG
    p4_diag_reservation_t reservation;
    if (!doom_diagnostics_reserve(&reservation)) return;
    /* Foreground-owned scratch; the consumer receives only a value copy. */
    static doom_diag_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    doom_memory_observe(DOOM_MEMORY_RUNTIME, doom_perf_now());
    snapshot.captured_us = doom_perf_now();
    snapshot.memory = s_memory_diag;
    snapshot.perf = s_perf;
    snapshot.frame_count = s_frame_count;
    snapshot.touch_polls = s_touch_polls;
    snapshot.touch_failures = s_touch_poll_failures;
    snapshot.touch_retries = s_touch_retries;
#if CONFIG_P4_BOARD_M5STACK_TAB5
    snapshot.touch_sampler = touch_sampler_diag_snapshot();
#endif
    snapshot.composite_gate = s_runtime_gate.composite_authorized != 0U;
    snapshot.touch_gate = s_runtime_gate.touch_authorized != 0U;
    snapshot.audio_gate = s_runtime_gate.audio_authorized != 0U;
    snapshot.display_valid = platform_display_try_get_stats(&snapshot.display) == ESP_OK;
    snapshot.audio_valid = s_audio_lifecycle.runtime_bound &&
        doom_audio_runtime_get_stats(&snapshot.audio) == ESP_OK;
    platform_audio_adapter_get_stats(&snapshot.adapter);
    snapshot.backend_valid = platform_audio_get_telemetry(&snapshot.backend) == ESP_OK;
    P4_DoomNetGetStats(&snapshot.net);
    doom_diagnostics_publish(reservation, &snapshot);
#else

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
    log_performance_stats(&video_stats);
#if P4_DOOM_INDEXED_DRAM_ENABLED
    indexed_framebuffer_heap("runtime");
#endif
#endif
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
#if P4_DOOM_USB_DEBUG
    memset(&s_perf, 0, sizeof(s_perf));
    doom_diagnostics_begin();
#endif
    if (DG_ScreenBuffer == NULL || !s_video_initialized
#if !CONFIG_P4_BOARD_M5STACK_TAB5
        || s_overlay_buffer == NULL
#endif
    ) {
        halt_dark("engine-frame-buffer", ESP_ERR_NO_MEM);
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    /* doomgeneric_Create allocated this before calling DG_Init. Tab5 now
     * expands each palette directly into a worker lease instead. */
    free(DG_ScreenBuffer);
    DG_ScreenBuffer = NULL;
    s_frame_acquire_us = 0U;
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
    ESP_LOGI(TAG, "P4_DOOM_E6 INDEXED_PRESENT enabled=1 palette=gamma_rgb888 "
                 "touch=worker_rgb888 copies=indices_palette");
#endif
#endif
    ESP_LOGI(TAG,
             "P4_DOOM_E6 VIDEO_READY input=0x00RRGGBB overlay=touch "
             "output=rgb565 source=%ux%u surface=%ux%u logical=%ux%u native=%ux%u "
             "rotation_cw=%u viewport=%ux%u margins=%u/%u/%u/%u",
             (unsigned)DOOM_VIDEO_WIDTH,
             (unsigned)DOOM_VIDEO_HEIGHT,
             (unsigned)DOOM_VIDEO_WIDTH,
             (unsigned)DOOM_VIDEO_HEIGHT,
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

#if CONFIG_P4_BOARD_M5STACK_TAB5
int DG_PrepareFrame(void)
{
    doom_perf_dg_begin();
#if P4_DOOM_STARTUP_UI
    /* Net polling may invoke the loading hook; it must not acquire another
     * frame while the engine holds its first presentation lease. */
    s_startup_ui_active = false;
#endif
    if (s_frame_error == ESP_OK) {
        if (!s_video_initialized || DG_ScreenBuffer != NULL ||
            s_overlay_buffer != NULL) {
            s_frame_error = ESP_ERR_INVALID_STATE;
        } else {
            const uint64_t started = doom_perf_now();
            s_frame_error = doom_video_acquire_xrgb8888(
                &s_overlay_buffer, DOOM_SUBMIT_TIMEOUT_MS);
            s_frame_acquire_us = doom_perf_now() - started;
            if (s_frame_error == ESP_OK) {
                DG_ScreenBuffer = s_overlay_buffer;
            } else {
                doom_perf_record(DOOM_PERF_SUBMIT, s_frame_acquire_us);
            }
        }
    }
    if (s_frame_error != ESP_OK) {
        /* A held lease remains owned by the adapter until deinit cancels it. */
        DG_ScreenBuffer = NULL;
        s_overlay_buffer = NULL;
    }
    doom_perf_dg_end();
    return s_frame_error == ESP_OK;
}
#endif

#if CONFIG_P4_BOARD_M5STACK_TAB5 && P4_DOOM_INDEXED_PACKET_EXPERIMENT
/* The engine invokes this only for its exact default indexed presentation.
 * No engine buffer/palette/touch pointer crosses the publication boundary. */
int DG_DrawIndexedFrame(const uint8_t *indices, const uint32_t *palette)
{
    doom_perf_frame_begin();
    doom_perf_dg_begin();
#if P4_DOOM_STARTUP_UI
    s_startup_ui_active = false;
#endif
    uint64_t phase_started = doom_perf_now();
    P4_DoomNetPoll();
    doom_perf_end(DOOM_PERF_NET, phase_started);
    if (s_frame_error != ESP_OK) goto done;
    if (!indices || !palette || !s_video_initialized ||
        DG_ScreenBuffer != NULL || s_overlay_buffer != NULL) {
        s_frame_error = ESP_ERR_INVALID_STATE;
        goto done;
    }
    uint8_t *leased_indices = NULL;
    uint32_t *leased_palette = NULL;
    phase_started = doom_perf_now();
    s_frame_error = doom_video_acquire_indexed(
        &leased_indices, &leased_palette, DOOM_SUBMIT_TIMEOUT_MS);
    const uint64_t acquire_us = doom_perf_now() - phase_started;
    if (s_frame_error != ESP_OK) {
        doom_perf_record(DOOM_PERF_SUBMIT, acquire_us);
        goto done;
    }
    phase_started = doom_perf_now();
    /* Match compose_frame's validation and neutral recovery before snapshot. */
    const uint32_t mask = (UINT32_C(1) << DOOM_TOUCH_ACTION_COUNT) - UINT32_C(1);
    if (s_touch_input.version != DOOM_TOUCH_INPUT_VERSION ||
        s_touch_input.size != sizeof(s_touch_input) ||
        s_touch_input.read_index >= DOOM_TOUCH_EVENT_CAPACITY ||
        s_touch_input.event_count > DOOM_TOUCH_EVENT_CAPACITY ||
        (s_touch_input.active_actions & ~mask) != 0U) {
        doom_touch_input_init(&s_touch_input);
        ESP_LOGW(TAG,
            "P4_DOOM_E6 TOUCH_DEGRADED stage=input-model-reset "
            "error=ESP_ERR_INVALID_STATE neutral=1 overlay=visible");
    }
    memcpy(leased_indices, indices, (size_t)DOOM_VIDEO_WIDTH * DOOM_VIDEO_HEIGHT);
    memcpy(leased_palette, palette, 256U * sizeof(*leased_palette));
    const uint32_t active_actions = s_touch_input.active_actions;
    doom_perf_end(DOOM_PERF_COMPOSE, phase_started);
    phase_started = doom_perf_now();
    s_frame_error = doom_video_publish_indexed(
        active_actions, false, DOOM_SUBMIT_TIMEOUT_MS);
    /* Locals must not be dereferenced after publication, even on failure. */
    doom_perf_record(DOOM_PERF_SUBMIT, acquire_us + doom_perf_now() - phase_started);
    phase_started = doom_perf_now();
    P4_DoomNetPollFrameTail(s_frame_error == ESP_OK &&
        ((s_frame_count + 1U) % DOOM_STATS_INTERVAL_FRAMES) != 0U);
    doom_perf_end(DOOM_PERF_NET, phase_started);
    if (s_frame_error != ESP_OK) goto done;
    ++s_frame_count;
    doom_perf_dg_end();
    if ((s_frame_count % DOOM_STATS_INTERVAL_FRAMES) == 0U) {
        phase_started = doom_perf_now();
        log_runtime_stats();
        doom_perf_end(DOOM_PERF_LOG, phase_started);
    }
    return 1;
done:
    /* A lease acquired before an exceptional exit is canceled by deinit.
     * A negative result suppresses the legacy fallback after a hard error. */
    doom_perf_dg_end();
    return -1;
}
#endif

void DG_DrawFrame(void)
{
    doom_perf_frame_begin();
    doom_perf_dg_begin();
#if P4_DOOM_STARTUP_UI
    /* Retire before network polling can invoke the startup hook. Gameplay
     * owns this buffer from its first frame, even if that submit fails. */
    s_startup_ui_active = false;
#endif
    uint64_t phase_started = doom_perf_now();
    P4_DoomNetPoll();
    doom_perf_end(DOOM_PERF_NET, phase_started);
    if (s_frame_error != ESP_OK) goto done;
    if (DG_ScreenBuffer == NULL || !s_video_initialized ||
        s_overlay_buffer == NULL
#if CONFIG_P4_BOARD_M5STACK_TAB5
        || DG_ScreenBuffer != s_overlay_buffer
#endif
    ) {
        s_frame_error = ESP_ERR_INVALID_STATE;
        goto done;
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const uint64_t acquire_us = s_frame_acquire_us;
    s_frame_acquire_us = 0U;
#endif
    phase_started = doom_perf_now();
    if (!doom_touch_audio_compose_frame_sized(
            (const uint32_t *)DG_ScreenBuffer, DOOM_VIDEO_WIDTH,
            s_overlay_buffer, DOOM_VIDEO_WIDTH, DOOM_VIDEO_WIDTH, DOOM_VIDEO_HEIGHT,
            &s_touch_input)) {
        doom_touch_input_init(&s_touch_input);
        if (!doom_touch_audio_compose_frame_sized(
                (const uint32_t *)DG_ScreenBuffer, DOOM_VIDEO_WIDTH,
                s_overlay_buffer, DOOM_VIDEO_WIDTH, DOOM_VIDEO_WIDTH, DOOM_VIDEO_HEIGHT,
                &s_touch_input)) {
            s_frame_error = ESP_ERR_INVALID_STATE;
            doom_perf_end(DOOM_PERF_COMPOSE, phase_started);
            goto done;
        }
        ESP_LOGW(TAG,
                 "P4_DOOM_E6 TOUCH_DEGRADED stage=input-model-reset "
                 "error=ESP_ERR_INVALID_STATE neutral=1 overlay=visible");
    }
    doom_perf_end(DOOM_PERF_COMPOSE, phase_started);
    phase_started = doom_perf_now();
#if CONFIG_P4_BOARD_M5STACK_TAB5
    s_frame_error = doom_video_publish_xrgb8888(false, DOOM_SUBMIT_TIMEOUT_MS);
    /* Success transfers ownership; failure also invalidates the API pointer. */
    DG_ScreenBuffer = NULL;
    s_overlay_buffer = NULL;
#else
    s_frame_error = doom_video_submit_xrgb8888(
        s_overlay_buffer, DOOM_VIDEO_WIDTH, DOOM_SUBMIT_TIMEOUT_MS);
#endif
#if CONFIG_P4_BOARD_M5STACK_TAB5
    /* One combined foreground submit sample per frame, excluding compose. */
    doom_perf_record(DOOM_PERF_SUBMIT, acquire_us + doom_perf_now() - phase_started);
#else
    doom_perf_end(DOOM_PERF_SUBMIT, phase_started);
#endif
    phase_started = doom_perf_now();
    /* Wipes, errors and a logging frame retain forced RX before returning. */
    P4_DoomNetPollFrameTail(s_frame_error == ESP_OK &&
        ((s_frame_count + 1U) % DOOM_STATS_INTERVAL_FRAMES) != 0U);
    doom_perf_end(DOOM_PERF_NET, phase_started);
    if (s_frame_error != ESP_OK) goto done;
    ++s_frame_count;
    doom_perf_dg_end();
    if ((s_frame_count % DOOM_STATS_INTERVAL_FRAMES) == 0U) {
        phase_started = doom_perf_now();
        log_runtime_stats();
        doom_perf_end(DOOM_PERF_LOG, phase_started);
    }
    return;
done:
#if CONFIG_P4_BOARD_M5STACK_TAB5
    /* On errors before publish, deinit cancels the adapter's held lease. */
    DG_ScreenBuffer = NULL;
    s_overlay_buffer = NULL;
#endif
    doom_perf_dg_end();
}

void DG_SleepMs(uint32_t milliseconds)
{
    doom_perf_dg_begin();
    uint64_t phase_started = doom_perf_now();
#if P4_DOOM_USB_DEBUG
    console_os_debug_poll();
    doom_perf_end(DOOM_PERF_DEBUG, phase_started);
    phase_started = doom_perf_now();
#endif
    P4_DoomNetPoll();
    doom_perf_end(DOOM_PERF_NET, phase_started);
    const uint32_t bounded = milliseconds > DOOM_MAX_SLEEP_MS
        ? DOOM_MAX_SLEEP_MS
        : milliseconds;
    phase_started = doom_perf_now();
    if (bounded == 0U) {
        taskYIELD();
    } else {
        const TickType_t ticks = pdMS_TO_TICKS(bounded);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
    doom_perf_end(DOOM_PERF_SLEEP, phase_started);
    doom_perf_dg_end();
}

uint32_t DG_GetTicksMs(void)
{
    return ticks_ms();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    doom_perf_dg_begin();
    uint64_t phase_started = doom_perf_now();
    int result = 0;
#if P4_DOOM_USB_DEBUG
    console_os_debug_poll();
    doom_perf_end(DOOM_PERF_DEBUG, phase_started);
#endif
    if (pressed == NULL || key == NULL) goto done;
    *pressed = 0;
    *key = 0U;
#if P4_DOOM_SHARED_GAMEPAD
    phase_started = doom_perf_now();
    service_gamepad();
    doom_perf_end(DOOM_PERF_GAMEPAD, phase_started);
    doom_gamepad_event_t gamepad_event;
    while (doom_gamepad_input_next(&s_gamepad_input, &gamepad_event)) {
        unsigned char mapped_key = 0U;
        if (!doom_gamepad_action_key(gamepad_event.action, &mapped_key)) {
            continue;
        }
        if (!doom_key_merge_update(&s_merged_keys, DOOM_KEY_SOURCE_GAMEPAD,
                                   mapped_key, gamepad_event.pressed == 1U)) {
            continue;
        }
        *pressed = gamepad_event.pressed == 1U ? 1 : 0;
        *key = mapped_key;
        result = 1;
        goto done;
    }
#endif
    phase_started = doom_perf_now();
    service_touch();
    doom_perf_end(DOOM_PERF_TOUCH, phase_started);
    doom_touch_event_t event;
    while (doom_touch_input_next(&s_touch_input, &event)) {
        unsigned char mapped_key = 0U;
        if (!doom_touch_audio_action_key(event.action, &mapped_key)) {
            continue;
        }
#if P4_DOOM_SHARED_GAMEPAD
        if (!doom_key_merge_update(&s_merged_keys, DOOM_KEY_SOURCE_TOUCH,
                                   mapped_key, event.pressed == 1U)) {
            continue;
        }
#endif
        *pressed = event.pressed == 1U ? 1 : 0;
        *key = mapped_key;
        result = 1;
        goto done;
    }
done:
    doom_perf_dg_end();
    return result;
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
    /* Console step zero means mute. Invalid values also fail silent; neither
     * may turn a muted game into the standalone application's default 8/10. */
    s_backend_volume_step = master_volume_step <= DOOM_BACKEND_VOLUME_MAX_STEP
        ? master_volume_step : 0U;
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
    doom_key_merge_init(&s_merged_keys);
#endif
    s_audio_gate_enabled =
        mode == DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO;
    s_audio_calls_allowed = doom_touch_audio_runtime_sound_allowed(
        &s_runtime_gate, s_backend_volume_step);
    if (s_audio_gate_enabled && !s_audio_calls_allowed) {
        ESP_LOGI(TAG, "P4_DOOM USER_MUTE volume_step=%u audio_initialization=skipped",
                 (unsigned)s_backend_volume_step);
    }
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
    const bool arena = title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI;
    const size_t wad_size = arena ? (size_t)PLATFORM_GAME_STORAGE_ARENA_BASE_WAD_BYTES : chex
        ? (size_t)PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES
        : (size_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
    const char *const wad_file_name = arena ? "freedoom2.wad" : chex ? "chex.wad" : "doom1.wad";
    const char *const wad_path = arena ? "/doom/freedoom2.wad" : chex ? "/doom/chex.wad" : EMBEDDED_WAD_PATH;
    const char *const wad_identity = arena ? P4_GCA_BASE_IDENTITY : chex
        ? "chex-quest-1.0" : "doom-shareware-1.9";
    const char *const wad_sha256 = arena ? P4_GCA_BASE_SHA256_HEX : chex
        ? "d8eb5277918883f490fb1a4be3c9a8588df2dbaee6dc4beb8df4929148bbffb1"
        : "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771";
#else
    const bool chex = false;
    const bool arena = false;
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
#ifdef P4_CONSOLE_OS_EMBEDDED
    if (arena) {
        result=verify_readonly_vfs("/doom/purehades.wad",
            (size_t)PLATFORM_GAME_STORAGE_PUREHADES_WAD_BYTES,true);
        if (result!=ESP_OK) halt_dark("arena-pwad-vfs-readback",result);
        result=verify_readonly_vfs("/doom/dwango5.wad",
            (size_t)PLATFORM_GAME_STORAGE_DWANGO5_WAD_BYTES,true);
        if (result!=ESP_OK) halt_dark("arena-dwango-vfs-readback",result);
        ESP_LOGI(TAG,"P4_DOOM_ARENA CONTENT pure-hades=0.6 dwango5=24 default=01,02,03,04,05 vote=majority midi=embedded");
    }
#endif
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
#if !CONFIG_P4_BOARD_M5STACK_TAB5
    s_overlay_buffer = heap_caps_calloc(
        OVERLAY_PIXELS, sizeof(*s_overlay_buffer),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_overlay_buffer == NULL) {
        halt_dark("overlay-buffer", ESP_ERR_NO_MEM);
    }
#endif
#if P4_DOOM_STARTUP_UI
    s_startup_ui_active = arena;
    s_startup_ui_waiting = false;
    s_startup_ui_drawing = false;
    s_startup_engine_phase = P4_DOOM_ENGINE_LOADING_PREPARING;
    s_startup_engine_created = false;
    s_startup_engine_completed = s_startup_engine_total = 0U;
    s_startup_net_progress = (p4_doom_loading_progress_t){0};
    s_startup_ui_last_phase = P4_DOOM_ENGINE_LOADING_PREPARING;
    s_startup_failure_visible = false;
    s_startup_failure_visible_ms = 0U;
    s_terminal_close_engine_wads = false;
    s_startup_ui_started_ms = ticks_ms();
    s_startup_ui_last_ms = s_startup_ui_started_ms;
#endif
    result = submit_startup_frame();
    if (result != ESP_OK) {
        halt_dark("startup-frame", result);
    }

    const bool sound_enabled = try_audio_enable();
    result = platform_display_set_brightness(25U);
    if (result != ESP_OK) {
        halt_dark("backlight", result);
    }

    char *engine_argv[11] = {"doom", "-iwad", (char *)wad_path,
                              "-gfxmode", "rgba8888"};
    int engine_argc=5;
    if (arena) {
        engine_argv[engine_argc++]="-file";
        engine_argv[engine_argc++]="/doom/purehades.wad";
        engine_argv[engine_argc++]="/doom/dwango5.wad";
    }
    if (!sound_enabled) {
        engine_argv[engine_argc++]="-nosound";
        engine_argv[engine_argc++]="-nomusic";
    }
#if defined(P4_CONSOLE_OS_EMBEDDED) && CONFIG_P4_BOARD_M5STACK_TAB5
    doom_memory_begin();
    ESP_LOGI(TAG,
             "P4_DOOM MEMORY stage=engine-start internal_free=%u "
             "internal_largest=%u dma_free=%u dma_largest=%u psram_free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
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
#if P4_DOOM_INDEXED_DRAM_ENABLED
    indexed_framebuffer_reserve();
    s_indexed_engine_live = true;
#endif
    doomgeneric_Create(engine_argc, engine_argv);
    P4_DoomLoadingProgress(P4_DOOM_ENGINE_LOADING_READY, 0U, 0U);
    verify_audio_start_or_safe_degrade(sound_enabled);
#if P4_DOOM_USB_DEBUG
    doom_memory_observe(DOOM_MEMORY_ENGINE_READY, doom_perf_now());
#endif
    doom_memory_service_events();
#if P4_DOOM_INDEXED_DRAM_ENABLED
    indexed_framebuffer_heap("engine-ready");
#endif
    key_prevweapon = '[';
    key_nextweapon = ']';
    if (s_frame_error != ESP_OK) {
        halt_dark("engine-first-frame", s_frame_error);
    }
#ifdef P4_CONSOLE_OS_EMBEDDED
    /* The upstream ENDOOM callback calls process exit(), which aborts under
     * ESP-IDF before the platform quit/cleanup callback can return us home. */
    if (!M_SetVariable("show_endoom", "0")) {
        halt_dark("quit-config", ESP_ERR_INVALID_STATE);
    }
#endif
    for (;;) {
        doomgeneric_Tick();
        doom_memory_service_events();
        if (doomgeneric_QuitRequested()) {
            /* Tick has returned; retire the snapshot generation before resources. */
            doom_diagnostics_end();
#if P4_DOOM_INDEXED_DRAM_ENABLED
            /* Tick and all exit callbacks have returned; engine use is over. */
            s_indexed_engine_live = false;
#endif
#ifdef P4_CONSOLE_OS_EMBEDDED
            if (release_audio()) close_engine_wads();
            composite_cleanup();
#endif
            ESP_LOGI(TAG,
                     "P4_DOOM_E6 EXIT status=confirmed action=restart-to-home "
                     "cleanup_complete=%u",
                     s_cleanup_complete ? 1U : 0U);
            esp_restart();
        }
        if (s_frame_error != ESP_OK) {
            halt_dark("engine-frame", s_frame_error);
        }
#if defined(P4_CONSOLE_OS_EMBEDDED) && CONFIG_P4_BOARD_M5STACK_TAB5
        /* Live catch-up and replay can finish without sleeping. A positive
         * scheduler block lets core-0 idle run even when touch and display
         * complete asynchronously; taskYIELD() cannot give idle this turn. */
        vTaskDelay(1U);
#endif
    }
}
