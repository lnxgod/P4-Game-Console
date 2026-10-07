#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/doom_video_tab5_worker.c"

static _Thread_local bool worker_thread;
static atomic_bool callback_block, callback_entered;
static atomic_int panel_error;
static atomic_uint panel_calls;
static atomic_uint_fast64_t ticks;
static unsigned heap_calls, heap_fail_at, heap_live;
static unsigned slot_calls, slot_fail_at, slots_live;
static bool fail_start, fail_join;
static uint16_t expected[768U * 480U];
static bool verify_pixels;
static int (*real_callback)(void *, const void *, size_t, uint32_t);
static uint64_t now_ms(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000U + (uint64_t)t.tv_nsec / 1000000U;
}
static void pause_thread(void)
{
    const struct timespec t = {0, 1000000};
    assert(nanosleep(&t, NULL) == 0);
}
void test_video_log(const char *tag, const char *format, ...)
{ (void)tag; (void)format; assert(!worker_thread); }
int64_t esp_timer_get_time(void)
{ return (int64_t)atomic_fetch_add(&ticks, 10U); }
UBaseType_t uxTaskGetStackHighWaterMark(void *task)
{ assert(task == NULL && worker_thread); return 2048U; }
uint32_t doom_video_metrics_begin(void) { assert(!worker_thread); return 1U; }
void doom_video_metrics_publish(const doom_video_metrics_t *snapshot)
{ assert(snapshot && (worker_thread || snapshot->closed)); }
void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps)
{
    assert(!worker_thread && alignment == 64U);
    assert((caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) &&
            (bytes == 768U*480U*2U || bytes == 768U*480U*4U)) ||
           (caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) && bytes == 768U*4U));
    if (++heap_calls == heap_fail_at) return NULL;
    void *p = NULL;
    assert(posix_memalign(&p, alignment, bytes) == 0);
    memset(p, 0xa5, bytes);
    ++heap_live;
    return p;
}
void heap_caps_free(void *pointer)
{
    assert(!worker_thread);
    if (pointer) { assert(heap_live > 0U); --heap_live; free(pointer); }
}
static void *slot_alloc(size_t bytes)
{
    assert(bytes == sizeof(doom_frame_packet_t) && bytes < 370000U);
    if (++slot_calls == slot_fail_at) return NULL;
    void *p = malloc(bytes); assert(p); ++slots_live; return p;
}
static void slot_free(void *pointer)
{ assert(slots_live > 0U); --slots_live; free(pointer); }
typedef struct { pthread_t thread; void (*entry)(void *); void *argument; } task_t;
static void *thread_entry(void *arg)
{ task_t *task = arg; worker_thread = true; task->entry(task->argument); return NULL; }
static int task_start(void (*fn)(void *), void *arg, void **out)
{
    if (fail_start) return DW_NO_MEMORY;
    task_t *task = calloc(1U, sizeof(*task)); assert(task);
    task->entry = fn; task->argument = arg;
    assert(pthread_create(&task->thread, NULL, thread_entry, task) == 0);
    *out = task; return DW_OK;
}
static int task_join(void *arg, uint32_t timeout)
{
    assert(timeout > 0U);
    if (fail_join) return DW_TIMEOUT;
    task_t *task = arg;
    assert(pthread_join(task->thread, NULL) == 0);
    free(task); return DW_OK;
}
static int callback(void *context, const void *pixels, size_t stride, uint32_t timeout)
{
    atomic_store(&callback_entered, true);
    while (atomic_load(&callback_block)) pause_thread();
    return real_callback(context, pixels, stride, timeout);
}
int display_worker_esp_create(int (*submit)(void *, const void *, size_t, uint32_t),
    void *context, size_t width, size_t height, size_t bytes, display_worker_t **out)
{
    assert(width == PACKET_WORDS && height == 1U && bytes == 4U);
    const display_worker_ops_t ops = {slot_alloc, slot_free, now_ms, pause_thread, task_start, task_join, callback};
    real_callback = submit;
    return display_worker_create(&ops, context, width, height, bytes, out);
}
esp_err_t platform_display_submit_game_content_rgb565(const uint16_t *pixels, size_t stride, uint32_t timeout)
{
    assert(worker_thread && stride == 768U && timeout > 0U);
    if (verify_pixels) assert(memcmp(expected, pixels, sizeof(expected)) == 0);
    atomic_fetch_add(&panel_calls, 1U);
    return atomic_load(&panel_error);
}
static void make_indexed(uint32_t seed, bool wait)
{
    uint8_t *pixels; uint32_t *palette;
    assert(doom_video_acquire_indexed(&pixels, &palette, 1000U) == ESP_OK);
    for (size_t i = 0U; i < 256U; ++i) palette[i] = (uint32_t)(i * 34571U) ^ seed;
    for (size_t i = 0U; i < FRAME_PIXELS; ++i) pixels[i] = (uint8_t)((i * 13U + seed) & 255U);
    uint32_t row[768];
    assert(doom_video_convert_indexed_touch_to_rgb565_768x480(pixels, 768U, palette, 16U, expected, 768U, row));
    assert(doom_video_publish_indexed(16U, wait, 1000U) == ESP_OK);
}
static void clean(void)
{
    assert(doom_video_deinit() == ESP_OK);
    assert(!s_worker && !s_rgb565 && !s_row && !s_xrgb && !s_lease.pixels);
    assert(heap_live == 0U && slots_live == 0U);
}
static void block(void)
{
    atomic_store(&callback_entered, false);
    atomic_store(&callback_block, true);
}
static void entered(void)
{
    const uint64_t start = now_ms();
    while (!atomic_load(&callback_entered)) { assert(now_ms()-start < 1000U); pause_thread(); }
}
int main(void)
{
    assert(DOOM_VIDEO_WIDTH == 768U && DOOM_VIDEO_HEIGHT == 480U);
    assert(doom_video_init() == ESP_OK);
    assert(heap_live == 2U && slots_live == 2U && !s_xrgb);
    verify_pixels = true;
    for (uint32_t i = 0U; i < 8U; ++i) make_indexed(i, true);
    assert(!s_xrgb && heap_live == 2U);
    uint32_t *xrgb = NULL;
    assert(doom_video_acquire_xrgb8888(&xrgb, 1000U) == ESP_OK && xrgb);
    assert(heap_live == 3U);
    for (size_t i = 0U; i < FRAME_PIXELS; ++i) xrgb[i] = (uint32_t)i * 73U;
    assert(doom_video_convert_xrgb8888_to_rgb565_768x480(xrgb, 768U, expected, 768U));
    assert(doom_video_publish_xrgb8888(false, 1000U) == ESP_OK);
    assert(!s_xrgb && heap_live == 2U);
    memset(expected, 0, sizeof(expected));
    assert(doom_video_submit_black(1000U) == ESP_OK);
    assert(!s_xrgb && heap_live == 2U);
    assert(doom_video_acquire_xrgb8888(&xrgb, 1000U) == ESP_OK);
    assert(doom_video_publish_indexed(0U, false, 1000U) == ESP_ERR_INVALID_ARG);
    assert(!s_lease.pixels && !s_xrgb && heap_live == 2U);
    uint8_t *pixels; uint32_t *palette;
    assert(doom_video_acquire_indexed(&pixels, &palette, 1000U) == ESP_OK);
    assert(doom_video_publish_indexed(UINT32_MAX, false, 1000U) == ESP_ERR_INVALID_ARG);
    assert(!s_lease.pixels);
    assert(doom_video_acquire_indexed(&pixels, &palette, 1000U) == ESP_OK);
    assert(doom_video_publish_xrgb8888(false, 1000U) == ESP_ERR_INVALID_STATE);
    clean();
    /* Creation rollback at every allocation boundary and task creation. */
    for (unsigned n = 1U; n <= 2U; ++n) {
        heap_fail_at = heap_calls + n;
        assert(doom_video_init() == ESP_ERR_NO_MEM);
        assert(heap_live == 0U && slots_live == 0U && !s_worker);
    }
    heap_fail_at = 0U;
    for (unsigned n = 1U; n <= 2U; ++n) {
        slot_fail_at = slot_calls + n;
        assert(doom_video_init() == ESP_ERR_NO_MEM);
        assert(heap_live == 0U && slots_live == 0U && !s_worker);
    }
    slot_fail_at = 0U; fail_start = true;
    assert(doom_video_init() == ESP_ERR_NO_MEM && heap_live == 0U && slots_live == 0U);
    fail_start = false;
    assert(doom_video_init() == ESP_OK);
    heap_fail_at = heap_calls + 1U;
    assert(doom_video_acquire_xrgb8888(&xrgb, 1000U) == ESP_ERR_NO_MEM && !xrgb);
    assert(!s_lease.pixels && heap_live == 2U);
    heap_fail_at = 0U;
    /* A blocked immutable indexed packet must survive a distinct next lease.
     * Timed-out second commit cancels only that second producer-owned lease. */
    block(); make_indexed(99U, false); entered();
    assert(doom_video_acquire_xrgb8888(&xrgb, 1000U) == ESP_OK);
    memset(xrgb, 0x19, FRAME_PIXELS * sizeof(*xrgb));
    assert(doom_video_publish_xrgb8888(false, 1U) == ESP_ERR_TIMEOUT);
    assert(!s_lease.pixels && !s_xrgb && heap_live == 2U);
    atomic_store(&callback_block, false);
    assert(display_worker_flush(s_worker, 1000U) == DW_OK);
    clean();
    /* An admitted true-color job outlives a flush timeout. Retain scratch
     * through a failed join, then safely retire only after the callback ends. */
    assert(doom_video_init() == ESP_OK);
    block();
    assert(doom_video_acquire_xrgb8888(&xrgb, 1000U) == ESP_OK);
    for (size_t i = 0U; i < FRAME_PIXELS; ++i) xrgb[i] = (uint32_t)i * 37U;
    assert(doom_video_convert_xrgb8888_to_rgb565_768x480(xrgb, 768U, expected, 768U));
    assert(doom_video_publish_xrgb8888(true, 1U) == ESP_ERR_TIMEOUT);
    entered();
    assert(s_xrgb && s_xrgb_pending && !s_lease.pixels && heap_live == 3U);
    fail_join = true;
    assert(doom_video_deinit() == ESP_ERR_TIMEOUT && heap_live == 3U && slots_live == 2U);
    fail_join = false; atomic_store(&callback_block, false);
    clean();
    /* Retrying admission can retire a completed timed-out compatibility job. */
    assert(doom_video_init() == ESP_OK); block();
    assert(doom_video_acquire_xrgb8888(&xrgb, 1000U) == ESP_OK);
    memset(xrgb, 0, FRAME_PIXELS * sizeof(*xrgb)); memset(expected, 0, sizeof(expected));
    assert(doom_video_publish_xrgb8888(false, 1U) == ESP_ERR_TIMEOUT);
    entered(); atomic_store(&callback_block, false);
    assert(doom_video_acquire_indexed(&pixels, &palette, 1000U) == ESP_OK);
    assert(!s_xrgb && !s_xrgb_pending && heap_live == 2U);
    assert(doom_video_publish_indexed(UINT32_MAX, false, 1000U) == ESP_ERR_INVALID_ARG);
    atomic_store(&panel_error, ESP_ERR_TIMEOUT);
    assert(doom_video_submit_black(1000U) == ESP_ERR_TIMEOUT);
    assert(doom_video_acquire_xrgb8888(&xrgb, 1000U) == ESP_ERR_TIMEOUT && !xrgb);
    clean();
    /* Backend error retires scratch but latches the worker failure. */
    assert(doom_video_init() == ESP_OK);
    assert(doom_video_acquire_xrgb8888(&xrgb, 1000U) == ESP_OK);
    memset(xrgb, 0, FRAME_PIXELS * sizeof(*xrgb));
    assert(doom_video_publish_xrgb8888(false, 1000U) == ESP_ERR_TIMEOUT);
    assert(!s_xrgb && !s_xrgb_pending && heap_live == 2U);
    clean();
    printf("PASS native packet ownership, worker-thread submission, allocation rollback, cancellation, immutable in-flight frame, commit/flush timeouts, safe join retention and error latch (%u panel frames)\n", atomic_load(&panel_calls));
    return 0;
}
