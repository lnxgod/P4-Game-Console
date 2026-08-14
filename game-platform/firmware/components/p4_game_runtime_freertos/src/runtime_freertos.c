#include "p4/runtime.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

#define P4_RUNTIME_GAME_STACK_BYTES 8192U
#define P4_RUNTIME_RENDER_STACK_BYTES 6144U

#define EVENT_LOAD_DONE BIT0
#define EVENT_LOAD_FAILED BIT1
#define EVENT_STOP_REQUESTED BIT2
#define EVENT_GAME_QUIESCED BIT3
#define EVENT_RENDER_DRAINED BIT4
#define EVENT_GAME_STOPPED BIT5
#define EVENT_RENDER_STOPPED BIT6
#define EVENT_INTERRUPT_IDLE BIT7
#define EVENT_ALL                                                                      \
    (EVENT_LOAD_DONE | EVENT_LOAD_FAILED | EVENT_STOP_REQUESTED | EVENT_GAME_QUIESCED | \
     EVENT_RENDER_DRAINED | EVENT_GAME_STOPPED | EVENT_RENDER_STOPPED | EVENT_INTERRUPT_IDLE)

typedef struct {
    StaticEventGroup_t event_storage;
    EventGroupHandle_t events;
    StaticSemaphore_t mutex_storage;
    SemaphoreHandle_t mutex;

    StaticTask_t game_task_storage;
    StaticTask_t render_task_storage;
    StackType_t game_stack[P4_RUNTIME_GAME_STACK_BYTES];
    StackType_t render_stack[P4_RUNTIME_RENDER_STACK_BYTES];
    TaskHandle_t game_task;
    TaskHandle_t render_task;

    p4_runtime_config_t config;
    p4_cartridge_ref_t cartridge;
    p4_cartridge_backend_t backend;
    p4_input_source_t input;
    p4_render_sink_t render;
    p4_input_latch_t input_latch;
    p4_render_pool_t render_pool;
    p4_lifecycle_t lifecycle;
    p4_runtime_stats_t stats;
    bool interrupt_requested;
    bool initialized;
} p4_runtime_context_t;

static p4_runtime_context_t runtime_context;

static TickType_t milliseconds_to_ticks(uint32_t milliseconds)
{
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    return ticks == 0U ? (TickType_t)1U : ticks;
}

static void lock_runtime(void)
{
    (void)xSemaphoreTake(runtime_context.mutex, portMAX_DELAY);
}

static void unlock_runtime(void)
{
    (void)xSemaphoreGive(runtime_context.mutex);
}

static void ensure_initialized(void)
{
    if (runtime_context.initialized) {
        return;
    }

    runtime_context.events = xEventGroupCreateStatic(&runtime_context.event_storage);
    runtime_context.mutex = xSemaphoreCreateMutexStatic(&runtime_context.mutex_storage);
    p4_lifecycle_init(&runtime_context.lifecycle);
    p4_render_pool_init(&runtime_context.render_pool);
    p4_input_latch_init(&runtime_context.input_latch);
    runtime_context.initialized = true;
}

static uint32_t saturating_duration_us(int64_t start_us, int64_t finish_us)
{
    const int64_t elapsed = finish_us - start_us;
    if (elapsed <= 0) {
        return 0U;
    }
    if ((uint64_t)elapsed > (uint64_t)UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)elapsed;
}

static void record_error_locked(p4_status_t error)
{
    if (error != P4_STATUS_OK) {
        runtime_context.stats.last_error = error;
    }
}

static void request_stop_from_game(p4_status_t error)
{
    lock_runtime();
    if (error != P4_STATUS_OK) {
        p4_lifecycle_fault(&runtime_context.lifecycle, error);
        record_error_locked(error);
    }
    (void)p4_lifecycle_request_stop(&runtime_context.lifecycle);
    unlock_runtime();
    (void)xEventGroupSetBits(runtime_context.events, EVENT_STOP_REQUESTED);
    if (runtime_context.render_task != NULL) {
        xTaskNotifyGive(runtime_context.render_task);
    }
}

static void publish_render_result(
    p4_status_t tick_result,
    uint8_t slot,
    const p4_render_writer_t *writer)
{
    const EventBits_t bits = xEventGroupGetBits(runtime_context.events);

    lock_runtime();
    runtime_context.stats.render_commands_dropped +=
        p4_render_writer_dropped_count(writer);
    if (slot == P4_RENDER_SLOT_NONE) {
        runtime_context.stats.frames_dropped++;
    } else if (tick_result == P4_STATUS_OK && (bits & EVENT_STOP_REQUESTED) == 0U) {
        if (runtime_context.render_pool.pending_slot != P4_RENDER_SLOT_NONE) {
            runtime_context.stats.frames_dropped++;
        }
        if (p4_render_pool_publish(&runtime_context.render_pool, slot) == P4_STATUS_OK) {
            runtime_context.stats.frames_published++;
        } else {
            p4_render_pool_cancel(&runtime_context.render_pool, slot);
            runtime_context.stats.frames_dropped++;
        }
    } else {
        p4_render_pool_cancel(&runtime_context.render_pool, slot);
    }
    unlock_runtime();

    if (slot != P4_RENDER_SLOT_NONE && tick_result == P4_STATUS_OK &&
        (bits & EVENT_STOP_REQUESTED) == 0U && runtime_context.render_task != NULL) {
        xTaskNotifyGive(runtime_context.render_task);
    }
}

static p4_status_t run_one_tick(uint64_t logical_tick)
{
    p4_raw_input_t raw_input;
    p4_tick_frame_t tick_frame;
    p4_render_writer_t writer;
    uint8_t render_slot = P4_RENDER_SLOT_NONE;
    bool have_input = false;
    int64_t start_us;
    uint32_t duration_us;
    p4_status_t result;

    memset(&raw_input, 0, sizeof(raw_input));
    memset(&tick_frame, 0, sizeof(tick_frame));
    if (runtime_context.input.read != NULL) {
        have_input = runtime_context.input.read(runtime_context.input.context, &raw_input);
    }
    p4_input_latch_sample(
        &runtime_context.input_latch,
        have_input ? &raw_input : NULL,
        logical_tick,
        &tick_frame.input);

    tick_frame.tick = logical_tick;
    tick_frame.generation = runtime_context.cartridge.generation;
    tick_frame.dt_numerator = 1U;
    tick_frame.dt_denominator = P4_GAME_TICK_HZ;
    tick_frame.version = P4_GAME_API_VERSION;
    tick_frame.size = (uint16_t)sizeof(tick_frame);

    lock_runtime();
    (void)p4_render_pool_begin(
        &runtime_context.render_pool,
        runtime_context.cartridge.generation,
        logical_tick,
        &writer,
        &render_slot);
    unlock_runtime();

    start_us = esp_timer_get_time();
    result = runtime_context.backend.tick(
        runtime_context.backend.context,
        &tick_frame,
        &writer);
    duration_us = saturating_duration_us(start_us, esp_timer_get_time());
    p4_render_writer_finish(&writer);
    publish_render_result(result, render_slot, &writer);

    lock_runtime();
    runtime_context.stats.simulation_ticks++;
    if (duration_us > runtime_context.stats.max_update_us) {
        runtime_context.stats.max_update_us = duration_us;
    }
    if (duration_us > runtime_context.config.update_soft_budget_us) {
        runtime_context.stats.soft_budget_overruns++;
    }
    if (duration_us > runtime_context.config.update_hard_budget_us) {
        runtime_context.stats.hard_budget_overruns++;
    }
    if (result != P4_STATUS_OK) {
        runtime_context.stats.tick_failures++;
        record_error_locked(result);
    }
    unlock_runtime();

    if (result != P4_STATUS_OK) {
        return result;
    }
    if (duration_us > runtime_context.config.update_hard_budget_us) {
        return P4_STATUS_TIMED_OUT;
    }
    return P4_STATUS_OK;
}

static void finish_game_session(bool watchdog_subscribed)
{
    p4_status_t unload_result;

    lock_runtime();
    if (runtime_context.lifecycle.state == P4_LIFECYCLE_RUNNING ||
        runtime_context.lifecycle.state == P4_LIFECYCLE_LOADING ||
        runtime_context.lifecycle.state == P4_LIFECYCLE_FAULTED) {
        (void)p4_lifecycle_request_stop(&runtime_context.lifecycle);
    }
    (void)p4_lifecycle_mark_quiesced(&runtime_context.lifecycle);
    (void)p4_lifecycle_begin_render_drain(&runtime_context.lifecycle);
    (void)xEventGroupSetBits(
        runtime_context.events,
        EVENT_STOP_REQUESTED | EVENT_GAME_QUIESCED);
    if (runtime_context.render_task != NULL) {
        xTaskNotifyGive(runtime_context.render_task);
    }
    unlock_runtime();

    if (watchdog_subscribed) {
        (void)esp_task_wdt_delete(NULL);
    }

    (void)xEventGroupWaitBits(
        runtime_context.events,
        EVENT_RENDER_DRAINED,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);
    (void)xEventGroupWaitBits(
        runtime_context.events,
        EVENT_INTERRUPT_IDLE,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    lock_runtime();
    (void)p4_lifecycle_begin_unload(&runtime_context.lifecycle);
    unlock_runtime();
    unload_result = runtime_context.backend.unload(runtime_context.backend.context);

    lock_runtime();
    if (unload_result == P4_STATUS_OK) {
        (void)p4_lifecycle_finish_unload(&runtime_context.lifecycle);
    } else {
        record_error_locked(unload_result);
        p4_lifecycle_init(&runtime_context.lifecycle);
    }
    runtime_context.stats.game_stack_min_free_bytes =
        (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    unlock_runtime();
}

static void run_game_session(void)
{
    p4_tick_scheduler_t scheduler;
    TickType_t wake_time;
    uint64_t logical_tick = 0U;
    uint32_t immediate_catchups = 0U;
    bool watchdog_subscribed = false;
    p4_status_t result;

    result = p4_tick_scheduler_init(
        &scheduler,
        (uint32_t)configTICK_RATE_HZ,
        runtime_context.config.tick_hz);
    if (result != P4_STATUS_OK) {
        request_stop_from_game(result);
        finish_game_session(false);
        return;
    }

    if (runtime_context.config.subscribe_game_task_watchdog &&
        esp_task_wdt_add(NULL) == ESP_OK) {
        watchdog_subscribed = true;
        lock_runtime();
        runtime_context.stats.game_watchdog_subscribed = true;
        unlock_runtime();
    }

    result = runtime_context.backend.load(
        runtime_context.backend.context,
        &runtime_context.cartridge);
    if (watchdog_subscribed) {
        (void)esp_task_wdt_reset();
    }
    if (result != P4_STATUS_OK) {
        lock_runtime();
        p4_lifecycle_fault(&runtime_context.lifecycle, result);
        record_error_locked(result);
        (void)p4_lifecycle_request_stop(&runtime_context.lifecycle);
        unlock_runtime();
        (void)xEventGroupSetBits(
            runtime_context.events,
            EVENT_LOAD_FAILED | EVENT_STOP_REQUESTED);
        finish_game_session(watchdog_subscribed);
        return;
    }

    lock_runtime();
    (void)p4_lifecycle_mark_loaded(&runtime_context.lifecycle);
    unlock_runtime();
    (void)xEventGroupSetBits(runtime_context.events, EVENT_LOAD_DONE);

    wake_time = xTaskGetTickCount();
    while ((xEventGroupGetBits(runtime_context.events) & EVENT_STOP_REQUESTED) == 0U) {
        uint32_t interval = 0U;
        BaseType_t delayed;

        result = p4_tick_scheduler_next(&scheduler, &interval);
        if (result != P4_STATUS_OK) {
            request_stop_from_game(result);
            break;
        }

        delayed = xTaskDelayUntil(&wake_time, (TickType_t)interval);
        if (delayed == pdFALSE) {
            bool rebase = false;
            lock_runtime();
            runtime_context.stats.deadline_misses++;
            if (immediate_catchups >= runtime_context.config.max_catchup_ticks) {
                runtime_context.stats.schedule_rebases++;
                rebase = true;
            } else {
                immediate_catchups++;
            }
            unlock_runtime();

            if (rebase) {
                immediate_catchups = 0U;
                scheduler.phase = 0U;
                wake_time = xTaskGetTickCount();
                result = p4_tick_scheduler_next(&scheduler, &interval);
                if (result != P4_STATUS_OK) {
                    request_stop_from_game(result);
                    break;
                }
                (void)xTaskDelayUntil(&wake_time, (TickType_t)interval);
            }
        } else {
            immediate_catchups = 0U;
        }

        if ((xEventGroupGetBits(runtime_context.events) & EVENT_STOP_REQUESTED) != 0U) {
            break;
        }

        result = run_one_tick(logical_tick);
        logical_tick++;
        if (watchdog_subscribed) {
            (void)esp_task_wdt_reset();
        }
        if (result != P4_STATUS_OK) {
            request_stop_from_game(result);
            break;
        }
    }

    finish_game_session(watchdog_subscribed);
}

static void game_task(void *argument)
{
    (void)argument;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_game_session();
        (void)xEventGroupSetBits(runtime_context.events, EVENT_GAME_STOPPED);
    }
}

static void run_render_session(void)
{
    const TickType_t poll_ticks = milliseconds_to_ticks(100U);
    bool finished = false;

    while (!finished) {
        const p4_render_packet_t *packet = NULL;
        uint8_t slot = P4_RENDER_SLOT_NONE;
        p4_status_t acquire_result;

        (void)ulTaskNotifyTake(pdTRUE, poll_ticks);
        for (;;) {
            lock_runtime();
            acquire_result = p4_render_pool_acquire_latest(
                &runtime_context.render_pool,
                runtime_context.cartridge.generation,
                &packet,
                &slot);
            if (acquire_result != P4_STATUS_OK && acquire_result != P4_STATUS_WOULD_BLOCK) {
                runtime_context.stats.render_failures++;
                record_error_locked(acquire_result);
            }
            unlock_runtime();

            if (acquire_result != P4_STATUS_OK) {
                break;
            }

            {
                const p4_status_t submit_result = runtime_context.render.submit(
                    runtime_context.render.context,
                    packet);
                lock_runtime();
                p4_render_pool_release(&runtime_context.render_pool, slot);
                if (submit_result == P4_STATUS_OK) {
                    runtime_context.stats.frames_rendered++;
                } else {
                    runtime_context.stats.render_failures++;
                    record_error_locked(submit_result);
                }
                unlock_runtime();
            }
        }

        if ((xEventGroupGetBits(runtime_context.events) &
             (EVENT_STOP_REQUESTED | EVENT_GAME_QUIESCED)) ==
            (EVENT_STOP_REQUESTED | EVENT_GAME_QUIESCED)) {
            lock_runtime();
            finished = !p4_render_pool_generation_busy(
                &runtime_context.render_pool,
                runtime_context.cartridge.generation);
            unlock_runtime();
        }
    }

    lock_runtime();
    runtime_context.stats.render_stack_min_free_bytes =
        (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    unlock_runtime();
    (void)xEventGroupSetBits(runtime_context.events, EVENT_RENDER_DRAINED);
}

static void render_task(void *argument)
{
    (void)argument;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_render_session();
        lock_runtime();
        /* No producer remains after GAME_QUIESCED, so these are stale data/stop wakes. */
        (void)ulTaskNotifyTake(pdTRUE, 0U);
        (void)xEventGroupSetBits(runtime_context.events, EVENT_RENDER_STOPPED);
        unlock_runtime();
    }
}

void p4_runtime_default_config(p4_runtime_config_t *config_out)
{
    if (config_out == NULL) {
        return;
    }

    config_out->tick_hz = P4_GAME_TICK_HZ;
    config_out->update_soft_budget_us = 8000U;
    config_out->update_hard_budget_us = 100000U;
    config_out->max_catchup_ticks = 2U;
    config_out->startup_timeout_ms = 1000U;
    config_out->stop_timeout_ms = 250U;
    config_out->game_task_priority = 6U;
    config_out->render_task_priority = 5U;
    config_out->subscribe_game_task_watchdog = true;
}

static bool start_arguments_valid(
    const p4_runtime_config_t *config,
    const p4_cartridge_ref_t *cartridge,
    const p4_cartridge_backend_t *backend,
    const p4_render_sink_t *render)
{
    return config != NULL && cartridge != NULL && backend != NULL && render != NULL &&
           config->tick_hz == P4_GAME_TICK_HZ && config->update_soft_budget_us != 0U &&
           config->update_hard_budget_us >= config->update_soft_budget_us &&
           config->max_catchup_ticks <= 10U && config->startup_timeout_ms != 0U &&
           config->stop_timeout_ms != 0U && config->game_task_priority < configMAX_PRIORITIES &&
           config->render_task_priority < configMAX_PRIORITIES && cartridge->generation != 0U &&
           configTICK_RATE_HZ >= config->tick_hz &&
           cartridge->game_api_version == P4_GAME_API_VERSION && backend->load != NULL &&
           backend->tick != NULL && backend->unload != NULL && render->submit != NULL;
}

p4_status_t p4_runtime_start(
    const p4_runtime_config_t *config,
    const p4_cartridge_ref_t *cartridge,
    const p4_cartridge_backend_t *backend,
    const p4_input_source_t *input,
    const p4_render_sink_t *render)
{
    EventBits_t bits;
    bool render_task_created = false;

    if (!start_arguments_valid(config, cartridge, backend, render)) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    ensure_initialized();

    lock_runtime();
    bits = xEventGroupGetBits(runtime_context.events);
    if (runtime_context.lifecycle.state != P4_LIFECYCLE_IDLE ||
        (runtime_context.game_task != NULL && (bits & EVENT_GAME_STOPPED) == 0U) ||
        (runtime_context.render_task != NULL && (bits & EVENT_RENDER_STOPPED) == 0U)) {
        unlock_runtime();
        return P4_STATUS_INVALID_STATE;
    }

    runtime_context.config = *config;
    runtime_context.cartridge = *cartridge;
    runtime_context.backend = *backend;
    memset(&runtime_context.input, 0, sizeof(runtime_context.input));
    if (input != NULL) {
        runtime_context.input = *input;
    }
    runtime_context.render = *render;
    memset(&runtime_context.stats, 0, sizeof(runtime_context.stats));
    runtime_context.interrupt_requested = false;
    p4_render_pool_init(&runtime_context.render_pool);
    p4_input_latch_reset(&runtime_context.input_latch);
    (void)p4_lifecycle_begin_load(
        &runtime_context.lifecycle,
        runtime_context.cartridge.generation);
    (void)xEventGroupClearBits(runtime_context.events, EVENT_ALL);
    (void)xEventGroupSetBits(runtime_context.events, EVENT_INTERRUPT_IDLE);

    if (runtime_context.render_task == NULL) {
        runtime_context.render_task = xTaskCreateStatic(
            render_task,
            "p4_render",
            P4_RUNTIME_RENDER_STACK_BYTES,
            NULL,
            (UBaseType_t)config->render_task_priority,
            runtime_context.render_stack,
            &runtime_context.render_task_storage);
        render_task_created = runtime_context.render_task != NULL;
    }
    if (runtime_context.render_task == NULL) {
        record_error_locked(P4_STATUS_BACKEND_FAILED);
        p4_lifecycle_init(&runtime_context.lifecycle);
        unlock_runtime();
        return P4_STATUS_BACKEND_FAILED;
    }
    vTaskPrioritySet(
        runtime_context.render_task,
        (UBaseType_t)config->render_task_priority);

    if (runtime_context.game_task == NULL) {
        runtime_context.game_task = xTaskCreateStatic(
            game_task,
            "p4_game",
            P4_RUNTIME_GAME_STACK_BYTES,
            NULL,
            (UBaseType_t)config->game_task_priority,
            runtime_context.game_stack,
            &runtime_context.game_task_storage);
    }
    if (runtime_context.game_task == NULL) {
        record_error_locked(P4_STATUS_BACKEND_FAILED);
        (void)xEventGroupSetBits(
            runtime_context.events,
            EVENT_STOP_REQUESTED | EVENT_GAME_QUIESCED);
        unlock_runtime();
        lock_runtime();
        p4_lifecycle_init(&runtime_context.lifecycle);
        if (render_task_created) {
            (void)xEventGroupSetBits(runtime_context.events, EVENT_RENDER_STOPPED);
        }
        unlock_runtime();
        return P4_STATUS_BACKEND_FAILED;
    }
    vTaskPrioritySet(
        runtime_context.game_task,
        (UBaseType_t)config->game_task_priority);
    unlock_runtime();

    xTaskNotifyGive(runtime_context.render_task);
    xTaskNotifyGive(runtime_context.game_task);

    bits = xEventGroupWaitBits(
        runtime_context.events,
        EVENT_LOAD_DONE | EVENT_LOAD_FAILED,
        pdFALSE,
        pdFALSE,
        milliseconds_to_ticks(config->startup_timeout_ms));
    if ((bits & EVENT_LOAD_DONE) != 0U) {
        return P4_STATUS_OK;
    }
    if ((bits & EVENT_LOAD_FAILED) != 0U) {
        (void)p4_runtime_request_stop(config->stop_timeout_ms);
        return P4_STATUS_BACKEND_FAILED;
    }

    (void)p4_runtime_request_stop(config->stop_timeout_ms);
    return P4_STATUS_TIMED_OUT;
}

p4_status_t p4_runtime_request_stop(uint32_t timeout_ms)
{
    EventBits_t bits;
    void (*interrupt)(void *context) = NULL;
    void *interrupt_context = NULL;
    bool workers_parked;

    if (timeout_ms == 0U) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    ensure_initialized();

    lock_runtime();
    bits = xEventGroupGetBits(runtime_context.events);
    workers_parked =
        (runtime_context.game_task == NULL || (bits & EVENT_GAME_STOPPED) != 0U) &&
        (runtime_context.render_task == NULL || (bits & EVENT_RENDER_STOPPED) != 0U);
    if (runtime_context.lifecycle.state == P4_LIFECYCLE_IDLE &&
        workers_parked) {
        const p4_status_t idle_error = runtime_context.stats.last_error;
        unlock_runtime();
        return idle_error;
    }
    if (runtime_context.lifecycle.state == P4_LIFECYCLE_LOADING ||
        runtime_context.lifecycle.state == P4_LIFECYCLE_RUNNING ||
        runtime_context.lifecycle.state == P4_LIFECYCLE_FAULTED) {
        (void)p4_lifecycle_request_stop(&runtime_context.lifecycle);
    }
    (void)xEventGroupSetBits(runtime_context.events, EVENT_STOP_REQUESTED);
    if (runtime_context.backend.request_interrupt != NULL &&
        !runtime_context.interrupt_requested &&
        (runtime_context.lifecycle.state == P4_LIFECYCLE_LOADING ||
         runtime_context.lifecycle.state == P4_LIFECYCLE_RUNNING ||
         runtime_context.lifecycle.state == P4_LIFECYCLE_STOP_REQUESTED)) {
        runtime_context.interrupt_requested = true;
        interrupt = runtime_context.backend.request_interrupt;
        interrupt_context = runtime_context.backend.context;
        (void)xEventGroupClearBits(runtime_context.events, EVENT_INTERRUPT_IDLE);
    }
    if (runtime_context.render_task != NULL && (bits & EVENT_RENDER_STOPPED) == 0U) {
        xTaskNotifyGive(runtime_context.render_task);
    }
    unlock_runtime();

    if (interrupt != NULL) {
        interrupt(interrupt_context);
        (void)xEventGroupSetBits(runtime_context.events, EVENT_INTERRUPT_IDLE);
    }

    bits = xEventGroupWaitBits(
        runtime_context.events,
        EVENT_GAME_STOPPED | EVENT_RENDER_STOPPED,
        pdFALSE,
        pdTRUE,
        milliseconds_to_ticks(timeout_ms));
    if ((bits & (EVENT_GAME_STOPPED | EVENT_RENDER_STOPPED)) !=
        (EVENT_GAME_STOPPED | EVENT_RENDER_STOPPED)) {
        return P4_STATUS_TIMED_OUT;
    }

    lock_runtime();
    {
        const p4_status_t result = runtime_context.stats.last_error;
        unlock_runtime();
        return result;
    }
}

void p4_runtime_get_stats(p4_runtime_stats_t *stats_out)
{
    if (stats_out == NULL) {
        return;
    }
    ensure_initialized();
    lock_runtime();
    *stats_out = runtime_context.stats;
    unlock_runtime();
}

p4_lifecycle_state_t p4_runtime_get_state(void)
{
    p4_lifecycle_state_t state;
    ensure_initialized();
    lock_runtime();
    state = runtime_context.lifecycle.state;
    unlock_runtime();
    return state;
}
