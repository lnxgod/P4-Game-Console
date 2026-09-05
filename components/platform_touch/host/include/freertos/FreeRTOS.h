#ifndef HOST_FREERTOS_FREERTOS_H
#define HOST_FREERTOS_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(milliseconds_) ((TickType_t)(milliseconds_))

#endif
