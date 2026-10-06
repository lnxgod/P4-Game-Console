// SPDX-License-Identifier: MIT
#define MAZE_BACKDROP_TEST 1
#include "../src/maze_chase.c"
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    for(unsigned mode=0;mode<2U;++mode){
        const uint16_t w=mode?768U:320U,h=mode?480U:200U;
        const size_t stride=(size_t)w+7U,words=stride*h+32U;
        uint16_t *a=malloc(words*2U),*b=malloc(words*2U);if(!a||!b)return 1;
        p4_game_surface_t ref={.pixels=a+16,.width=w,.height=h,.stride_pixels=stride};
        p4_game_surface_t fast=ref;fast.pixels=b+16;
        for(unsigned clear=0;clear<2U;++clear){
            for(size_t i=0;i<words;++i)a[i]=b[i]=0x5aa5U;
            draw_backdrop_reference(&ref,clear!=0U);draw_backdrop(&fast,clear!=0U);
            if(memcmp(a,b,words*2U)){fprintf(stderr,"Backdrop mismatch %u clear=%u\n",w,clear);return 1;}
        }
        /* At 60 Hz every actor advances within a cell, in native pixels. */
        int last=visual_axis(&fast,MAZE_ORIGIN_X,12,11,0,112,false);
        for(unsigned t=16;t<=112;t+=16){
            const int now=visual_axis(&fast,MAZE_ORIGIN_X,12,11,t,112,false);
            if(now>=last||last-now>(mode?5:2))return 1;
            last=now;
        }
        last=visual_axis(&fast,MAZE_ORIGIN_Y,6,7,0,176,true);
        for(unsigned t=16;t<=176;t+=16){
            const int now=visual_axis(&fast,MAZE_ORIGIN_Y,6,7,t,176,true);
            if(now<=last||now-last>(mode?3:1))return 1;
            last=now;
        }
        /* Unwrapped tunnel positions progress each frame, including before arrival. */
        last=visual_axis(&fast,MAZE_ORIGIN_X,0,24,0,112,false);
        for(unsigned t=16;t<=112;t+=16){
            const int now=visual_axis(&fast,MAZE_ORIGIN_X,0,24,t,112,false);
            if(now>=last||last-now>(mode?5:2))return 1;
            last=now;
        }
        /* A split actor appears only at the two lips, never across the arena. */
        for(size_t i=0;i<words;++i)b[i]=0x5aa5U;
        const int seam=visual_axis(&fast,MAZE_ORIGIN_X,0,24,56,112,false);
        actor_sprite(&fast,seam,p4_ui_y(&fast,90),0,true);
        unsigned left_count=0,right_count=0;
        for(unsigned yy=0;yy<h;++yy)for(unsigned xx=0;xx<stride;++xx){
            if(fast.pixels[(size_t)yy*stride+xx]==0x5aa5U)continue;
            if(xx>=(unsigned)p4_ui_x(&fast,22)&&xx<(unsigned)p4_ui_x(&fast,34))++left_count;
            else if(xx>=(unsigned)p4_ui_x(&fast,285)&&xx<(unsigned)p4_ui_x(&fast,297))++right_count;
            else return 1;
        }
        if(!left_count||!right_count)return 1;
        free(a);free(b);
    }
    puts("Exact scenery pixels, row guards and sub-tile actor steps passed");return 0;
}
