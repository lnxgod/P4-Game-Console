// SPDX-License-Identifier: MIT
/* Resource-backed CPU trace for this game's launch qualification. No SDL or
 * alternative runtime: interactive play uses tools/p4-game-host unchanged. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "p4/audio.h"
#include "p4/game.h"
#include "byte_buddy_save.h"
extern const p4_game_descriptor_t p4_byte_buddy_game;
enum { FRAMES=2000, MAX_EVENTS=4096 };
typedef struct { unsigned frame, buttons; int x,y; } event_t;
static event_t events[MAX_EVENTS];
static double update_times[FRAMES], render_times[FRAMES], total_times[FRAMES];
static double now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)!=0) abort();
    return (double)t.tv_sec*1000.0+(double)t.tv_nsec/1000000.0;
}
static int compare(const void *a,const void *b) {
    const double x=*(const double *)a,y=*(const double *)b;
    return x>y ? 1 : x<y ? -1 : 0;
}
static void stats(const char *name,double *values) {
    unsigned missed=0;
    for (unsigned i=0;i<FRAMES;++i) if (values[i]>1000.0/30.0) ++missed;
    qsort(values,FRAMES,sizeof(*values),compare);
    printf("\"%s_ms\":{\"p50\":%.4f,\"p95\":%.4f,\"p99\":%.4f,\"max\":%.4f,\"over_33_333ms\":%u}",
        name,values[FRAMES/2],values[FRAMES*95/100],values[FRAMES*99/100],values[FRAMES-1],missed);
}
static bool queue_save(void *context,const char *slot,uint32_t schema,
                       uint32_t expected,const uint8_t *data,size_t bytes,
                       p4_game_save_ticket_t *ticket) {
    (void)slot;(void)schema;(void)data;(void)bytes;
    *(uint32_t *)context=expected+1U;*ticket=expected+1U;return true;
}
static bool save_status(void *context,p4_game_save_ticket_t ticket,
                       p4_game_save_status_t *status,uint32_t *sequence) {
    (void)ticket;*status=P4_GAME_SAVE_COMMITTED;*sequence=*(uint32_t *)context;return true;
}
static bool capture(const char *prefix,unsigned frame,const p4_game_surface_t *s) {
    char path[1024];
    if (snprintf(path,sizeof(path),"%s-%04u.ppm",prefix,frame)<0) return false;
    FILE *f=fopen(path,"wb");if (!f) return false;
    fprintf(f,"P6\n%u %u\n255\n",s->width,s->height);
    for (unsigned y=0;y<s->height;++y) for (unsigned x=0;x<s->width;++x) {
        const uint16_t p=s->pixels[y*s->stride_pixels+x];
        const unsigned char rgb[3]={(unsigned char)((p>>11)*255/31),
            (unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
        if (fwrite(rgb,3,1,f)!=1U) { fclose(f);return false; }
    }
    return fclose(f)==0;
}
int main(int argc,char **argv) {
    if (argc!=5 && argc!=6) { fprintf(stderr,"usage: %s ART.bin INPUT.txt 320|768 STAGE(0..4) [CAPTURE_PREFIX]\n",argv[0]);return 2; }
    const unsigned width=(unsigned)strtoul(argv[3],NULL,10);
    const unsigned stage=(unsigned)strtoul(argv[4],NULL,10);
    if ((width!=320U && width!=768U)||stage>4U) return 2;
    FILE *f=fopen(argv[1],"rb");if (!f) return 2;
    if (fseek(f,0,SEEK_END)!=0) return 2;
    const long bytes=ftell(f);if (bytes<=0 || bytes>2*1024*1024) return 2;
    rewind(f);uint8_t *art=malloc((size_t)bytes);
    if (!art || fread(art,1,(size_t)bytes,f)!=(size_t)bytes) return 2;
    fclose(f);
    f=fopen(argv[2],"r");if (!f) return 2;
    unsigned count=0;
    while (count<MAX_EVENTS) {
        event_t *e=&events[count];
        const int fields=fscanf(f,"%u %u %d %d",&e->frame,&e->buttons,&e->x,&e->y);
        if (fields==EOF) break;
        if (fields!=4 || e->buttons>255U || e->x < -1 || e->x>319 || e->y < -1 || e->y>199 ||
            (count>0 && e->frame<=events[count-1U].frame)) return 2;
        ++count;
    }
    fclose(f);
    const unsigned height=width==768U?480U:200U,stride=width+7U;
    uint16_t *pixels=malloc((size_t)stride*height*sizeof(*pixels));
    void *state=calloc(1,p4_byte_buddy_game.state_bytes);
    if (!pixels||!state) return 2;
    for (size_t i=0;i<(size_t)stride*height;++i) pixels[i]=0x5aa5;
    const uint16_t care[5]={0,8,28,60,104};
    byte_buddy_save_profile_t profile={.hunger=72,.joy=68,.hygiene=75,.energy=70,
        .coins=40,.care_actions=care[stage],.action_counts={26,26,26,26}};
    uint8_t payload[BYTE_BUDDY_SAVE_PAYLOAD_BYTES];
    if (byte_buddy_save_encode(&profile,payload,sizeof(payload))!=sizeof(payload)) return 2;
    uint32_t sequence=1;
    p4_audio_mixer_t mixer;p4_audio_mixer_init(&mixer);
    const p4_game_services_t services={
        .available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_STORAGE|
            P4_GAME_CAP_SAVE|P4_GAME_CAP_AUDIO_TONE|P4_GAME_CAP_AUDIO_STREAM|
            (width==768U?P4_GAME_CAP_VIDEO_HIGH_RES:0U),
        .game_id=p4_byte_buddy_game.id,.resource_data=art,.resource_bytes=(size_t)bytes,
        .resource_format_version=1,.save_data=payload,.save_bytes=sizeof(payload),
        .save_schema_version=1,.save_sequence=1,.save_context=&sequence,
        .queue_save=queue_save,.read_save_status=save_status,.audio_context=&mixer,
        .play_tone=p4_audio_mixer_service_play_tone,
        .submit_pcm16_stereo=p4_audio_mixer_service_submit_pcm16_stereo,
        .stop_audio=p4_audio_mixer_service_stop};
    p4_game_instance_t game={0};
    if (!p4_game_instance_start(&game,&p4_byte_buddy_game,&services,state,p4_byte_buddy_game.state_bytes)) return 2;
    p4_game_surface_t surface={.pixels=pixels,.stride_pixels=stride,.width=(uint16_t)width,.height=(uint16_t)height};
    event_t current={0,0,-1,-1};unsigned index=0,changed=0;uint32_t previous=0,last=0;
    int16_t samples[267*2];
    for (unsigned frame=0;frame<FRAMES;++frame) {
        if (index<count && events[index].frame==frame) current=events[index++];
        p4_game_input_t input={.held=current.buttons,.pressed=current.buttons&~previous,
            .released=previous&~current.buttons,.touch_valid=true,
            .touch_count=(uint8_t)(current.x>=0&&current.y>=0?1:0)};
        if (input.touch_count) input.touches[0]=(p4_game_point_t){(uint16_t)current.x,(uint16_t)current.y};
        previous=current.buttons;
        const unsigned delta=frame%3U==2U?16U:17U;
        const double begin=now_ms();
        if (p4_game_instance_update(&game,&input,delta)!=P4_GAME_CONTINUE ||
            !p4_audio_mixer_render(&mixer,samples,delta==16U?266U:267U)) return 3;
        const double updated=now_ms();
        if (!p4_game_instance_render(&game,&surface)) return 3;
        const double end=now_ms();
        update_times[frame]=updated-begin;render_times[frame]=end-updated;total_times[frame]=end-begin;
        uint32_t digest=2166136261U;
        for (unsigned y=0;y<height;++y) {
            for (unsigned x=0;x<width;x+=7U) digest=(digest^pixels[y*stride+x])*16777619U;
            for (unsigned x=width;x<stride;++x) if (pixels[y*stride+x]!=0x5aa5) return 4;
        }
        if (frame>0 && digest!=last) ++changed;last=digest;
        if (argc==6 && (frame==200U || frame==500U || frame==1000U || frame==1300U || frame==1600U || frame==1999U))
            if (!capture(argv[5],frame,&surface)) return 4;
    }
    printf("{\"mode\":\"optimized host CPU; real game, resource and tone/PCM mixer; excludes device/display\",\"width\":%u,\"frames\":2000,\"initial_stage\":%u,\"events\":%u,\"changed_frames\":%u,",width,stage,count,changed);
    stats("update_audio",update_times);printf(",");stats("render",render_times);printf(",");stats("total",total_times);puts("}");
    p4_game_instance_stop(&game);free(state);free(pixels);free(art);return 0;
}
