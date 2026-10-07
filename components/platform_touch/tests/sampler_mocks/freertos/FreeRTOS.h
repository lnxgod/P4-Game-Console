#ifndef SAMPLER_TEST_FREERTOS_H
#define SAMPLER_TEST_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef struct { unsigned held; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0U }
#define pdPASS 1
#define configTICK_RATE_HZ 1000U
void sampler_test_enter(portMUX_TYPE *lock);
void sampler_test_exit(portMUX_TYPE *lock);
#define taskENTER_CRITICAL(lock_) sampler_test_enter(lock_)
#define taskEXIT_CRITICAL(lock_) sampler_test_exit(lock_)
#endif
