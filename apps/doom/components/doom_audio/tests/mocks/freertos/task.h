#ifndef DOOM_AUDIO_TEST_TASK_H
#define DOOM_AUDIO_TEST_TASK_H

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t task,
                       const char *name,
                       uint32_t stack_depth,
                       void *argument,
                       UBaseType_t priority,
                       TaskHandle_t *out_handle);
void vTaskDelete(TaskHandle_t task);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);

#endif
