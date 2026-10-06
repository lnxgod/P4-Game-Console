// SPDX-License-Identifier: MIT
#include "worker_runtime.h"
#include "p4/audio_worker.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
bool mock_task_failure;
static atomic_bool fail_audio;
static atomic_uint nonzero;
static _Thread_local int core_id;
void *heap_caps_calloc(size_t n,size_t bytes,unsigned caps){(void)caps;return calloc(n,bytes);}
void heap_caps_free(void *p){free(p);}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void mock_sleep_ms(unsigned ms){struct timespec t={.tv_sec=ms/1000,.tv_nsec=(long)(ms%1000)*1000000};while(nanosleep(&t,&t)&&errno==EINTR){}}
QueueHandle_t xQueueCreateStatic(unsigned n,size_t size,uint8_t *storage,StaticQueue_t *q)
{*q=(StaticQueue_t){.storage=storage,.size=size,.capacity=n};pthread_mutex_init(&q->lock,NULL);return q;}
int xQueuePeek(QueueHandle_t q,void *out,unsigned wait)
{assert(!wait);pthread_mutex_lock(&q->lock);bool ok=q->count!=0;if(ok)memcpy(out,q->storage+q->head*q->size,q->size);pthread_mutex_unlock(&q->lock);return ok;}
int xQueueReceive(QueueHandle_t q,void *out,unsigned wait)
{assert(!wait);pthread_mutex_lock(&q->lock);bool ok=q->count!=0;if(ok){memcpy(out,q->storage+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;--q->count;}pthread_mutex_unlock(&q->lock);return ok;}
int xQueueSend(QueueHandle_t q,const void *in,unsigned wait)
{assert(!wait);pthread_mutex_lock(&q->lock);bool ok=q->count<q->capacity;if(ok){memcpy(q->storage+((q->head+q->count)%q->capacity)*q->size,in,q->size);++q->count;}pthread_mutex_unlock(&q->lock);return ok;}
void vQueueDelete(QueueHandle_t q){pthread_mutex_destroy(&q->lock);}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *s)
{pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->ready,NULL);s->given=false;return s;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait)
{struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=wait/1000;until.tv_nsec+=(long)(wait%1000)*1000000;if(until.tv_nsec>=1000000000){++until.tv_sec;until.tv_nsec-=1000000000;}pthread_mutex_lock(&s->lock);int result=0;while(!s->given&&!result)result=pthread_cond_timedwait(&s->ready,&s->lock,&until);bool ok=s->given;s->given=false;pthread_mutex_unlock(&s->lock);return ok;}
int xSemaphoreGive(SemaphoreHandle_t s){pthread_mutex_lock(&s->lock);s->given=true;pthread_cond_signal(&s->ready);pthread_mutex_unlock(&s->lock);return 1;}
void vSemaphoreDelete(SemaphoreHandle_t s){pthread_cond_destroy(&s->ready);pthread_mutex_destroy(&s->lock);}
typedef struct {void (*run)(void *);void *arg;int core;} Launch;
static void *trampoline(void *opaque){Launch launch=*(Launch *)opaque;free(opaque);core_id=launch.core;launch.run(launch.arg);return NULL;}
int xTaskCreatePinnedToCore(void (*run)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out,int core)
{assert(!strcmp(name,"p4-game-audio")&&stack==4096&&priority==4&&core==1);if(mock_task_failure)return 0;Launch *l=malloc(sizeof(*l));assert(l);*l=(Launch){run,arg,core};pthread_t thread;assert(!pthread_create(&thread,NULL,trampoline,l));pthread_detach(thread);*out=(void *)1;return pdPASS;}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task){(void)task;return 1024;}
int xPortGetCoreID(void){return core_id;}
void vTaskDelete(TaskHandle_t task){assert(!task);pthread_exit(NULL);}
void mock_audio_fail(bool fail){atomic_store(&fail_audio,fail);}
unsigned mock_audio_nonzero(void){return atomic_load(&nonzero);}
bool p4_game_platform_audio_running(const p4_game_platform_audio_t *a){return a&&a->running;}
esp_err_t p4_game_platform_audio_write(p4_game_platform_audio_t *a,const int16_t *pcm,size_t n)
{assert(n==128&&core_id==1);mock_sleep_ms(8);if(atomic_load(&fail_audio)){++a->write_failures;a->running=false;return ESP_FAIL;}for(size_t i=0;i<n*2;++i)if(pcm[i]){assert(pcm[i]==1234||pcm[i]==-1234||pcm[i]==400||pcm[i]==-400);atomic_fetch_add(&nonzero,1);}++a->writes;a->frames_written+=(uint32_t)n;return ESP_OK;}
