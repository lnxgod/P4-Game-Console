#ifndef DOOM_AUDIO_TEST_FREERTOS_H
#define DOOM_AUDIO_TEST_FREERTOS_H

#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(milliseconds_) ((TickType_t)(milliseconds_))
#define tskIDLE_PRIORITY 0U

#endif
