// SPDX-License-Identifier: MIT
// Actual game mixer capture plus timing, lifecycle and signal-level regression.
#include "blast_circuit_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"MUSIC FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
typedef struct {
    uint32_t hash,frames,blocks,peak,max_jump;
    uint64_t energy;
    int previous;
    bool stereo;
    FILE *file;
} capture_t;
static bool capture(void *ctx,const int16_t *data,size_t count)
{
    capture_t *p=ctx;CHECK(count>0U && count<=256U);++p->blocks;
    for(size_t i=0;i<count;++i) {
        const int sample=data[i*2U],magnitude=sample<0?-sample:sample;
        const int jump=sample-p->previous;p->previous=sample;
        if((unsigned)magnitude>p->peak) p->peak=(unsigned)magnitude;
        if((unsigned)(jump<0?-jump:jump)>p->max_jump) p->max_jump=(unsigned)(jump<0?-jump:jump);
        p->energy+=(uint64_t)(sample*sample);
        if(data[i*2U]!=data[i*2U+1U]) p->stereo=true;
        for(unsigned side=0U;side<2U;++side) {
            const uint16_t v=(uint16_t)data[i*2U+side];
            p->hash=(p->hash^v)*16777619U;
            if(p->file) {CHECK(fputc(v&255U,p->file)!=EOF);CHECK(fputc(v>>8U,p->file)!=EOF);}
        }
    }
    p->frames+=(uint32_t)count;return false; // Rejection must not stall music time.
}
static void header(FILE *file,uint32_t frames)
{
    const uint32_t bytes=frames*4U;
    uint8_t h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,2,0,0x80,0x3e,0,0,0,0xfa,0,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    for(unsigned i=0;i<4U;++i) {h[4U+i]=(uint8_t)((bytes+36U)>>(i*8U));h[40U+i]=(uint8_t)(bytes>>(i*8U));}
    CHECK(fwrite(h,1,sizeof(h),file)==sizeof(h));
}
static const unsigned duration_ms[BC_MUSIC_TRACKS]={64000U,81600U,96000U};
static capture_t render(unsigned theme,bool partition,FILE *file,unsigned first,unsigned tracks)
{
    bc_state_t s={0};s.world.phase=BC_PLAY;s.world.theme=(uint8_t)theme;s.audio.music_track=(uint8_t)first;
    capture_t p={.hash=2166136261U,.file=file};
    const p4_game_services_t api={.available_capabilities=P4_GAME_CAP_AUDIO_STREAM,.audio_context=&p,.submit_pcm16_stereo=capture};
    p4_game_context_t c={.state=&s,.state_bytes=sizeof(s),.services=&api};
    uint32_t expected=0U;
    for(unsigned track=0U;track<tracks;++track) {
        const unsigned which=(first+track)%BC_MUSIC_TRACKS;
        for(unsigned i=0;i<duration_ms[which]/100U;++i) {
            if(partition) {bc_audio_update(&c,&s,17U);bc_audio_update(&c,&s,83U);}
            else bc_audio_update(&c,&s,100U);
        }
        expected+=duration_ms[which]*16U;
        CHECK(p.frames==expected && s.audio.music_tick==0U && s.audio.music_frame==0U && s.audio.music_event==0U);
        CHECK(s.audio.music_track==(which+1U)%BC_MUSIC_TRACKS && p.previous==0);
        for(unsigned v=0;v<BC_MUSIC_VOICES;++v) CHECK(!s.audio.music[v].frames);
        for(unsigned i=0;i<BC_MUSIC_DELAY;++i) CHECK(s.audio.delay[i]==0);
    }
    CHECK(p.peak>3000U && p.peak<24000U && p.stereo);
    CHECK(p.energy/p.frames>300000U && p.energy/p.frames<25000000U);
    // Every boundary advances to another piece, without restarting on a round.
    p.file=NULL;bc_audio_update(&c,&s,100U);
    CHECK(s.audio.music_tick>=2U);
    const unsigned playing=s.audio.music_track;
    s.world.round=2U;s.world.phase=BC_READY;bc_audio_update(&c,&s,100U);
    CHECK(s.audio.music_track==playing && s.audio.music_tick>=4U);
    s.world.phase=BC_PAUSED;bc_audio_update(&c,&s,100U);
    CHECK(s.audio.music_gain==0);
    p.peak=0U;bc_audio_update(&c,&s,100U);CHECK(p.peak==0U);
    const uint32_t before=p.frames;s.audio.muted=true;bc_cue(&s,BC_S_BLAST);
    bc_audio_update(&c,&s,100U);CHECK(p.frames==before && s.cues==0U);
    for(unsigned i=0;i<BC_SOUNDS;++i) CHECK(s.audio.effect_left[i]==0U);
    s.audio.muted=false;s.world.phase=BC_PLAY;p.peak=0U;
    bc_audio_update(&c,&s,100U);CHECK(p.peak>100U && s.audio.music_gain==128);
    return p;
}
int main(int argc,char **argv)
{
    if(argc==3) {
        unsigned start=0U,tracks=BC_MUSIC_TRACKS;
        if(strcmp(argv[1],"all")!=0) {
            if(strlen(argv[1])!=1U || argv[1][0]<'0' || argv[1][0]>'2') return EXIT_FAILURE;
            start=(unsigned)(argv[1][0]-'0');tracks=1U;
        }
        FILE *file=fopen(argv[2],"wb");if(!file) {perror(argv[2]);return EXIT_FAILURE;}
        uint32_t frames=0U;for(unsigned t=0U;t<tracks;++t) frames+=duration_ms[(start+t)%BC_MUSIC_TRACKS]*16U;
        header(file,frames);(void)render(0U,false,file,start,tracks);CHECK(fclose(file)==0);
    } else if(argc!=1) return EXIT_FAILURE;
    const capture_t a=render(0U,false,NULL,0U,BC_MUSIC_TRACKS);
    const capture_t b=render(0U,true,NULL,0U,BC_MUSIC_TRACKS);
    CHECK(a.hash==b.hash && a.energy==b.energy && a.frames==b.frames);
    const capture_t prism=render(2U,false,NULL,0U,BC_MUSIC_TRACKS);CHECK(prism.hash!=a.hash);
    // Separate renders keep bounds meaningful for each new arrangement.
    (void)render(0U,false,NULL,1U,1U);(void)render(0U,false,NULL,2U,1U);
    printf("Music: three-track 241.6-second rotation, all boundaries to zero, callback partition invariance, stereo, FIFO rejection, round continuity, pause/mute/resume; state %zu bytes; %u failures\n",sizeof(bc_state_t),failures);
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
