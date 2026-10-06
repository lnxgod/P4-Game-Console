// SPDX-License-Identifier: MIT
#include "console/startup.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void le(FILE *f, uint32_t v, unsigned bytes)
{ for (unsigned i=0;i<bytes;++i) fputc((int)((v>>(8U*i))&255U),f); }
static void check_progress(uint16_t *p,size_t stride,size_t w,size_t h,const uint8_t *logo)
{
    const size_t y=461U*h/480U,left=28U*w/768U,right=740U*w/768U-1U;
    assert(console_startup_render(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,0,"Loading games...",true));
    const uint16_t cyan=p[y*stride+left],track=p[y*stride+right];assert(cyan!=track);
    assert(console_startup_render(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,31,"Loading games...",false));
    assert(p[y*stride+left]==track&&p[y*stride+right]==cyan);
    assert(console_startup_render(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,62,"Loading games...",false));
    assert(p[y*stride+left]==cyan&&p[y*stride+right]==track);
    assert(console_startup_render(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,CONSOLE_STARTUP_COMPLETE,"Ready",false));
    for(size_t x=left;x<=right;++x)assert(p[y*stride+x]==cyan);
    assert(console_startup_render(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,UINT_MAX-1U,"Loading games...",false));
}
int main(int argc,char **argv)
{
    assert(argc==4);
    uint8_t logo[CONSOLE_STARTUP_LOGO_BYTES];
    FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(logo,1,sizeof(logo),f)==sizeof(logo)); assert(fgetc(f)==EOF); fclose(f);
    const size_t w=1280,h=720,stride=w+7;
    uint16_t *p=malloc((h+1)*stride*sizeof(*p)); assert(p);
    for(size_t i=0;i<(h+1)*stride;++i) p[i]=0xdead;
    assert(!console_startup_render(p,stride,w,h,logo,sizeof(logo)-1,0,"Starting...",true));
    assert(console_startup_render(p,stride,w,h,logo,sizeof(logo),3,"Starting GameChangersAI OS...",true));
    for(size_t y=0;y<h;++y) for(size_t x=w;x<stride;++x) assert(p[y*stride+x]==0xdead);
    for(size_t x=0;x<stride;++x) assert(p[h*stride+x]==0xdead);
    uint16_t *copy=malloc(h*stride*sizeof(*p)); assert(copy); memcpy(copy,p,h*stride*sizeof(*p));
    assert(console_startup_render(p,stride,w,h,logo,sizeof(logo),4,"Starting GameChangersAI OS...",false));
    assert(memcmp(copy,p,408*h/480*stride*sizeof(*p))==0); free(copy);
    check_progress(p,stride,768,480,logo);check_progress(p,stride,1152,720,logo);check_progress(p,stride,w,h,logo);
    assert(console_startup_render(p,stride,w,h,logo,sizeof(logo),CONSOLE_STARTUP_COMPLETE,"Ready",false));
    f=fopen(argv[2],"wb"); assert(f); fprintf(f,"P6\n%zu %zu\n255\n",w,h);
    for(size_t y=0;y<h;++y) for(size_t x=0;x<w;++x) {
        uint16_t v=p[y*stride+x]; fputc(((v>>11)&31)*255/31,f);fputc(((v>>5)&63)*255/63,f);fputc((v&31)*255/31,f);
    } fclose(f);free(p);
    size_t frames=console_startup_audio_frames();
    /* Startup cue must stay within the one-second latency budget. */
    assert(frames > 0U && frames <= CONSOLE_STARTUP_SAMPLE_RATE);
    int16_t *pcm=malloc(frames*4);assert(pcm);
    assert(!console_startup_audio_render(pcm,frames,1));
    assert(console_startup_audio_render(pcm,0,frames));
    assert(pcm[0]==0 && pcm[(frames-1U)*2U]==0);
    for(size_t n=0;n<frames;n+=128U) {
        const char *status=console_startup_audio_status(n);
        assert(status && !strstr(status,"Dial") && !strstr(status,"ATDT"));
    }
    int16_t chunk[267*2];
    for(size_t n=0;n<frames;n+=267) {
        size_t count=frames-n<267?frames-n:267;
        assert(console_startup_audio_render(chunk,n,count));
        assert(memcmp(chunk,pcm+n*2,count*4)==0);
    }
    int peak=0; long long sum=0;
    for(size_t n=0;n<frames;++n) { int v=pcm[n*2]; assert(v==pcm[n*2+1]);assert(v>=-12000 && v<=12000);if(abs(v)>peak)peak=abs(v);sum+=v; }
    assert(peak>4000 && llabs(sum/(long long)frames)<100);
    f=fopen(argv[3],"wb");assert(f);fwrite("RIFF",1,4,f);le(f,(uint32_t)(36+frames*4),4);fwrite("WAVEfmt ",1,8,f);
    le(f,16,4);le(f,1,2);le(f,2,2);le(f,16000,4);le(f,64000,4);le(f,4,2);le(f,16,2);
    fwrite("data",1,4,f);le(f,(uint32_t)(frames*4),4);
    for(size_t n=0;n<frames*2;++n)le(f,(uint16_t)pcm[n],2);
    fclose(f);free(pcm);printf("Startup PASS: native pixels, logo bounds, padding, incremental repaint, full-width marquee and completion; fanfare %.2fs peak=%d chunk-stable stereo\n",(double)frames/16000,peak);
    return 0;
}
