// SPDX-License-Identifier: GPL-2.0-or-later

#include "doom/audio_runtime.h"
#include "doom/audio_ring.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/semphr.h"
#include "freertos/task.h"

struct platform_audio {
    platform_audio_state_t state;
};

struct doom_audio_test_semaphore {
    bool allocated;
    bool deleted;
    unsigned count;
};

static unsigned s_failures;
static struct doom_audio_test_semaphore s_semaphores[2];
static unsigned s_semaphore_create_calls;
static unsigned s_fail_semaphore_create_call;
static unsigned s_semaphore_delete_calls;
static unsigned s_invalid_semaphore_calls;
static bool s_fail_task_create;
static bool s_run_task_while_waiting;
static bool s_task_active;
static TaskFunction_t s_task;
static void *s_task_argument;
static esp_err_t s_start_result;
static esp_err_t s_stop_result;
static esp_err_t s_safe_result;
static esp_err_t s_get_state_result;
static platform_audio_state_t s_start_failure_state;
static unsigned s_start_calls;
static unsigned s_stop_calls;
static unsigned s_safe_calls;
static unsigned s_write_calls;
static UBaseType_t s_worker_stack_hwm_bytes;

#define EXPECT_EQ(expected_, actual_)                                           \
    do {                                                                        \
        const int64_t expected_value_ = (int64_t)(expected_);                   \
        const int64_t actual_value_ = (int64_t)(actual_);                       \
        if (expected_value_ != actual_value_) {                                 \
            fprintf(stderr, "%s:%d: expected %lld, got %lld: %s\n",          \
                    __FILE__, __LINE__, (long long)expected_value_,             \
                    (long long)actual_value_, #actual_);                        \
            ++s_failures;                                                       \
        }                                                                       \
    } while (0)

static void reset_mocks(void)
{
    memset(s_semaphores, 0, sizeof(s_semaphores));
    s_semaphore_create_calls = 0U;
    s_fail_semaphore_create_call = 0U;
    s_semaphore_delete_calls = 0U;
    s_invalid_semaphore_calls = 0U;
    s_fail_task_create = false;
    s_run_task_while_waiting = true;
    s_task_active = false;
    s_task = NULL;
    s_task_argument = NULL;
    s_start_result = ESP_OK;
    s_stop_result = ESP_OK;
    s_safe_result = ESP_OK;
    s_get_state_result = ESP_OK;
    s_start_failure_state = PLATFORM_AUDIO_STATE_READY_MUTED;
    s_start_calls = 0U;
    s_stop_calls = 0U;
    s_safe_calls = 0U;
    s_write_calls = 0U;
    s_worker_stack_hwm_bytes = 1536U;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    ++s_semaphore_create_calls;
    if (s_semaphore_create_calls == s_fail_semaphore_create_call ||
        s_semaphore_create_calls > 2U) {
        return NULL;
    }
    struct doom_audio_test_semaphore *const semaphore =
        &s_semaphores[s_semaphore_create_calls - 1U];
    semaphore->allocated = true;
    return semaphore;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    if (semaphore == NULL || !semaphore->allocated || semaphore->deleted) {
        ++s_invalid_semaphore_calls;
        return pdFALSE;
    }
    semaphore->count = 1U;
    return pdTRUE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    (void)timeout;
    if (semaphore == NULL || !semaphore->allocated || semaphore->deleted) {
        ++s_invalid_semaphore_calls;
        return pdFALSE;
    }
    if (semaphore->count == 0U && s_task_active &&
        s_run_task_while_waiting && s_task != NULL) {
        TaskFunction_t const task = s_task;
        void *const argument = s_task_argument;
        s_task = NULL;
        task(argument);
    }
    if (semaphore->count == 0U) {
        return pdFALSE;
    }
    semaphore->count = 0U;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    if (semaphore == NULL || !semaphore->allocated || semaphore->deleted) {
        ++s_invalid_semaphore_calls;
        return;
    }
    semaphore->deleted = true;
    ++s_semaphore_delete_calls;
}

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t stack_depth,
                       void *argument,
                       UBaseType_t priority,
                       TaskHandle_t *out_handle)
{
    (void)name;
    (void)stack_depth;
    (void)priority;
    if (s_fail_task_create) {
        return pdFAIL;
    }
    s_task = task;
    s_task_argument = argument;
    s_task_active = true;
    if (out_handle != NULL) {
        *out_handle = (TaskHandle_t)(uintptr_t)1U;
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
    s_task_active = false;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return s_worker_stack_hwm_bytes;
}

esp_err_t platform_audio_force_safe_shutdown(void)
{
    ++s_safe_calls;
    return s_safe_result;
}

esp_err_t platform_audio_start(platform_audio_t *audio)
{
    ++s_start_calls;
    if (s_start_result == ESP_OK) {
        audio->state = PLATFORM_AUDIO_STATE_RUNNING;
    } else {
        audio->state = s_start_failure_state;
    }
    return s_start_result;
}

esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *interleaved_pcm,
                                      size_t frame_count)
{
    (void)audio;
    (void)interleaved_pcm;
    (void)frame_count;
    ++s_write_calls;
    return ESP_OK;
}

esp_err_t platform_audio_stop(platform_audio_t *audio)
{
    ++s_stop_calls;
    if (s_stop_result == ESP_OK) {
        audio->state = PLATFORM_AUDIO_STATE_READY_MUTED;
    }
    return s_stop_result;
}

esp_err_t platform_audio_get_state(const platform_audio_t *audio,
                                   platform_audio_state_t *out_state)
{
    if (s_get_state_result == ESP_OK) {
        *out_state = audio->state;
    }
    return s_get_state_result;
}

static esp_err_t bind_fixture(platform_audio_t *audio)
{
    const doom_audio_runtime_config_t config = {
        .platform_audio = audio,
        .sample_rate_hz = DOOM_AUDIO_OUTPUT_RATE_HZ,
        .display_owns_ldo3_ldo4 = true,
    };
    return doom_audio_runtime_bind(&config);
}

static void test_semaphore_allocation_failure_restores_bound(void)
{
    reset_mocks();
    platform_audio_t audio = {.state = PLATFORM_AUDIO_STATE_READY_MUTED};
    s_fail_semaphore_create_call = 2U;
    EXPECT_EQ(ESP_OK, bind_fixture(&audio));
    EXPECT_EQ(ESP_ERR_NO_MEM, doom_audio_runtime_start());
    EXPECT_EQ(1, s_semaphore_delete_calls);
    EXPECT_EQ(0, s_invalid_semaphore_calls);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
}

static void test_task_creation_failure_restores_bound(void)
{
    reset_mocks();
    platform_audio_t audio = {.state = PLATFORM_AUDIO_STATE_READY_MUTED};
    s_fail_task_create = true;
    EXPECT_EQ(ESP_OK, bind_fixture(&audio));
    EXPECT_EQ(ESP_ERR_NO_MEM, doom_audio_runtime_start());
    EXPECT_EQ(2, s_semaphore_delete_calls);
    EXPECT_EQ(0, s_invalid_semaphore_calls);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
}

static void test_platform_start_failure_joins_and_restores_bound(void)
{
    reset_mocks();
    platform_audio_t audio = {.state = PLATFORM_AUDIO_STATE_READY_MUTED};
    s_start_result = ESP_FAIL;
    EXPECT_EQ(ESP_OK, bind_fixture(&audio));
    EXPECT_EQ(ESP_FAIL, doom_audio_runtime_start());
    EXPECT_EQ(2, s_semaphore_delete_calls);
    EXPECT_EQ(0, s_invalid_semaphore_calls);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
}

static void test_start_join_timeout_retains_resources_for_stop(void)
{
    reset_mocks();
    platform_audio_t audio = {.state = PLATFORM_AUDIO_STATE_READY_MUTED};
    s_start_result = ESP_FAIL;
    s_run_task_while_waiting = false;
    EXPECT_EQ(ESP_OK, bind_fixture(&audio));
    EXPECT_EQ(ESP_ERR_TIMEOUT, doom_audio_runtime_start());
    EXPECT_EQ(0, s_semaphore_delete_calls);
    EXPECT_EQ(ESP_ERR_INVALID_STATE, doom_audio_runtime_unbind());

    s_run_task_while_waiting = true;
    EXPECT_EQ(ESP_OK, doom_audio_runtime_stop());
    EXPECT_EQ(2, s_semaphore_delete_calls);
    EXPECT_EQ(0, s_invalid_semaphore_calls);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
}

static void test_initial_stats_have_unsampled_worker_hwm(void)
{
    doom_audio_runtime_stats_t stats = {0};
    EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
    EXPECT_EQ(UINT32_MAX, stats.worker_stack_hwm_bytes);
}

static void test_successful_start_and_stop(void)
{
    reset_mocks();
    platform_audio_t audio = {.state = PLATFORM_AUDIO_STATE_READY_MUTED};
    doom_audio_runtime_stats_t stats = {0};
    EXPECT_EQ(ESP_OK, bind_fixture(&audio));
    EXPECT_EQ(ESP_OK, doom_audio_runtime_start());
    EXPECT_EQ(PLATFORM_AUDIO_STATE_RUNNING, audio.state);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
    EXPECT_EQ(UINT32_MAX, stats.worker_stack_hwm_bytes);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_stop());
    EXPECT_EQ(PLATFORM_AUDIO_STATE_READY_MUTED, audio.state);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
    EXPECT_EQ(1536, stats.worker_stack_hwm_bytes);
    EXPECT_EQ(0, s_write_calls);
    EXPECT_EQ(2, s_semaphore_delete_calls);
    EXPECT_EQ(0, s_invalid_semaphore_calls);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
}

static void test_stop_voice_clears_active_state(void)
{
    reset_mocks();
    platform_audio_t audio = {.state = PLATFORM_AUDIO_STATE_READY_MUTED};
    static const uint8_t sample_bytes[] = {UINT8_C(128)};
    const doom_audio_sample_t sample = {
        .samples = sample_bytes,
        .sample_count = 1U,
        .sample_rate_hz = DOOM_AUDIO_OUTPUT_RATE_HZ,
    };
    EXPECT_EQ(ESP_OK, bind_fixture(&audio));
    EXPECT_EQ(ESP_OK, doom_audio_runtime_start());
    EXPECT_EQ(true, doom_audio_runtime_start_voice(0U, &sample, 127U, 127U));
    EXPECT_EQ(true, doom_audio_runtime_voice_active(0U));
    doom_audio_runtime_stop_voice(0U);
    EXPECT_EQ(false, doom_audio_runtime_voice_active(0U));
    EXPECT_EQ(ESP_OK, doom_audio_runtime_stop());
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
}

static void test_ring_full_start_rollback_clears_active_state(void)
{
    reset_mocks();
    platform_audio_t audio = {.state = PLATFORM_AUDIO_STATE_READY_MUTED};
    static const uint8_t sample_bytes[] = {UINT8_C(128)};
    const doom_audio_sample_t sample = {
        .samples = sample_bytes,
        .sample_count = 1U,
        .sample_rate_hz = DOOM_AUDIO_OUTPUT_RATE_HZ,
    };
    EXPECT_EQ(ESP_OK, bind_fixture(&audio));
    EXPECT_EQ(ESP_OK, doom_audio_runtime_start());
    for (size_t index = 0U;
         index < (size_t)DOOM_AUDIO_COMMAND_RING_CAPACITY; ++index) {
        EXPECT_EQ(true, doom_audio_runtime_start_voice(
                            0U, &sample, 127U, 127U));
    }
    EXPECT_EQ(false, doom_audio_runtime_start_voice(
                         0U, &sample, 127U, 127U));
    EXPECT_EQ(false, doom_audio_runtime_voice_active(0U));
    doom_audio_runtime_stats_t stats = {0};
    EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
    EXPECT_EQ(1, stats.commands_dropped);
    EXPECT_EQ(ESP_OK, doom_audio_runtime_stop());
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
}

static void test_unclean_start_failure_requires_guarded_stop(void)
{
    reset_mocks();
    platform_audio_t audio = {.state = PLATFORM_AUDIO_STATE_READY_MUTED};
    s_start_result = ESP_FAIL;
    s_start_failure_state = PLATFORM_AUDIO_STATE_RUNNING;
    EXPECT_EQ(ESP_OK, bind_fixture(&audio));
    EXPECT_EQ(ESP_FAIL, doom_audio_runtime_start());
    EXPECT_EQ(ESP_ERR_INVALID_STATE, doom_audio_runtime_unbind());
    EXPECT_EQ(ESP_OK, doom_audio_runtime_stop());
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
}

int main(void)
{
    test_initial_stats_have_unsampled_worker_hwm();
    test_semaphore_allocation_failure_restores_bound();
    test_task_creation_failure_restores_bound();
    test_platform_start_failure_joins_and_restores_bound();
    test_start_join_timeout_retains_resources_for_stop();
    test_successful_start_and_stop();
    test_stop_voice_clears_active_state();
    test_ring_full_start_rollback_clears_active_state();
    test_unclean_start_failure_requires_guarded_stop();
    if (s_failures != 0U) {
        fprintf(stderr, "Doom audio runtime tests failed: %u\n", s_failures);
        return 1;
    }
    puts("P4_DOOM_AUDIO RUNTIME HOST PASS startup_cleanup=joined "
         "timeout_resources=retained backend=borrowed");
    return 0;
}
