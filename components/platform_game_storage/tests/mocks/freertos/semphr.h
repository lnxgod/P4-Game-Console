#pragma once
typedef int StaticSemaphore_t;
typedef int *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p) { return p; }
static inline int xSemaphoreTake(SemaphoreHandle_t p,int delay) { (void)p; (void)delay; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t p) { (void)p; return 1; }
