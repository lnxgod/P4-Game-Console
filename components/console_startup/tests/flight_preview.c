// SPDX-License-Identifier: MIT
#include "console/startup.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static void write_ppm(const char *path,const uint16_t *p,size_t stride,size_t w,size_t h)
{
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%zu %zu\n255\n",w,h);
    for(size_t y=0;y<h;++y)for(size_t x=0;x<w;++x){uint16_t v=p[y*stride+x];
        fputc(((v>>11)&31)*255/31,f);fputc(((v>>5)&63)*255/63,f);fputc((v&31)*255/31,f);}
    fclose(f);
}
static void check_size(size_t w,size_t h,const uint8_t *logo,const char *prefix)
{
    size_t stride=w+7;uint16_t *p=malloc((h+1)*stride*sizeof(*p)),*ref=malloc((h+1)*stride*sizeof(*ref));assert(p&&ref);
    for(size_t i=0;i<(h+1)*stride;++i)p[i]=ref[i]=0xdead;
    assert(!console_startup_render_flight(p,w-1,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,0,"Starting",false,true));
    assert(!console_startup_render_flight(p,SIZE_MAX,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,0,"Starting",false,true));
    assert(!console_startup_render_flight(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES-1,0,"Starting",false,true));
    const uint32_t times[]={0,40,80,120,160,200,240,320,400,480,560,640,720,800,880,960,
        1040,1120,1200,1280,1360,1440,1500,1580,1980,2500,3500,5000,6200,UINT32_MAX};
    for(size_t i=0;i<sizeof(times)/sizeof(times[0]);++i){
        const char *label=i%2?"Loading your games":
            "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
        if(i)memcpy(ref,p,(h+1)*stride*sizeof(*p));
        assert(console_startup_render_flight(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,times[i],label,false,i==0));
        /* Verify the advertised native display damage band contains every
         * changed pixel, including long-to-short status transitions. */
        if(i)for(size_t y=0;y<h;++y)for(size_t x=0;x<w;++x)
            if(x<256U*w/1280U||x>=1024U*w/1280U)
                assert(p[y*stride+x]==ref[y*stride+x]);
        assert(console_startup_render_flight(ref,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,times[i],label,false,true));
        assert(memcmp(p,ref,(h+1)*stride*sizeof(*p))==0);
        for(size_t y=0;y<h;++y)for(size_t x=w;x<stride;++x)assert(p[y*stride+x]==0xdead);
        for(size_t x=0;x<stride;++x)assert(p[h*stride+x]==0xdead);
    }
    assert(console_startup_render_flight(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,UINT32_MAX,NULL,true,false));
    assert(console_startup_render_flight(ref,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,0,NULL,true,true));
    assert(memcmp(p,ref,(h+1)*stride*sizeof(*p))==0);
    if(prefix){
        const clock_t start=clock();
        for(unsigned i=0;i<70U;++i){
            const bool done=i>=60U;
            assert(console_startup_render_flight(p,stride,w,h,logo,CONSOLE_STARTUP_LOGO_BYTES,i*60U,done?"Ready to play":"Loading your games",done,i==0));
            char file[1024];(void)snprintf(file,sizeof(file),"%s-%03u.ppm",prefix,i);
            write_ppm(file,p,stride,w,h);
        }
        printf("Flight host: 70 native frames including file output in %.3f seconds; not hardware FPS.\n",(double)(clock()-start)/CLOCKS_PER_SEC);
    }
    free(p);free(ref);
}
int main(int argc,char **argv)
{
    assert(argc==2||argc==3);uint8_t *logo=malloc(CONSOLE_STARTUP_LOGO_BYTES);assert(logo);
    FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(logo,1,CONSOLE_STARTUP_LOGO_BYTES,f)==CONSOLE_STARTUP_LOGO_BYTES);assert(fgetc(f)==EOF);fclose(f);
    check_size(768,480,logo,NULL);check_size(1152,720,logo,NULL);check_size(1280,720,logo,argc==3?argv[2]:NULL);
    free(logo);puts("Flight PASS: time-based poses, incremental/full equality, fixed status, completed pose independent of time, malformed buffers and pixel guards.");
    return 0;
}
