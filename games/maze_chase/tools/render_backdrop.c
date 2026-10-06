// SPDX-License-Identifier: MIT
// Export the authoritative procedural scenery, never a scaled screenshot.
#define MAZE_BACKDROP_GENERATOR 1
#include "../src/maze_chase.c"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    for(unsigned mode=0;mode<2U;++mode){
        const uint16_t w=mode?768U:320U,h=mode?480U:200U;
        uint16_t *pixels=calloc((size_t)w*h,sizeof(*pixels));if(!pixels)return 1;
        p4_game_surface_t s={.pixels=pixels,.width=w,.height=h,.stride_pixels=w};
        for(unsigned clear=0;clear<2U;++clear){
            draw_backdrop_reference(&s,clear!=0U);
            char name[512];snprintf(name,sizeof(name),"%s-%u-%u.rgb565",argv[1],w,clear);
            FILE *f=fopen(name,"wb");if(!f){free(pixels);return 1;}
            for(size_t i=0;i<(size_t)w*h;++i){
                fputc(pixels[i]&255U,f);fputc(pixels[i]>>8U,f);
            }
            if(fclose(f)){free(pixels);return 1;}
        }
        free(pixels);
    }
    return 0;
}
