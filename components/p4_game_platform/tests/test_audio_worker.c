// SPDX-License-Identifier: MIT
#include "worker_runtime.h"
#include "p4/audio_worker.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void submit(p4_game_audio_worker_t *w,unsigned count,int16_t value)
{int16_t pcm[512];for(unsigned i=0;i<512;++i)pcm[i]=value;while(count){unsigned n=count>256?256:count;assert(p4_game_audio_worker_pcm(w,pcm,n));count-=n;}memset(pcm,0x33,sizeof(pcm));}
int main(void)
{
 p4_audio_mixer_t *m=calloc(1,sizeof(*m));assert(m);p4_game_audio_worker_t *w=NULL;
 p4_game_platform_audio_t audio={.running=true};
 mock_task_failure=true;assert(p4_game_audio_worker_open(&w,&audio,m)==ESP_ERR_NO_MEM&&!w);mock_task_failure=false;
 assert(p4_game_audio_worker_open(&w,&audio,m)==ESP_OK);
 assert(!p4_game_audio_worker_pcm(w,NULL,1));int16_t invalid[514]={0};assert(!p4_game_audio_worker_pcm(w,invalid,257));
 /* Reproduce the device's 40 ms frame bursts: 640 samples exceeds the old
  * 512-frame FIFO. Output on the concurrent core must remain continuous. */
 for(unsigned i=0;i<20;++i){submit(w,640,1234);mock_sleep_ms(40);}
 p4_game_audio_worker_stats_t stats={0};p4_game_audio_worker_stats(w,&stats);
 assert(stats.core==1&&stats.running&&stats.frames_written>8000);
 assert(stats.mixer.stream_blocks_rejected==0&&stats.rejected_commands==0);
 assert(stats.mixer.stream_underrun_frames==0&&mock_audio_nonzero()>1000);
 /* Stop clears old-generation queued music; rapid restart remains usable. */
 p4_game_audio_worker_stop(w);submit(w,768,-1234);mock_sleep_ms(24);
 p4_game_audio_worker_stop(w);mock_sleep_ms(24);
 p4_tone_t tone={.frequency_hz=440,.duration_ms=50,.volume_step=1,.waveform=P4_WAVE_SQUARE};
 assert(p4_game_audio_worker_tone(w,&tone));mock_sleep_ms(16);
 /* Backpressure is bounded and observable, with no busy wait. */
 unsigned rejected=0;for(unsigned i=0;i<100;++i)if(!p4_game_audio_worker_pcm(w,invalid,256))++rejected;assert(rejected);
 p4_game_audio_worker_stop(w);mock_sleep_ms(16);
 mock_audio_fail(true);mock_sleep_ms(24);p4_game_audio_worker_stats(w,&stats);
 assert(!stats.running&&stats.write_failures==1);assert(!p4_game_audio_worker_pcm(w,invalid,1));
 assert(p4_game_audio_worker_close(&w)==ESP_OK&&!w);
 mock_audio_fail(false);audio=(p4_game_platform_audio_t){.running=true};p4_audio_mixer_init(m);
 assert(p4_game_audio_worker_open(&w,&audio,m)==ESP_OK);submit(w,768,1234);mock_sleep_ms(24);
 assert(p4_game_audio_worker_close(&w)==ESP_OK&&!w);free(m);
 puts("PASS: core-1 audio, 40ms PCM bursts without underruns/rejection, copied buffers, bounded queue, stop/restart generations, failed writes, joined teardown/reopen");
}
