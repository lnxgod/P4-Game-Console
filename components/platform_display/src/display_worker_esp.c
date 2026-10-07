/* Worker contains no display hardware ownership.
 * The injected submission callback uses the existing shared blocking service. */
#include "platform/display_worker_esp.h"
#include <stdatomic.h>
#include <stdlib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

typedef struct {
    void (*entry)(void *);
    void *argument;
    atomic_bool quiescent;
} worker_task_t;

static void *allocate_pixels(size_t bytes)
{
    return heap_caps_aligned_alloc(64U, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static uint64_t monotonic_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}
static void pause_tick(void) { vTaskDelay(1U); }

static void task_entry(void *argument)
{
    worker_task_t *task = argument;
    task->entry(task->argument);
    /* FINAL access to task/worker/pixels. join may free them after acquiring
     * this store. Only this task's independent FreeRTOS TCB/stack remain; they
     * are reaped through normal self-deletion, never forced cross-core delete. */
    atomic_store_explicit(&task->quiescent, true, memory_order_release);
    vTaskDelete(NULL);
}
static int start(void (*entry)(void *), void *argument, void **out)
{
    worker_task_t *task = heap_caps_calloc(1U, sizeof(*task),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!task) return DW_NO_MEMORY;
    task->entry = entry; task->argument = argument;
    atomic_init(&task->quiescent, false);
    /* Core 0 owns engine callbacks. Priority 2 is below Doom audio (3),
     * Wi-Fi lifecycle (4), and BLE startup (5). 4096 internal stack bytes;
     * hardware stack headroom and fairness remain required acceptance gates. */
    if (xTaskCreatePinnedToCore(task_entry, "p4-display", 4096U, task, 2U,
                               NULL, 1) != pdPASS) {
        heap_caps_free(task); return DW_NO_MEMORY;
    }
    *out = task;
    return DW_OK;
}
static int join(void *opaque, uint32_t timeout)
{
    worker_task_t *task = opaque;
    const uint64_t began = monotonic_ms();
    while (!atomic_load_explicit(&task->quiescent, memory_order_acquire)) {
        if (monotonic_ms() - began >= timeout) return DW_TIMEOUT;
        pause_tick();
    }
    heap_caps_free(task);
    return DW_OK;
}
int display_worker_esp_create(
    int (*submit)(void *, const void *, size_t, uint32_t), void *context,
    size_t width, size_t height, size_t pixel_bytes,
    display_worker_t **out)
{
    const display_worker_ops_t ops = {allocate_pixels, heap_caps_free,
        monotonic_ms, pause_tick, start, join, submit};
    return display_worker_create(&ops, context, width, height, pixel_bytes,
                                 out);
}
