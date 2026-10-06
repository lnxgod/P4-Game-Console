// SPDX-License-Identifier: MIT
// CPU update/render benchmark, not a display or device-FPS acceptance test.
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "p4/audio.h"
#include "p4/game.h"
extern const p4_game_descriptor_t P4_HOST_GAME_DESCRIPTOR;
enum { LIMIT = 10000, EVENTS = 4096 };
typedef struct { unsigned frame, buttons; int x, y; } event_t;
static event_t events[EVENTS];
static double updates[LIMIT], renders[LIMIT], totals[LIMIT];
static double now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) abort();
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1000000.0;
}
static int compare(const void *a, const void *b) {
    const double x = *(const double *)a, y = *(const double *)b;
    return x > y ? 1 : (x < y ? -1 : 0);
}
static void stats(const char *name, double *samples, unsigned n) {
    double sum = 0.0;
    unsigned missed = 0U;
    for (unsigned i=0; i<n; ++i) { sum += samples[i]; if(samples[i]>1000.0/30.0) ++missed; }
    qsort(samples, n, sizeof(*samples), compare);
    printf("\"%s_ms\":{\"mean\":%.4f,\"p50\":%.4f,\"p95\":%.4f,\"p99\":%.4f,\"max\":%.4f,\"over_33_333ms\":%u}",
           name, sum/n, samples[n/2U], samples[n*95U/100U], samples[n*99U/100U], samples[n-1U], missed);
}
int main(int argc, char **argv) {
    const unsigned frames = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 2000U;
    const unsigned width = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 768U;
    if (frames < 100U || frames > LIMIT || (width != 320U && width != 768U) || argc > 4) {
        fprintf(stderr,"usage: %s [100..10000 frames] [320|768 width] [input-tape.txt]\n",argv[0]); return 2;
    }
    unsigned event_count=0U;
    if(argc == 4) {
        FILE *file=fopen(argv[3],"r");
        if(file==NULL) return 2;
        while(event_count<EVENTS) {
            event_t *e=&events[event_count];
            const int fields=fscanf(file,"%u %u %d %d",&e->frame,&e->buttons,&e->x,&e->y);
            if(fields==EOF) break;
            if(fields!=4 || e->buttons>255U || e->x < -1 || e->x >319 || e->y < -1 || e->y>199 ||
               (event_count>0U && e->frame<=events[event_count-1U].frame)) { fclose(file); return 2; }
            ++event_count;
        }
        fclose(file);
    }
    const unsigned height=width==768U?480U:200U;
    uint16_t *pixels=calloc((size_t)width*height,sizeof(*pixels));
    void *state=calloc(1U,P4_HOST_GAME_DESCRIPTOR.state_bytes);
    if(pixels==NULL || state==NULL) return 2;
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    const p4_game_services_t services={
        .available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_AUDIO_TONE|P4_GAME_CAP_AUDIO_STREAM|
            (width==768U?P4_GAME_CAP_VIDEO_HIGH_RES:0U),
        .game_id=P4_HOST_GAME_DESCRIPTOR.id,.audio_context=&mixer,
        .play_tone=p4_audio_mixer_service_play_tone,
        .submit_pcm16_stereo=p4_audio_mixer_service_submit_pcm16_stereo,
        .stop_audio=p4_audio_mixer_service_stop
    };
    p4_game_instance_t game={0};
    if(!p4_game_instance_start(&game,&P4_HOST_GAME_DESCRIPTOR,&services,state,P4_HOST_GAME_DESCRIPTOR.state_bytes)) return 2;
    p4_game_surface_t surface={.pixels=pixels,.stride_pixels=width,.width=(uint16_t)width,.height=(uint16_t)height};
    unsigned event_index=0U; event_t current={0U,0U,-1,-1};
    uint32_t previous=0U, digest=0U, last=0U, changed=0U;
    int16_t audio[267U*2U];
    for(unsigned frame=0U; frame<frames; ++frame) {
        if(argc==4) {
            if(event_index<event_count && events[event_index].frame==frame) current=events[event_index++];
        } else {
            current.buttons=frame<30U?0U:((frame%120U==30U?P4_BUTTON_A:0U)|
                (frame%240U<120U?P4_BUTTON_RIGHT:P4_BUTTON_LEFT)|
                (frame%120U==60U?P4_BUTTON_B:0U));
        }
        p4_game_input_t input={.held=current.buttons,.pressed=current.buttons&~previous,
            .released=previous&~current.buttons,.touch_valid=true,
            .touch_count=(uint8_t)(current.x>=0 && current.y>=0?1U:0U)};
        if(input.touch_count) input.touches[0]=(p4_game_point_t){(uint16_t)current.x,(uint16_t)current.y};
        previous=current.buttons;
        const uint32_t elapsed=frame%3U==2U?16U:17U;
        const double begin=now_ms();
        if(p4_game_instance_update(&game,&input,elapsed)!=P4_GAME_CONTINUE) { fprintf(stderr,"update failed/exited at frame%u\n",frame); return 3; }
        if(!p4_audio_mixer_render(&mixer,audio,frame%3U==2U?266U:267U)) return 3;
        const double updated=now_ms();
        if(!p4_game_instance_render(&game,&surface)) return 3;
        const double end=now_ms();
        updates[frame]=updated-begin; renders[frame]=end-updated; totals[frame]=end-begin;
        digest=2166136261U;
        for(size_t i=0U;i<(size_t)width*height;i+=17U) digest=(digest^pixels[i])*16777619U;
        if(frame>0U && digest!=last) ++changed;
        last=digest;
    }
    printf("{\"game\":\"%s\",\"frames\":%u,\"width\":%u,\"height\":%u,\"mode\":\"optimized native host CPU; real game+tone/PCM mixer; excludes display, device and transport\",\"changed_frames\":%u,\"last_frame_hash\":\"%08" PRIx32 "\",",P4_HOST_GAME_DESCRIPTOR.id,frames,width,height,changed,digest);
    stats("update_audio",updates,frames); printf(","); stats("render",renders,frames); printf(","); stats("total",totals,frames); puts("}");
    p4_game_instance_stop(&game); free(pixels); free(state); return 0;
}
