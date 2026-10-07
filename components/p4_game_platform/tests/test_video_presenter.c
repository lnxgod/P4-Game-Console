/* SPDX-License-Identifier: MIT */
/* Test the actual presenter and worker. Substitute only task/allocation and
 * synchronous display callbacks; these tests do not measure device cadence. */
#define _POSIX_C_SOURCE 200809L
#include "p4/video_presenter.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REQUIRE(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); abort(); \
} } while (0)

enum { WIDTH = 768, HEIGHT = 480, FRAME_LIMIT = 64,
       BACKEND_TIMEOUT = 0x107, BACKEND_ERROR = -12345 };
typedef struct {
    pthread_t thread;
    void (*entry)(void *);
    void *argument;
    atomic_bool done;
} test_task_t;
typedef struct {
    pthread_t foreground;
    atomic_bool block;
    atomic_uint started, calls;
    atomic_int active;
    const uint16_t *expected[FRAME_LIMIT], *observed[FRAME_LIMIT];
    unsigned order[FRAME_LIMIT];
    uint32_t timeouts[FRAME_LIMIT];
    int results[FRAME_LIMIT];
} panel_t;

static unsigned alloc_calls, free_calls, fail_alloc;
static bool fail_start;
static atomic_uint live_tasks;

static uint64_t now_ms(void)
{
    struct timespec time;
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (uint64_t)time.tv_sec * 1000U + (uint64_t)time.tv_nsec / 1000000U;
}
static uint64_t now_us(void) { return now_ms() * 1000U; }
static void pause_tick(void)
{
    const struct timespec duration = {.tv_sec = 0, .tv_nsec = 1000000};
    (void)nanosleep(&duration, NULL);
}
static void *pixels_alloc(size_t bytes)
{
    REQUIRE(bytes == (size_t)WIDTH * HEIGHT * sizeof(uint16_t));
    if (++alloc_calls == fail_alloc) return NULL;
    return malloc(bytes);
}
static void pixels_free(void *pixels)
{
    /* Source buffers must never be released before a successful join. */
    REQUIRE(pixels != NULL && atomic_load(&live_tasks) == 0U);
    ++free_calls;
    free(pixels);
}
static void *task_entry(void *argument)
{
    test_task_t *task = argument;
    task->entry(task->argument);
    atomic_store_explicit(&task->done, true, memory_order_release);
    return NULL;
}
static int task_start(void (*entry)(void *), void *argument, void **out)
{
    if (fail_start) return DW_NO_MEMORY;
    test_task_t *task = calloc(1U, sizeof(*task));
    REQUIRE(task != NULL);
    task->entry = entry;
    task->argument = argument;
    atomic_init(&task->done, false);
    REQUIRE(pthread_create(&task->thread, NULL, task_entry, task) == 0);
    atomic_fetch_add(&live_tasks, 1U);
    *out = task;
    return DW_OK;
}
static int task_join(void *opaque, uint32_t timeout)
{
    test_task_t *task = opaque;
    const uint64_t began = now_ms();
    while (!atomic_load_explicit(&task->done, memory_order_acquire)) {
        if (now_ms() - began >= timeout) return DW_TIMEOUT;
        pause_tick();
    }
    REQUIRE(pthread_join(task->thread, NULL) == 0);
    free(task);
    REQUIRE(atomic_fetch_sub(&live_tasks, 1U) == 1U);
    return DW_OK;
}
static uint16_t frame_pixel(unsigned frame, size_t index)
{
    return (uint16_t)((frame * 1009U +
        (uint32_t)(index % 65521U) * 37U) & 0xffffU);
}
static void frame_fill(uint16_t *pixels, size_t stride, unsigned frame)
{
    for (size_t y = 0U; y < HEIGHT; ++y)
        for (size_t x = 0U; x < stride; ++x)
            pixels[y * stride + x] = x < WIDTH
                ? frame_pixel(frame, y * WIDTH + x) : UINT16_C(0xbabe);
}
static void frame_verify(const uint16_t *pixels, unsigned frame)
{
    for (size_t i = 0U; i < (size_t)WIDTH * HEIGHT; ++i)
        REQUIRE(pixels[i] == frame_pixel(frame, i));
}
static int panel_submit(void *context, const uint16_t *pixels, size_t stride,
    uint32_t timeout, p4_game_video_backend_metrics_t *metrics)
{
    panel_t *panel = context;
    REQUIRE(stride == WIDTH && !pthread_equal(pthread_self(), panel->foreground));
    REQUIRE(atomic_fetch_add(&panel->active, 1) == 0);
    unsigned frame = 0U;
    while (frame < FRAME_LIMIT && pixels[0] != frame_pixel(frame, 0U)) ++frame;
    REQUIRE(frame < FRAME_LIMIT);
    if (panel->expected[frame] != NULL) REQUIRE(pixels == panel->expected[frame]);
    frame_verify(pixels, frame);
    atomic_fetch_add_explicit(&panel->started, 1U, memory_order_release);
    while (atomic_load_explicit(&panel->block, memory_order_acquire)) pause_tick();
    /* Alternate foreground rendering must not alter any in-flight byte. */
    frame_verify(pixels, frame);
    const unsigned call = atomic_load_explicit(&panel->calls, memory_order_relaxed);
    REQUIRE(call < FRAME_LIMIT);
    panel->observed[call] = pixels;
    panel->order[call] = frame;
    panel->timeouts[call] = timeout;
    const int result = panel->results[frame];
    *metrics = (p4_game_video_backend_metrics_t){
        .valid = result == DW_OK, .reuse_us = 11U, .transform_us = 22U,
        .handoff_us = 33U, .ppa_us = 17U, .core = 1,
        .stack_free_bytes = result == BACKEND_ERROR ? 0U : 4096U};
    REQUIRE(atomic_fetch_sub(&panel->active, 1) == 1);
    atomic_store_explicit(&panel->calls, call + 1U, memory_order_release);
    return result;
}
static int worker_create(int (*submit)(void *, const void *, size_t, uint32_t),
    void *context, size_t width, size_t height, size_t pixel_bytes,
    display_worker_t **out)
{
    REQUIRE(width == WIDTH && height == HEIGHT && pixel_bytes == sizeof(uint16_t));
    const display_worker_ops_t ops = {
        pixels_alloc, pixels_free, now_ms, pause_tick, task_start, task_join, submit};
    return display_worker_create(&ops, context, width, height, pixel_bytes, out);
}
static void panel_init(panel_t *panel)
{
    REQUIRE(atomic_load(&live_tasks) == 0U);
    memset(panel, 0, sizeof(*panel));
    panel->foreground = pthread_self();
    atomic_init(&panel->block, false);
    atomic_init(&panel->started, 0U);
    atomic_init(&panel->calls, 0U);
    atomic_init(&panel->active, 0);
    alloc_calls = 0U;
    free_calls = 0U;
    fail_alloc = 0U;
    fail_start = false;
}
static p4_game_video_config_t config_for(panel_t *panel)
{
    return (p4_game_video_config_t){
        WIDTH, HEIGHT, panel, panel_submit, BACKEND_TIMEOUT, now_us, worker_create};
}
static p4_game_video_presenter_t *presenter_create(panel_t *panel)
{
    p4_game_video_presenter_t *presenter = NULL;
    const p4_game_video_config_t config = config_for(panel);
    REQUIRE(p4_game_video_presenter_create(&config, &presenter) == DW_OK);
    REQUIRE(presenter != NULL && alloc_calls == 2U);
    return presenter;
}
static uint16_t *frame_acquire(p4_game_video_presenter_t *presenter,
                             panel_t *panel, unsigned frame)
{
    uint16_t *pixels = NULL;
    size_t stride = 0U;
    REQUIRE(frame < FRAME_LIMIT);
    REQUIRE(p4_game_video_presenter_acquire(presenter, &pixels, &stride, 1000U) == DW_OK);
    REQUIRE(pixels != NULL && stride == WIDTH);
    panel->expected[frame] = pixels;
    frame_fill(pixels, stride, frame);
    return pixels;
}
static void wait_started(panel_t *panel, unsigned count)
{
    const uint64_t began = now_ms();
    while (atomic_load_explicit(&panel->started, memory_order_acquire) < count) {
        REQUIRE(now_ms() - began < 3000U);
        pause_tick();
    }
}
static void failed_acquire(p4_game_video_presenter_t *presenter, int error)
{
    uint16_t sentinel;
    uint16_t *pixels = &sentinel;
    size_t stride = 999U;
    REQUIRE(p4_game_video_presenter_acquire(presenter, &pixels, &stride, 5U) == error);
    REQUIRE(pixels == NULL && stride == 0U);
}
static p4_game_video_stats_t stop(p4_game_video_presenter_t **presenter,
                                 panel_t *panel, unsigned calls)
{
    p4_game_video_stats_t stats = {0};
    REQUIRE(p4_game_video_presenter_stop(presenter, 1000U, &stats) == DW_OK);
    REQUIRE(*presenter == NULL && free_calls == 2U && atomic_load(&live_tasks) == 0U);
    REQUIRE(stats.accepted == calls && atomic_load(&panel->calls) == calls);
    REQUIRE(stats.closing);
    return stats;
}

static void test_native_zero_copy_parity(void)
{
    panel_t panel;
    panel_init(&panel);
    p4_game_video_presenter_t *presenter = presenter_create(&panel);
    REQUIRE(p4_game_video_presenter_commit(presenter, 0U, 100U) == DW_INVALID);
    for (unsigned frame = 0U; frame < 16U; ++frame) {
        (void)frame_acquire(presenter, &panel, frame);
        REQUIRE(p4_game_video_presenter_commit(presenter, 1000U,
            frame == 0U ? 250U : 100U) == DW_OK);
        if (frame == 0U) REQUIRE(atomic_load(&panel.calls) == 1U);
    }
    REQUIRE(p4_game_video_presenter_flush(presenter, 1000U) == DW_OK);
    /* Compare source identities while those allocations are still alive. */
    for (unsigned frame = 0U; frame < 16U; ++frame) {
        REQUIRE(panel.order[frame] == frame);
        REQUIRE(panel.observed[frame] == panel.expected[frame]);
        REQUIRE(panel.timeouts[frame] == (frame == 0U ? 250U : 100U));
    }
    p4_game_video_stats_t stats = stop(&presenter, &panel, 16U);
    REQUIRE(stats.completed == 16U && stats.backend.samples == 16U);
    REQUIRE(stats.queue.samples == 16U); /* Acquire + commit is one attempted frame. */
    REQUIRE(stats.copy.samples == 0U && stats.copy.total_us == 0U);
    REQUIRE(stats.copy.maximum_us == 0U && stats.backend_timeouts == 0U);
    REQUIRE(stats.wait_timeouts == 0U && stats.hard_error == DW_OK);
    REQUIRE(stats.backend_core == 1 && stats.stack_free_bytes == 4096U);
    REQUIRE(stats.reuse.samples == 16U && stats.reuse.total_us == 176U);
    REQUIRE(stats.transform.total_us == 352U && stats.handoff.total_us == 528U);
    REQUIRE(stats.ppa.total_us == 272U);
    puts("PASS direct native RGB565 parity, lease pointer identity and zero copy accounting");
}

static void test_overlap_and_unpublished_cancel(void)
{
    panel_t panel;
    panel_init(&panel);
    p4_game_video_presenter_t *presenter = presenter_create(&panel);
    (void)frame_acquire(presenter, &panel, 0U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 250U) == DW_OK);
    atomic_store(&panel.block, true);
    uint16_t *in_flight = frame_acquire(presenter, &panel, 1U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 5U, 100U) == DW_OK);
    wait_started(&panel, 2U);
    uint16_t *writable = frame_acquire(presenter, &panel, 2U);
    REQUIRE(writable != in_flight);
    failed_acquire(presenter, DW_INVALID);
    REQUIRE(p4_game_video_presenter_present(presenter, writable, WIDTH, 0U, 100U) == DW_INVALID);
    REQUIRE(p4_game_video_presenter_commit(presenter, 5U, 100U) == DW_TIMEOUT);
    frame_verify(writable, 2U); /* Timed-out commit retains foreground ownership. */
    REQUIRE(p4_game_video_presenter_cancel(presenter) == DW_OK);
    REQUIRE(p4_game_video_presenter_cancel(presenter) == DW_OK);
    REQUIRE(frame_acquire(presenter, &panel, 3U) == writable);
    atomic_store(&panel.block, false);
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 100U) == DW_OK);
    p4_game_video_stats_t stats = stop(&presenter, &panel, 3U);
    REQUIRE(panel.order[0] == 0U && panel.order[1] == 1U && panel.order[2] == 3U);
    REQUIRE(stats.completed == 3U && stats.wait_timeouts >= 1U && stats.copy.samples == 0U);
    puts("PASS alternate render overlaps immutable backend source; timed-out unpublished frame cancels");
}

static void test_first_barrier_timeout_recovery(void)
{
    panel_t panel;
    panel_init(&panel);
    p4_game_video_presenter_t *presenter = presenter_create(&panel);
    atomic_store(&panel.block, true);
    (void)frame_acquire(presenter, &panel, 0U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 5U, 250U) == DW_TIMEOUT);
    wait_started(&panel, 1U);
    REQUIRE(p4_game_video_presenter_cancel(presenter) == DW_OK);
    failed_acquire(presenter, DW_TIMEOUT);
    p4_game_video_stats_t stats;
    REQUIRE(p4_game_video_presenter_stats(presenter, &stats) == DW_OK);
    REQUIRE(stats.accepted == 1U && stats.completed == 0U && stats.wait_timeouts >= 2U);
    atomic_store(&panel.block, false);
    (void)frame_acquire(presenter, &panel, 1U); /* Finishes the pending barrier first. */
    REQUIRE(atomic_load(&panel.calls) == 1U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 100U) == DW_OK);
    stats = stop(&presenter, &panel, 2U);
    REQUIRE(stats.completed == 2U && stats.hard_error == DW_OK);
    puts("PASS first completion timeout exposes no writable surface until the barrier is consumed");
}

static void test_backend_timeout_is_not_first_success(void)
{
    panel_t panel;
    panel_init(&panel);
    panel.results[0] = BACKEND_TIMEOUT;
    p4_game_video_presenter_t *presenter = presenter_create(&panel);
    (void)frame_acquire(presenter, &panel, 0U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 250U) == DW_TIMEOUT);
    p4_game_video_stats_t stats;
    REQUIRE(p4_game_video_presenter_stats(presenter, &stats) == DW_OK);
    REQUIRE(stats.accepted == 1U && stats.completed == 0U);
    REQUIRE(stats.backend_timeouts == 1U && stats.hard_error == DW_OK);
    atomic_store(&panel.block, true);
    (void)frame_acquire(presenter, &panel, 1U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 5U, 250U) == DW_TIMEOUT);
    wait_started(&panel, 2U);
    failed_acquire(presenter, DW_TIMEOUT);
    atomic_store(&panel.block, false);
    (void)frame_acquire(presenter, &panel, 2U);
    REQUIRE(atomic_load(&panel.calls) == 2U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 100U) == DW_OK);
    stats = stop(&presenter, &panel, 3U);
    REQUIRE(stats.completed == 2U && stats.backend_timeouts == 1U && stats.hard_error == DW_OK);
    puts("PASS consumed backend timeout remains nonfatal while the first-success barrier stays active");
}

static void test_hard_error_with_prepared_frame(void)
{
    panel_t panel;
    panel_init(&panel);
    p4_game_video_presenter_t *presenter = presenter_create(&panel);
    (void)frame_acquire(presenter, &panel, 0U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 250U) == DW_OK);
    panel.results[1] = BACKEND_ERROR;
    atomic_store(&panel.block, true);
    (void)frame_acquire(presenter, &panel, 1U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 5U, 100U) == DW_OK);
    wait_started(&panel, 2U);
    uint16_t *writable = frame_acquire(presenter, &panel, 2U);
    atomic_store(&panel.block, false);
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 100U) == BACKEND_ERROR);
    frame_verify(writable, 2U);
    REQUIRE(p4_game_video_presenter_cancel(presenter) == DW_OK);
    failed_acquire(presenter, BACKEND_ERROR);
    p4_game_video_stats_t stats = stop(&presenter, &panel, 2U);
    REQUIRE(stats.completed == 1U && stats.hard_error == BACKEND_ERROR);
    REQUIRE(stats.stack_free_bytes == 0U && panel.order[1] == 1U);
    puts("PASS backend hard error rejects prepared commit, retains cancellation ownership and joins safely");
}

static void test_held_exit_and_join_retention(void)
{
    panel_t panel;
    panel_init(&panel);
    p4_game_video_presenter_t *presenter = presenter_create(&panel);
    (void)frame_acquire(presenter, &panel, 0U);
    p4_game_video_stats_t stats = stop(&presenter, &panel, 0U);
    REQUIRE(stats.completed == 0U); /* Exit before the opening frame is published. */
    panel_init(&panel);
    presenter = presenter_create(&panel);
    (void)frame_acquire(presenter, &panel, 0U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 250U) == DW_OK);
    atomic_store(&panel.block, true);
    (void)frame_acquire(presenter, &panel, 1U);
    REQUIRE(p4_game_video_presenter_commit(presenter, 5U, 100U) == DW_OK);
    wait_started(&panel, 2U);
    (void)frame_acquire(presenter, &panel, 2U); /* Exit owns the next writable surface. */
    p4_game_video_presenter_t *retained = presenter;
    stats = (p4_game_video_stats_t){.accepted = 99U};
    REQUIRE(p4_game_video_presenter_stop(&presenter, 5U, &stats) == DW_TIMEOUT);
    REQUIRE(presenter == retained && free_calls == 0U && atomic_load(&live_tasks) == 1U);
    REQUIRE(stats.accepted == 99U); /* A failed join must not issue final evidence. */
    failed_acquire(presenter, DW_INVALID);
    REQUIRE(p4_game_video_presenter_cancel(presenter) == DW_OK);
    REQUIRE(p4_game_video_presenter_stats(presenter, &stats) == DW_OK && stats.closing);
    atomic_store(&panel.block, false);
    stats = stop(&presenter, &panel, 2U);
    REQUIRE(stats.completed == 2U && panel.order[1] == 1U);
    puts("PASS initial/next held leases cancel on exit; failed join retains source, presenter and task until retry");
}

static void test_create_validation_and_rollback(void)
{
    panel_t panel;
    panel_init(&panel);
    const p4_game_video_config_t config = config_for(&panel);
    p4_game_video_presenter_t *presenter = NULL;
    REQUIRE(p4_game_video_presenter_create(NULL, &presenter) == DW_INVALID);
    REQUIRE(p4_game_video_presenter_create(&config, NULL) == DW_INVALID);
    p4_game_video_config_t bad = config;
    bad.width = SIZE_MAX;
    REQUIRE(p4_game_video_presenter_create(&bad, &presenter) == DW_INVALID && !presenter);
    bad = config;
    bad.height = 0U;
    REQUIRE(p4_game_video_presenter_create(&bad, &presenter) == DW_INVALID && !presenter);
    bad = config;
    bad.backend_submit = NULL;
    REQUIRE(p4_game_video_presenter_create(&bad, &presenter) == DW_INVALID && !presenter);
    bad = config;
    bad.worker_create = NULL;
    REQUIRE(p4_game_video_presenter_create(&bad, &presenter) == DW_INVALID && !presenter);
    bad = config;
    bad.now_us = NULL;
    REQUIRE(p4_game_video_presenter_create(&bad, &presenter) == DW_INVALID && !presenter);
    bad = config;
    bad.backend_timeout_error = DW_OK;
    REQUIRE(p4_game_video_presenter_create(&bad, &presenter) == DW_INVALID && !presenter);
    REQUIRE(alloc_calls == 0U);
    for (unsigned stage = 1U; stage <= 3U; ++stage) {
        panel_init(&panel);
        fail_alloc = stage;
        fail_start = stage == 3U;
        REQUIRE(p4_game_video_presenter_create(&config, &presenter) == DW_NO_MEMORY && !presenter);
        REQUIRE(free_calls == stage - 1U && atomic_load(&live_tasks) == 0U);
    }
    panel_init(&panel);
    presenter = presenter_create(&panel);
    failed_acquire(NULL, DW_INVALID);
    REQUIRE(p4_game_video_presenter_acquire(presenter, NULL, &(size_t){0}, 0U) == DW_INVALID);
    REQUIRE(p4_game_video_presenter_acquire(presenter, &(uint16_t *){NULL}, NULL, 0U) == DW_INVALID);
    REQUIRE(p4_game_video_presenter_cancel(NULL) == DW_INVALID);
    (void)stop(&presenter, &panel, 0U);
    puts("PASS malformed creation and lease outputs reject safely; both buffer and task allocation roll back");
}

static void test_copy_route_remains_owned(void)
{
    panel_t panel;
    panel_init(&panel);
    p4_game_video_presenter_t *presenter = presenter_create(&panel);
    const size_t stride = WIDTH + 7U;
    uint16_t *source = malloc(stride * HEIGHT * sizeof(*source));
    REQUIRE(source != NULL);
    frame_fill(source, stride, 0U);
    REQUIRE(p4_game_video_presenter_present(presenter, source, stride, 1000U, 250U) == DW_OK);
    atomic_store(&panel.block, true);
    frame_fill(source, stride, 1U);
    REQUIRE(p4_game_video_presenter_present(presenter, source, stride, 5U, 100U) == DW_OK);
    wait_started(&panel, 2U);
    memset(source, 0xa5, stride * HEIGHT * sizeof(*source));
    free(source);
    atomic_store(&panel.block, false);
    REQUIRE(p4_game_video_presenter_flush(presenter, 1000U) == DW_OK);
    (void)frame_acquire(presenter, &panel, 2U); /* Copy and lease calls can alternate after ownership ends. */
    REQUIRE(p4_game_video_presenter_commit(presenter, 1000U, 100U) == DW_OK);
    p4_game_video_stats_t stats = stop(&presenter, &panel, 3U);
    REQUIRE(stats.completed == 3U && stats.copy.samples == 2U);
    REQUIRE(panel.order[0] == 0U && panel.order[1] == 1U && panel.order[2] == 2U);
    puts("PASS padded copy route preserves bytes after caller mutation/free and safely alternates with direct leases");
}

int main(void)
{
    test_native_zero_copy_parity();
    test_overlap_and_unpublished_cancel();
    test_first_barrier_timeout_recovery();
    test_backend_timeout_is_not_first_success();
    test_hard_error_with_prepared_frame();
    test_held_exit_and_join_retention();
    test_create_validation_and_rollback();
    test_copy_route_remains_owned();
    return 0;
}
