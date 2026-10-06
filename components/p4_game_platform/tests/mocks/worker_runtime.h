// SPDX-License-Identifier: MIT
#ifndef P4_WORKER_TEST_RUNTIME_H
#define P4_WORKER_TEST_RUNTIME_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(m) ((void)pthread_mutex_lock(m))
#define portEXIT_CRITICAL(m) ((void)pthread_mutex_unlock(m))
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
typedef struct { pthread_mutex_t lock;uint8_t *storage;size_t size,capacity,head,count; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
typedef struct { pthread_mutex_t lock;pthread_cond_t ready;bool given; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
void *heap_caps_calloc(size_t,size_t,unsigned);
void heap_caps_free(void *);
int64_t esp_timer_get_time(void);
QueueHandle_t xQueueCreateStatic(unsigned,size_t,uint8_t *,StaticQueue_t *);
int xQueuePeek(QueueHandle_t,void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
int xQueueSend(QueueHandle_t,const void *,unsigned);
void vQueueDelete(QueueHandle_t);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *,int);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
int xPortGetCoreID(void);
void vTaskDelete(TaskHandle_t);
extern bool mock_task_failure;
void mock_audio_fail(bool);
unsigned mock_audio_nonzero(void);
void mock_sleep_ms(unsigned);
#endif
