#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doom_touch/input.h"

enum { WIDTH=768, HEIGHT=480, STRIDE=775, GUARD=17 };
static uint32_t src[GUARD+STRIDE*HEIGHT+GUARD];
static uint32_t full[GUARD+STRIDE*HEIGHT+GUARD];
static uint32_t rows[GUARD+STRIDE*HEIGHT+GUARD];
static const uint32_t sentinel=UINT32_C(0xdeadbeef);
static uint32_t background(size_t x,size_t y)
{ return (uint32_t)(((x*101U+y*353U)^UINT32_C(0x724369))&UINT32_C(0xffffff)); }
static void init(void)
{
    for (size_t i=0;i<sizeof(src)/sizeof(src[0]);++i) src[i]=full[i]=rows[i]=sentinel;
    for(size_t y=0;y<HEIGHT;++y) for(size_t x=0;x<WIDTH;++x)
        src[GUARD+y*STRIDE+x]=background(x,y);
}
static void guards(const uint32_t *buffer)
{
    for(size_t i=0;i<GUARD;++i) assert(buffer[i]==sentinel && buffer[GUARD+STRIDE*HEIGHT+i]==sentinel);
    for(size_t y=0;y<HEIGHT;++y) for(size_t x=WIDTH;x<STRIDE;++x)
        assert(buffer[GUARD+y*STRIDE+x]==sentinel);
}
int main(void)
{
    const uint32_t all=(UINT32_C(1)<<DOOM_TOUCH_ACTION_COUNT)-1U;
    for(unsigned scenario=0;scenario<DOOM_TOUCH_ACTION_COUNT+2U;++scenario) {
        const uint32_t actions=scenario==0?0U:scenario==1?all:UINT32_C(1)<<(scenario-2U);
        init();
        assert(doom_touch_overlay_render_xrgb8888_sized(src+GUARD,STRIDE,full+GUARD,STRIDE,WIDTH,HEIGHT,actions));
        memcpy(rows,src,sizeof(src));
        for(size_t y=0;y<HEIGHT;++y) {
            assert(doom_touch_overlay_render_row_xrgb8888_sized(rows+GUARD+y*STRIDE,STRIDE,y,WIDTH,HEIGHT,actions));
            if(!doom_touch_overlay_row_may_draw_sized(y,WIDTH,HEIGHT))
                assert(memcmp(rows+GUARD+y*STRIDE,src+GUARD+y*STRIDE,WIDTH*sizeof(uint32_t))==0);
        }
        assert(memcmp(full,rows,sizeof(full))==0);
        guards(full); guards(rows); guards(src);
        /* Independent legacy geometry oracle with this actual native pixel's
         * background: adjacent native pixels must blend their own scene data. */
        uint32_t canonical[320];
        for(size_t sample=0;sample<2048U;++sample) {
            const size_t x=(sample*379U)%WIDTH, y=(sample*173U+sample/HEIGHT)%HEIGHT;
            for(size_t i=0;i<320;++i) canonical[i]=background(x,y);
            assert(doom_touch_overlay_render_row_xrgb8888(canonical,320,y*200U/HEIGHT,actions));
            assert(full[GUARD+y*STRIDE+x]==canonical[x*320U/WIDTH]);
        }
        /* Canonical API remains pixel-identical for legacy board recovery. */
        uint32_t legacy[320], sized[320];
        for(size_t y=0;y<200;++y) {
            for(size_t x=0;x<320;++x) legacy[x]=sized[x]=background(x,y);
            assert(doom_touch_overlay_render_row_xrgb8888(legacy,320,y,actions));
            assert(doom_touch_overlay_render_row_xrgb8888_sized(sized,320,y,320,200,actions));
            assert(memcmp(legacy,sized,sizeof(legacy))==0);
        }
    }
    init();
    assert(!doom_touch_overlay_render_xrgb8888_sized(src+GUARD,STRIDE,full+GUARD,STRIDE,769,480,0));
    assert(!doom_touch_overlay_render_xrgb8888_sized(src+GUARD,STRIDE,full+GUARD,STRIDE,768,481,0));
    assert(!doom_touch_overlay_render_xrgb8888_sized(src+GUARD,STRIDE,full+GUARD,STRIDE,0,0,0));
    assert(!doom_touch_overlay_render_xrgb8888_sized(src+GUARD,SIZE_MAX,full+GUARD,STRIDE,768,480,0));
    assert(!doom_touch_overlay_render_xrgb8888_sized(src+GUARD,STRIDE,src+GUARD+1,STRIDE,768,480,0));
    assert(!doom_touch_overlay_render_xrgb8888_sized(src+GUARD,STRIDE,full+GUARD,STRIDE,768,480,UINT32_MAX));
    assert(!doom_touch_overlay_render_row_xrgb8888_sized(full+GUARD,STRIDE,480,768,480,0));
    assert(!doom_touch_overlay_render_row_xrgb8888_sized(full+GUARD,767,0,768,480,0));
    assert(!doom_touch_overlay_render_row_xrgb8888_sized((uint32_t*)((uint8_t*)full+1),STRIDE,0,768,480,0));
    assert(!doom_touch_overlay_render_row_xrgb8888_sized(full+GUARD,SIZE_MAX,0,768,480,0));
    for(size_t i=0;i<sizeof(full)/sizeof(full[0]);++i) assert(full[i]==sentinel);
    puts("native touch overlay: 16 masks, padded guards, pixel-local blends, canonical parity and invalid bounds passed");
    return 0;
}
