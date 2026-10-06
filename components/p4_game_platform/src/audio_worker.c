// SPDX-License-Identifier: MIT
#include "p4/audio_worker.h"
#include <stdatomic.h>
#include <string.h>
#include "sdkconfig.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

enum { COMMANDS=8, OUTPUT_FRAMES=128, PREFILL_FRAMES=768, STACK_BYTES=4096 };
typedef struct {
    uint32_t generation;
    size_t frames; /* zero means a tone command */
    p4_tone_t tone;
    int16_t pcm[P4_GAME_MAX_AUDIO_STREAM_FRAMES*2];
} audio_command_t;
struct p4_game_audio_worker {
    p4_game_platform_audio_t *audio;
    p4_audio_mixer_t *mixer;
    QueueHandle_t queue;
    StaticQueue_t queue_control;
    uint8_t *queue_storage;
    SemaphoreHandle_t done;
    StaticSemaphore_t done_control;
    TaskHandle_t task;
    atomic_bool shutdown, running;
    atomic_uint generation, rejected;
    portMUX_TYPE stats_lock;
    p4_game_audio_worker_stats_t stats;
};
static void publish(p4_game_audio_worker_t *w)
{
    p4_game_audio_worker_stats_t s={
        .frames_written=w->audio->frames_written,
        .write_failures=w->audio->write_failures,
        .stack_remaining=(uint32_t)uxTaskGetStackHighWaterMark(NULL),
        .core=xPortGetCoreID(),.running=atomic_load(&w->running),
    };
    p4_audio_mixer_get_stats(w->mixer,&s.mixer);
    portENTER_CRITICAL(&w->stats_lock);w->stats=s;portEXIT_CRITICAL(&w->stats_lock);
}
static void run(void *opaque)
{
    p4_game_audio_worker_t *w=opaque;
    uint32_t generation=atomic_load(&w->generation);
    bool primed=false;int64_t stream_since=0;
    audio_command_t command;
    int16_t pcm[OUTPUT_FRAMES*2];
    while(!atomic_load(&w->shutdown)) {
        uint32_t current=atomic_load(&w->generation);
        if(current!=generation) {
            p4_audio_mixer_stop_all(w->mixer);generation=current;
            primed=false;stream_since=0;
        }
        /* Only the worker consumes commands or mutates mixer state. Peek
         * before receiving a PCM block so a full mixer cannot lose it. */
        for(unsigned budget=0;budget<COMMANDS;++budget) {
            if(xQueuePeek(w->queue,&command,0)!=pdTRUE)break;
            /* A producer may stop and enqueue its new song during this drain.
             * Leave that generation queued until the outer loop resets. */
            if(command.generation!=generation &&
               atomic_load(&w->generation)!=generation)break;
            if(command.generation==generation && command.frames>
                P4_GAME_AUDIO_STREAM_BUFFER_FRAMES-w->mixer->stream_queued_frames)break;
            if(xQueueReceive(w->queue,&command,0)!=pdTRUE)break;
            if(command.generation!=generation)continue;
            if(command.frames) {
                (void)p4_audio_mixer_submit_pcm16_stereo(w->mixer,command.pcm,command.frames);
                if(!stream_since)stream_since=esp_timer_get_time();
            } else (void)p4_audio_mixer_play_tone(w->mixer,&command.tone);
        }
        /* One short startup prefill absorbs uneven game-frame arrival. Tone
         * games begin immediately. A finite short stream is released at 64ms. */
        if(w->mixer->stream_active&&!primed) {
            primed=w->mixer->stream_queued_frames>=PREFILL_FRAMES ||
                (stream_since && esp_timer_get_time()-stream_since>=64000);
        }
        if(w->mixer->stream_active&&!primed)memset(pcm,0,sizeof(pcm));
        else (void)p4_audio_mixer_render(w->mixer,pcm,OUTPUT_FRAMES);
        /* The board's bounded I2S write supplies the sample clock. Never hold
         * a queue/statistics lock across this blocking operation. */
        if(p4_game_platform_audio_write(w->audio,pcm,OUTPUT_FRAMES)!=ESP_OK) {
            atomic_store(&w->running,false);publish(w);break;
        }
        publish(w);
    }
    atomic_store(&w->running,false);publish(w);
    SemaphoreHandle_t done=w->done;
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}
esp_err_t p4_game_audio_worker_open(p4_game_audio_worker_t **out,
    p4_game_platform_audio_t *audio,p4_audio_mixer_t *mixer)
{
    if(!out||*out||!mixer||!p4_game_platform_audio_running(audio))return ESP_ERR_INVALID_ARG;
    p4_game_audio_worker_t *w=heap_caps_calloc(1,sizeof(*w),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!w)return ESP_ERR_NO_MEM;
    w->stats_lock=(portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    w->queue_storage=heap_caps_calloc(COMMANDS,sizeof(audio_command_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!w->queue_storage){heap_caps_free(w);return ESP_ERR_NO_MEM;}
    w->queue=xQueueCreateStatic(COMMANDS,sizeof(audio_command_t),w->queue_storage,&w->queue_control);
    w->done=xSemaphoreCreateBinaryStatic(&w->done_control);
    w->audio=audio;w->mixer=mixer;
    atomic_init(&w->shutdown,false);atomic_init(&w->running,true);
    atomic_init(&w->generation,1);atomic_init(&w->rejected,0);
    w->stats.core=-1;
#if CONFIG_FREERTOS_UNICORE
    const BaseType_t core=0;
#else
    const BaseType_t core=1;
#endif
    if(xTaskCreatePinnedToCore(run,"p4-game-audio",STACK_BYTES,w,4,&w->task,core)!=pdPASS) {
        vSemaphoreDelete(w->done);vQueueDelete(w->queue);
        heap_caps_free(w->queue_storage);heap_caps_free(w);return ESP_ERR_NO_MEM;
    }
    *out=w;return ESP_OK;
}
static bool enqueue(p4_game_audio_worker_t *w,audio_command_t *command)
{
    if(!w||!atomic_load(&w->running)||atomic_load(&w->shutdown))return false;
    command->generation=atomic_load(&w->generation);
    if(xQueueSend(w->queue,command,0)==pdTRUE)return true;
    atomic_fetch_add(&w->rejected,1);return false;
}
bool p4_game_audio_worker_pcm(p4_game_audio_worker_t *w,const int16_t *pcm,size_t frames)
{
    if(!pcm||!frames||frames>P4_GAME_MAX_AUDIO_STREAM_FRAMES)return false;
    audio_command_t command={.frames=frames};
    memcpy(command.pcm,pcm,frames*2*sizeof(*pcm));return enqueue(w,&command);
}
bool p4_game_audio_worker_tone(p4_game_audio_worker_t *w,const p4_tone_t *tone)
{
    if(!tone||tone->frequency_hz<40||tone->frequency_hz>4000||
       !tone->duration_ms||tone->duration_ms>5000||!tone->volume_step||tone->volume_step>10||
       tone->waveform<P4_WAVE_SQUARE||tone->waveform>P4_WAVE_TRIANGLE)return false;
    audio_command_t command={.tone=*tone};return enqueue(w,&command);
}
void p4_game_audio_worker_stop(p4_game_audio_worker_t *w)
{ if(w)atomic_fetch_add(&w->generation,1); }
void p4_game_audio_worker_stats(p4_game_audio_worker_t *w,p4_game_audio_worker_stats_t *out)
{
    if(!w||!out)return;
    portENTER_CRITICAL(&w->stats_lock);*out=w->stats;portEXIT_CRITICAL(&w->stats_lock);
    out->rejected_commands=atomic_load(&w->rejected);
}
esp_err_t p4_game_audio_worker_close(p4_game_audio_worker_t **owner)
{
    if(!owner||!*owner)return ESP_OK;
    p4_game_audio_worker_t *w=*owner;
    atomic_store(&w->shutdown,true);
    if(xSemaphoreTake(w->done,pdMS_TO_TICKS(2000))!=pdTRUE)return ESP_ERR_TIMEOUT;
    vSemaphoreDelete(w->done);vQueueDelete(w->queue);
    heap_caps_free(w->queue_storage);heap_caps_free(w);*owner=NULL;return ESP_OK;
}
