#!/usr/bin/env python3
"""Run the production startup painter with real font data and a display sink."""

from pathlib import Path
import os
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c"


def function(source: str, name: str) -> str:
    match = re.search(rf"^(?:static )?(?:_Noreturn )?(?:void|int|esp_err_t) {name}\([^;]*?\)\s*\{{", source, re.M)
    assert match is not None, f"Missing production loading presentation: {name}"
    start = match.start()
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "p4/presentation_font.h"
#define P4_CONSOLE_OS_EMBEDDED 1
#define P4_DOOM_STARTUP_UI 1
/* LOADING_API */
static p4_doom_loading_progress_t network_progress;
void P4_DoomNetGetLoadingProgress(p4_doom_loading_progress_t *out)
{ *out = network_progress; }
#define DOOM_TOUCH_FRAME_WIDTH 320U
#define DOOM_TOUCH_FRAME_HEIGHT 200U
#define DOOM_VIDEO_WIDTH 320U
#define DOOM_FIRST_FRAME_TIMEOUT_MS 250U
#define DOOM_SUBMIT_TIMEOUT_MS 100U
#define DOOM_STATS_INTERVAL_FRAMES 150U
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_FAIL -1
#define ESP_LOGW(...) ((void)0)
#define TAG "test"
#define ESP_LOGI(tag, ...) ((void)(tag), test_log(__VA_ARGS__))
#define ESP_LOGE(tag, ...) ((void)(tag), test_log(__VA_ARGS__))
typedef int esp_err_t;
static void test_log(const char *format, ...) { (void)format; }
static const char *esp_err_to_name(esp_err_t error) { (void)error; return "test"; }
/* Timing instrumentation is orthogonal to submitted loading pixels. */
enum { DOOM_PERF_NET, DOOM_PERF_COMPOSE, DOOM_PERF_SUBMIT, DOOM_PERF_LOG };
static uint64_t doom_perf_now(void) { return 0; }
static void doom_perf_frame_begin(void) {}
static void doom_perf_dg_begin(void) {}
static void doom_perf_dg_end(void) {}
static void doom_perf_end(unsigned phase, uint64_t started)
{ (void)phase; (void)started; }
static uint32_t frame[320U * 200U];
static uint32_t captured[320U * 200U];
static uint32_t game[320U * 200U];
#if CONFIG_P4_BOARD_M5STACK_TAB5
static uint32_t *s_overlay_buffer, *DG_ScreenBuffer;
static uint64_t s_frame_acquire_us;
static bool frame_leased;
static void doom_perf_record(unsigned phase, uint64_t elapsed)
{ (void)phase; (void)elapsed; }
#else
static uint32_t *s_overlay_buffer = frame, *DG_ScreenBuffer = game;
#endif
static uint32_t s_frame_count;
static esp_err_t s_frame_error;
static bool s_video_initialized = true, s_cleanup_active;
static bool s_cleanup_complete, s_display_initialized = true, s_blob_registered = true;
static bool s_console_os_launch_active = true;
static void *s_shared_bus;
static bool s_terminal_close_engine_wads;
static void close_engine_wads(void);
static void composite_cleanup(void);
static bool audio_release_ok = true;
static unsigned audio_releases, wad_closes, open_wads;
typedef struct { bool open; } wad_file_t;
static wad_file_t wad_a, wad_b;
static unsigned numlumps = 3U;
static struct { wad_file_t *wad_file; } lumpinfo[3];
static void open_engine_wads(void)
{
    assert(open_wads == 0U);
    wad_a.open = wad_b.open = true;
    lumpinfo[0].wad_file = lumpinfo[2].wad_file = &wad_a;
    lumpinfo[1].wad_file = &wad_b;
    open_wads = 2U;
}
static void W_CloseFile(wad_file_t *file)
{
    assert(audio_release_ok && file->open && open_wads > 0U);
    for (unsigned i = 0U; i < numlumps; ++i) assert(lumpinfo[i].wad_file != file);
    file->open = false;
    --open_wads;
    ++wad_closes;
}
static bool release_audio(void) { ++audio_releases; return audio_release_ok; }
static bool release_retained_touch(void) { return true; }
static void doom_diagnostics_end(void) {}
static esp_err_t platform_i2c_shared_destroy(void **bus) { *bus = NULL; return ESP_OK; }
static esp_err_t platform_display_set_brightness(unsigned brightness)
{ assert(brightness == 0U); return ESP_OK; }
static esp_err_t doom_video_deinit(void) { return ESP_OK; }
static esp_err_t platform_display_deinit(void) { return ESP_OK; }
static esp_err_t platform_readonly_blob_unregister(void)
{ return open_wads == 0U ? ESP_OK : ESP_ERR_INVALID_STATE; }
#if !CONFIG_P4_BOARD_M5STACK_TAB5
static void test_free(void *pixels) { assert(pixels == frame); }
#define free test_free
#endif
static unsigned clock_ms, submits, polls, logs;
static unsigned delays, restarts, cleanup_retry_sleeps;
static bool recover_audio_on_retry;
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(TickType_t ticks)
{
    clock_ms += ticks;
    ++delays;
    if (ticks == 1000U) assert(recover_audio_on_retry); /* No black-screen loop after success. */
    if (recover_audio_on_retry && ticks == 1000U) {
        assert(!s_cleanup_complete && s_blob_registered && s_terminal_close_engine_wads);
        assert(open_wads == 2U && wad_closes == 4U && restarts == 2U);
        assert(clock_ms == 131000U); /* No failed-audio presentation hold. */
        ++cleanup_retry_sleeps;
        audio_release_ok = true;
        recover_audio_on_retry = false;
    }
}
#if P4_DOOM_USB_DEBUG
#define P4_BUTTON_BACK 128U
#define P4_BUTTON_B 32U
static uint32_t debug_buttons;
static unsigned debug_polls;
static void console_os_debug_poll(void) { ++debug_polls; }
static uint32_t console_os_debug_buttons(void) { return debug_buttons; }
#endif
static jmp_buf halt_env;
static void startup_failure_before_cleanup(void);
static _Noreturn void esp_restart(void)
{
    assert(audio_release_ok && open_wads == 0U && s_cleanup_complete && !s_blob_registered);
    ++restarts;
    longjmp(halt_env, 1);
}
static bool reenter, inject_poll;
static int test_display_result, s_touch_input;
static uint32_t ticks_ms(void) { return clock_ms; }
void p4_doom_startup_status(bool waiting);
static int doom_video_submit_xrgb8888(const uint32_t *pixels, unsigned stride,
                                     uint32_t timeout)
{
    assert(stride == 320U && timeout <= 250U);
    ++submits;
    memcpy(captured, pixels, sizeof(captured));
    if (reenter) p4_doom_startup_status(false);
    return test_display_result;
}
#if CONFIG_P4_BOARD_M5STACK_TAB5
static int doom_video_acquire_xrgb8888(uint32_t **pixels, uint32_t timeout)
{
    assert(!frame_leased && *pixels == NULL && timeout <= 250U);
    frame_leased = true;
    *pixels = frame;
    if (inject_poll) p4_doom_startup_status(false);
    return ESP_OK;
}
static int doom_video_publish_xrgb8888(bool initial, uint32_t timeout)
{
    (void)initial;
    assert(frame_leased);
    const int result = doom_video_submit_xrgb8888(frame,320U,timeout);
    frame_leased = false;
    return result;
}
#endif
static void P4_DoomNetPoll(void)
{
    ++polls;
    if (inject_poll) p4_doom_startup_status(true);
}
static void P4_DoomNetPollFrameTail(bool allow)
{ (void)allow; P4_DoomNetPoll(); }
static bool doom_touch_audio_compose_frame(const uint32_t *pixels, unsigned in,
    uint32_t *overlay, unsigned out, const int *input)
{
    assert(in == 320U && out == 320U && input == &s_touch_input);
    if (overlay != pixels) memcpy(overlay, pixels, sizeof(frame));
    return true;
}
static void doom_touch_input_init(int *input) { *input = 0; }
static void log_runtime_stats(void) { ++logs; }

/* PRODUCTION_STATE */
/* PRODUCTION_FUNCTIONS */

static unsigned ink(unsigned top, unsigned bottom)
{
    unsigned count = 0;
    for (unsigned y = top; y < bottom; ++y)
        for (unsigned x = 4U; x < 316U; ++x)
            if (captured[y * 320U + x] == 0x00ffffffU) ++count;
    return count;
}

static void start(unsigned time)
{
    clock_ms = time;
    s_startup_ui_active = true;
    s_startup_ui_waiting = false;
    s_startup_ui_drawing = false;
    s_startup_ui_started_ms = time;
    s_startup_ui_last_ms = time;
    s_startup_engine_phase = P4_DOOM_ENGINE_LOADING_PREPARING;
    s_startup_engine_created = false;
    s_startup_engine_completed = s_startup_engine_total = 0U;
    s_startup_net_progress = (p4_doom_loading_progress_t){0};
    network_progress = (p4_doom_loading_progress_t){0};
    s_startup_ui_last_phase = P4_DOOM_ENGINE_LOADING_PREPARING;
    s_startup_failure_visible = false;
    s_startup_failure_visible_ms = 0U;
    s_terminal_close_engine_wads = false;
    s_cleanup_complete = false;
    s_video_initialized = s_display_initialized = s_blob_registered = true;
#if !CONFIG_P4_BOARD_M5STACK_TAB5
    s_overlay_buffer = frame;
#endif
}

static void expect_text(const char *text, unsigned y, unsigned scale)
{
    /* Match actual submitted pixels against the compiled-in real glyphs. */
    uint32_t *const saved_overlay = s_overlay_buffer;
    s_overlay_buffer = frame;
    uint32_t saved[28U * 320U];
    const size_t bytes = 28U / scale * 320U * sizeof(uint32_t);
    memcpy(saved, frame + y * 320U, bytes);
    memset(frame + y * 320U, 0, bytes);
    startup_text_scaled(text, y, scale);
    for (unsigned i = 0; i < 28U / scale * 320U; ++i)
        assert((captured[y * 320U + i] == 0x00ffffffU) ==
               (frame[y * 320U + i] == 0x00ffffffU));
    memcpy(frame + y * 320U, saved, bytes);
    s_overlay_buffer = saved_overlay;
}

int main(void)
{
    /* Legacy startup retains its existing frame. */
    assert(submit_startup_frame() == ESP_OK);
    assert(captured[100U * 320U + 160U] == 0x000080ffU);
    assert(ink(44U, 72U) == 0U);

    start(1000U);
    assert(submit_startup_frame() == ESP_OK);
    expect_text("Doom Arena by Game Changers", 14U, 2U);
    expect_text("2/3 Prepare engine", 44U, 1U);
    expect_text("Reading game data", 78U, 2U);
    expect_text("Working - please wait", 130U, 2U);
    expect_text("Elapsed 0 s", 164U, 2U);
    unsigned before = submits;
    clock_ms = 1499U;
    p4_doom_startup_status(false);
    assert(submits == before);
    clock_ms = 1500U;
    p4_doom_startup_status(false);
    assert(submits == ++before);
    assert(captured[110U * 320U + 64U] == 0x00001838U);
    assert(captured[110U * 320U + 112U] == 0x000080ffU);

    /* Engine loops only report work; the service paints a phase immediately,
       then rate-limits actual completed-item changes to two frames/second. */
    P4_DoomLoadingProgress(P4_DOOM_ENGINE_LOADING_SPRITES, 675U, 1350U);
    assert(submits == before);
    p4_doom_startup_status(false);
    assert(submits == ++before);
    expect_text("2/3 Load sprites", 44U, 1U);
    expect_text("Sprite headers loaded", 78U, 2U);
    expect_text("50%  675 / 1350 items", 130U, 2U);
    assert(s_startup_ui_started_ms == 1000U);
    for (unsigned x = 48U; x < 272U; ++x)
        assert(captured[110U * 320U + x] ==
               (x < 160U ? 0x000080ffU : 0x00001838U));
    P4_DoomLoadingProgress(P4_DOOM_ENGINE_LOADING_SPRITES, 1000U, 1350U);
    p4_doom_startup_status(false);
    assert(submits == before);
    clock_ms = 2000U;
    p4_doom_startup_status(false);
    assert(submits == ++before);
    expect_text("74%  1000 / 1350 items", 130U, 2U);

    /* Checked bytes and actual replay tics use distinct labels. Pending
       transport state cannot hide the engine's real sprite progress. */
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_CHECKPOINT,512U,1024U};
    p4_doom_startup_status(true);
    assert(submits == ++before);
    expect_text("3/3 Sync match", 44U, 1U);
    expect_text("Receiving match state", 78U, 2U);
    expect_text("50%  512 / 1024 bytes", 130U, 2U);
    p4_doom_startup_status(false);
    assert(submits == ++before);
    expect_text("2/3 Load sprites", 44U, 1U);
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_CATCHUP,30U,120U};
    P4_DoomLoadingProgress(P4_DOOM_ENGINE_LOADING_READY,0U,0U);
    /* Restoring a checkpoint calls G_InitNew after Create. This new MAP
       phase cannot replace actual canonical catch-up with a stuck 100%. */
    P4_DoomLoadingProgress(P4_DOOM_ENGINE_LOADING_MAP,13U,13U);
    assert(s_startup_engine_created);
    p4_doom_startup_status(false);
    assert(submits == ++before);
    expect_text("Catching up to host", 78U, 2U);
    expect_text("25%  30 / 120 tics", 130U, 2U);
    assert(captured[110U * 320U + 103U] == 0x000080ffU);
    assert(captured[110U * 320U + 104U] == 0x00001838U);

    /* Unknown totals remain explicit waiting; no invented percentage. */
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_WAITING_HOST,0U,0U};
    p4_doom_startup_status(false);
    assert(submits == ++before);
    expect_text("Waiting for host", 78U, 2U);
    expect_text("Working - please wait", 130U, 2U);
    clock_ms = 62000U;
    reenter = true;
    p4_doom_startup_status(false);
    reenter = false;
    assert(submits == ++before); /* Display callbacks cannot recurse. */
    expect_text("Elapsed 61 s", 164U, 2U);

    /* Invalid/lifecycle states never touch the display. */
    clock_ms += 500U;
    s_video_initialized = false;
    p4_doom_startup_status(false);
    s_video_initialized = true;
#if !CONFIG_P4_BOARD_M5STACK_TAB5
    s_overlay_buffer = NULL;
    p4_doom_startup_status(false);
    s_overlay_buffer = frame;
#endif
    s_cleanup_active = true;
    p4_doom_startup_status(false);
    s_cleanup_active = false;
    assert(submits == before);

    /* A failure is visible immediately, once, even inside the refresh window. */
    clock_ms = 62001U;
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_FAILED,0U,0U};
    p4_doom_startup_status(false);
    assert(submits == ++before);
    expect_text("Join failed", 44U, 1U);
    expect_text("Returning Home...", 78U, 2U);
    expect_text("Connection did not complete", 130U, 2U);
    assert(captured[110U * 320U + 160U] == 0x00802020U);
    clock_ms += 1000U;
    p4_doom_startup_status(false);
    assert(submits == before);
    assert(s_startup_failure_visible);
    const unsigned polls_before_hold = polls;
    startup_failure_before_cleanup();
    assert(clock_ms == 64001U && delays == 100U);
    assert(!s_startup_failure_visible && submits == before && polls == polls_before_hold);
    startup_failure_before_cleanup();
    assert(clock_ms == 64001U && delays == 100U); /* No repeated cleanup delay. */

    /* First engine frame retires loading before either network poll. */
    for (unsigned i = 0; i < 320U * 200U; ++i) game[i] = 0x00123456U;
    inject_poll = true;
#if CONFIG_P4_BOARD_M5STACK_TAB5
    assert(DG_PrepareFrame());
    assert(!s_startup_ui_active && frame_leased);
    memcpy(DG_ScreenBuffer,game,sizeof(game));
#endif
    DG_DrawFrame();
#if CONFIG_P4_BOARD_M5STACK_TAB5
    assert(!frame_leased && s_overlay_buffer == NULL && DG_ScreenBuffer == NULL);
#endif
    assert(submits == ++before && polls == 2U && s_frame_count == 1U);
    assert(!s_startup_ui_active && captured[110U * 320U + 160U] == 0x00123456U);
    clock_ms += 1000U;
    p4_doom_startup_status(true);
    P4_DoomLoadingProgress(P4_DOOM_ENGINE_LOADING_SPRITES, 0U, 2U);
    assert(submits == before);
    inject_poll = false;

    /* Tick wrap keeps the throttle bounded. */
    start(UINT32_MAX - 100U);
    clock_ms = 399U;
    p4_doom_startup_status(false);
    assert(submits == ++before);

    /* Percent and bar arithmetic stay bounded for maximal counters and
       malformed completed values; the actual source cannot exceed total. */
    P4_DoomLoadingProgress(P4_DOOM_ENGINE_LOADING_TEXTURES,UINT32_MAX,UINT32_MAX);
    p4_doom_startup_status(false);
    assert(submits == ++before);
    expect_text("100%  4294967295 / 4294967295 items", 130U, 2U);
    assert(captured[110U * 320U + 271U] == 0x000080ffU);
    P4_DoomLoadingProgress(P4_DOOM_ENGINE_LOADING_MAP,UINT32_MAX,13U);
    p4_doom_startup_status(false);
    assert(submits == ++before && s_startup_engine_completed == 13U);
    expect_text("3/3 Load match", 44U, 1U);
    expect_text("100%  13 / 13 items", 130U, 2U);

    /* A failed optional refresh disables further paint attempts. */
    test_display_result = 1;
    clock_ms += 500U;
    p4_doom_startup_status(false);
    assert(submits == ++before && !s_startup_ui_active);
    clock_ms += 1000U;
    p4_doom_startup_status(true);
    assert(submits == before && s_frame_error == ESP_OK);

    /* A later launch clears prior failure/progress and has no false rejoin label. */
    test_display_result = 0;
    start(100000U);
    assert(submit_startup_frame() == ESP_OK);
    expect_text("2/3 Prepare engine", 44U, 1U);
    expect_text("Doom Arena by Game Changers", 14U, 2U);

    /* Normal exits do not wait; a configure I_Error callback takes the
       non-returning owner cleanup path with a readable failure frame. */
    startup_failure_before_cleanup();
    engine_exit_composite();
    assert(clock_ms == 100000U && restarts == 0U);
    open_engine_wads();
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_FAILED,0U,0U};
    if (setjmp(halt_env) == 0) {
        engine_exit_composite();
        assert(false);
    }
    assert(restarts == 1U && clock_ms == 102500U && !s_startup_failure_visible);
    assert(wad_closes == 2U && open_wads == 0U && !s_terminal_close_engine_wads);
    expect_text("Join failed", 44U, 1U);
    expect_text("Returning Home...", 78U, 2U);

#if P4_DOOM_USB_DEBUG
    /* Raw debug Back/B can dismiss, without engine input or network polling. */
    start(110000U);
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_FAILED,0U,0U};
    p4_doom_startup_status(false);
    debug_buttons = P4_BUTTON_BACK;
    startup_failure_before_cleanup();
    assert(clock_ms == 110000U && debug_polls > 0U && !s_startup_failure_visible);
    debug_buttons = 0U;
#endif

    /* The bounded terminal delay remains correct across clock wrap. */
    start(UINT32_MAX - 100U);
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_FAILED,0U,0U};
    p4_doom_startup_status(false);
    startup_failure_before_cleanup();
    assert(clock_ms == 1899U);

    /* A degraded optional display cannot bypass terminal owner cleanup.
       Nothing was published, so the failure adds no retention delay. */
    start(120000U);
    open_engine_wads();
    test_display_result = 1;
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_FAILED,0U,0U};
    p4_doom_startup_status(false);
    assert(!s_startup_ui_active && !s_startup_failure_visible);
    if (setjmp(halt_env) == 0) {
        engine_exit_composite();
        assert(false);
    }
    assert(restarts == 2U && clock_ms == 120500U);
    assert(wad_closes == 4U && open_wads == 0U && s_cleanup_complete);
    test_display_result = 0;

    /* Audio release failure retains every descriptor. A later successful
       owner cleanup retry closes each WAD once and allows blob unregister. */
    start(130000U);
    open_engine_wads();
    audio_release_ok = false;
    recover_audio_on_retry = true;
    network_progress = (p4_doom_loading_progress_t){P4_DOOM_LOADING_FAILED,0U,0U};
    p4_doom_startup_status(false);
    if (setjmp(halt_env) == 0) {
        engine_exit_composite();
        assert(false);
    }
    assert(restarts == 3U && cleanup_retry_sleeps == 1U && !recover_audio_on_retry);
    assert(open_wads == 0U && wad_closes == 6U && !s_terminal_close_engine_wads);
    assert(s_cleanup_complete && !s_blob_registered && clock_ms == 132500U);
    composite_cleanup();
    assert(wad_closes == 6U && audio_releases > 0U);

    /* Text clipping remains safe for an oversized label or row. */
    s_overlay_buffer = frame;
    startup_text_scaled("A long label that exceeds the screen width considerably", 195U, 1U);
    startup_text_scaled("Outside", UINT32_MAX, 2U);
    assert(logs == 0U);
    puts("Doom startup presentation: measured engine/transfer/replay progress, failure, throttle, lifecycle and bounds passed");
    return 0;
}
'''


def main() -> None:
    source = SOURCE.read_text()
    production = "\n\n".join(function(source, name) for name in (
        "startup_text_scaled", "P4_DoomLoadingProgress", "startup_progress_info",
        "submit_startup_frame", "p4_doom_startup_status", "startup_failure_before_cleanup",
        "close_engine_wads", "composite_cleanup", "halt_dark", "engine_exit_composite", "DG_DrawFrame"))
    tab5_prepare = function(source, "DG_PrepareFrame")
    state = re.search(r"/\* Arena startup presentation state\. \*/(.*?)#endif",
                      source, re.S)
    assert state is not None, "Missing production startup presentation state"
    header = (ROOT / "apps/doom_audio_probe/components/doom_engine_audio/p4_doom_net.h").read_text()
    api = header[header.index("/* Startup-only owner-task snapshots."):header.index("boolean P4_DoomNetFailed(void);")]
    with tempfile.TemporaryDirectory(prefix="p4-doom-startup-") as directory:
        folder = Path(directory)
        test = folder / "test.c"
        test.write_text(HARNESS.replace("/* LOADING_API */", api)
                        .replace("/* PRODUCTION_STATE */", state.group(1))
                        .replace("/* PRODUCTION_FUNCTIONS */",
                                 "#if CONFIG_P4_BOARD_M5STACK_TAB5\n" + tab5_prepare + "\n#endif\n" + production))
        binary = folder / "test"
        for tab5, debug in ((0,0),(1,0),(1,1)):
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                "-Wconversion", "-Wshadow", "-Werror", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer", "-DCONFIG_P4_BOARD_M5STACK_TAB5="+str(tab5), str(test),
                "-DP4_DOOM_USB_DEBUG="+str(debug),
                str(ROOT / "components/p4_game_api/src/presentation_font.c"),
                "-I", str(ROOT / "components/p4_game_api/include"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)



if __name__ == "__main__":
    main()
