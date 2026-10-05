// SPDX-License-Identifier: MIT
#include "platform_display_layout.h"
#include "platform/board.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

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
            expected=logical_x<64 || logical_x>=1216 ? 0 :
                src[(x*height/720)*(width+3)+(logical_x-64)*width/1152];
        }
        assert(dest[y*stride+x]==expected);
    }
    free(dest);free(src);
}
int main(void)
{
    assert(PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES==90);
    check_surface(320,200,platform_display_layout_rgb565_320x200);
    check_surface(384,240,platform_display_layout_rgb565_384x240);
    check_surface(768,480,platform_display_layout_rgb565_768x480);
    puts("TAB5 DISPLAY PASS native=720x1280 viewport=1152x720+64+0 formats=3 padding=preserved");
    return 0;
}
