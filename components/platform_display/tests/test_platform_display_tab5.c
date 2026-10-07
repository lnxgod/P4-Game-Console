// SPDX-License-Identifier: MIT
#include "platform_display_layout.h"
#include "platform/board.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef bool (*scale_fn)(const uint16_t *,size_t,uint16_t *,size_t,size_t);
static void check_surface(size_t width,size_t height,scale_fn scale)
{
    const size_t stride=727,rows=1283;
    uint16_t *src=malloc((width+3)*height*sizeof(*src));
    uint16_t *dest=malloc(stride*rows*sizeof(*dest));
    assert(src && dest);
    for (size_t y=0;y<height;++y) for (size_t x=0;x<width+3;++x)
        src[y*(width+3)+x]=(uint16_t)(x*31U+y*257U);
    for (size_t i=0;i<stride*rows;++i) dest[i]=0xdead;
    assert(!scale(NULL,width+3,dest,stride,rows));
    assert(!scale(src,width-1,dest,stride,rows));
    assert(!scale(src,width+3,dest,719,rows));
    assert(!scale(src,width+3,dest,stride,1279));
    assert(scale(src,width+3,dest,stride,rows));
    for (size_t y=0;y<rows;++y) for (size_t x=0;x<stride;++x) {
        uint16_t expected=0xdead;
        if (y<1280 && x<720) {
            /* Physical (x,y) corresponds to landscape (1279-y,x). */
            size_t logical_x=1279-y;
            expected=width==1280U ? src[x*(width+3)+logical_x] : logical_x<64 || logical_x>=1216 ? 0 :
                src[(x*height/720)*(width+3)+(logical_x-64)*width/1152];
        }
        assert(dest[y*stride+x]==expected);
    }
    free(dest);free(src);
}
static void check_damage_replay(void)
{
    uint16_t *source=calloc(1280U*720U,sizeof(*source));
    uint16_t *frames[2]={calloc(720U*1280U,sizeof(*source)),calloc(720U*1280U,sizeof(*source))};
    assert(source&&frames[0]&&frames[1]);
    platform_display_rgb565_region_t previous={0,0,1280,720},r,mapped;
    const platform_display_rgb565_region_t bad[]={{0,0,0,1},{1279,0,2,1},{0,719,1,2},{65535,0,1,1}};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i)assert(!platform_display_layout_tab5_damage(&bad[i],NULL,&r,&mapped));
    for(unsigned frame=0;frame<20;++frame){
        platform_display_rgb565_region_t current=frame%5U?(platform_display_rgb565_region_t){(uint16_t)(frame*31U),(uint16_t)(frame*9U),96,127}:(platform_display_rgb565_region_t){0,0,1280,720};
        if(frame==3)current=(platform_display_rgb565_region_t){0,0,1,720};
        for(unsigned y=current.y;y<(unsigned)current.y+current.height;++y)for(unsigned x=current.x;x<(unsigned)current.x+current.width;++x)
            source[(size_t)y*1280+x]=(uint16_t)(frame*100U+x+y);
        assert(platform_display_layout_tab5_damage(&current,frame?&previous:NULL,&r,&mapped));
        assert((unsigned)mapped.y+mapped.height<=1280U); /* pinned rotated PPA cache window */
        for(unsigned y=mapped.y;y<(unsigned)mapped.y+mapped.height;++y)for(unsigned x=mapped.x;x<(unsigned)mapped.x+mapped.width;++x)
            frames[frame%2U][(size_t)y*720U+x]=source[(size_t)x*1280U+1279U-y];
        for(unsigned y=0;y<1280;++y)for(unsigned x=0;x<720;++x)
            assert(frames[frame%2U][(size_t)y*720U+x]==source[(size_t)x*1280U+1279U-y]);
        previous=current;
    }
    free(source);free(frames[0]);free(frames[1]);
    puts("TAB5 DAMAGE PASS alternating_buffers=2 complete_frame_equivalence=1 invalid_bounds_rejected=1");
}
static void check_exact_edge_damage(void)
{
    const platform_display_rgb565_region_t edges[]={
        {0,0,1,720},{1279,0,1,720},{0,0,1280,1},{0,719,1280,1},
        {0,719,1,1},{1279,719,1,1},
    };
    for(size_t i=0;i<sizeof(edges)/sizeof(edges[0]);++i){
        platform_display_rgb565_region_t source,mapped;
        assert(platform_display_layout_tab5_damage(&edges[i],&edges[i],&source,&mapped));
        assert(memcmp(&source,&edges[i],sizeof(source))==0);
        assert(mapped.width==source.height&&mapped.height==source.width);
        assert((unsigned)mapped.x+mapped.width<=720U);
        assert((unsigned)mapped.y+mapped.height<=1280U);
        /* IDF invalidates new_block_h == rotated source width, not height. */
        const size_t cache_start=(size_t)mapped.y*720U*sizeof(uint16_t);
        const size_t cache_end=cache_start+(size_t)mapped.height*720U*sizeof(uint16_t);
        assert(((cache_end+63U)&~(size_t)63U)<=720U*1280U*sizeof(uint16_t));
    }
}
int main(void)
{
    check_exact_edge_damage();
    check_damage_replay();
    assert(PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES==90);
    check_surface(320,200,platform_display_layout_rgb565_320x200);
    check_surface(384,240,platform_display_layout_rgb565_384x240);
    check_surface(768,480,platform_display_layout_rgb565_768x480);
    check_surface(1152,720,platform_display_layout_rgb565_1152x720);
    check_surface(1280,720,platform_display_layout_rgb565_1280x720);
    puts("TAB5 DISPLAY PASS native=720x1280 viewport=1152x720+64+0 native_ui=1280x720 formats=5 padding=preserved");
    return 0;
}
