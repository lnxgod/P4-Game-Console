#ifndef SAMPLER_TEST_TASK_H
#define SAMPLER_TEST_TASK_H
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(void (*entry)(void *), const char *name, uint32_t stack,
                      void *context, UBaseType_t priority, TaskHandle_t *handle);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *last, TickType_t interval);
void vTaskDelete(TaskHandle_t handle);
#endif
