// SPDX-License-Identifier: MIT
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p4/card_art.h"
#include "p4/visual.h"
#include "raster_reference.h"

static uint32_t rng = 0x8316abcdU;
static uint32_t random_u32(void)
{
    rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
    return rng;
}
static size_t comparisons;
static void same(const uint16_t *actual, const uint16_t *expected, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "raster comparison %zu mismatch at %zu: %04x != %04x\n",
                comparisons, i, actual[i], expected[i]);
            abort();
        }
    }
    ++comparisons;
}
static void reset(uint16_t *actual, uint16_t *expected, size_t count)
{
    for (size_t i = 0; i < count; ++i) actual[i] = (uint16_t)random_u32();
    memcpy(expected, actual, count * sizeof(*actual));
}
#define CHECK_CALL(new_call, old_call) do { \
    reset(a, b, count); new_call; old_call; same(a, b, count); \
} while (0)

static void verify_blend(void)
{
    /* Exhaust all component input pairs and every coverage value, including
     * the 952 maximum rounded numerator for 6-bit green. */
    for (unsigned a = 0; a <= 16U; ++a)
        for (unsigned back = 0; back < 64U; ++back)
            for (unsigned front = 0; front < 64U; ++front) {
                const uint16_t b = (uint16_t)(((back & 31U) << 11U) | (back << 5U) | (back & 31U));
                const uint16_t f = (uint16_t)(((front & 31U) << 11U) | (front << 5U) | (front & 31U));
                assert(p4_ui_blend(b, f, a) == reference_p4_ui_blend(b, f, a));
            }
    assert(p4_ui_blend(0x1234U, 0xabcdU, UINT_MAX) == 0xabcdU);
    for (unsigned n = 0; n <= 952U; ++n) assert(((n * 2185U) >> 15U) == n / 15U);
}

static void verify(unsigned width, unsigned height)
{
    enum { GUARD = 19, SOURCE_SIDE = 128 };
    const size_t stride = (size_t)width + 7U;
    const size_t count = stride * height + GUARD * 2U;
    uint16_t *a = malloc(count * sizeof(*a)), *b = malloc(count * sizeof(*b));
    uint16_t *source = malloc(SOURCE_SIDE * SOURCE_SIDE * sizeof(*source));
    assert(a && b && source);
    for (size_t i = 0; i < SOURCE_SIDE * SOURCE_SIDE; ++i)
        source[i] = i % 5U == 0U ? 0xf81fU : (uint16_t)random_u32();
    p4_game_surface_t actual = {.pixels = a + GUARD, .width = (uint16_t)width,
        .height = (uint16_t)height, .stride_pixels = stride};
    p4_game_surface_t expected = actual; expected.pixels = b + GUARD;
    const int positions[][2] = {{-19,-17},{0,0},{37,43},{(int)width-5,(int)height-3},
        {-(int)width,-(int)height},{(int)width+1,(int)height+1},{-8192,8192}};
    for (unsigned h = 0; h <= 129U; ++h) {
        const int x = positions[h % 7U][0], y = positions[h % 7U][1];
        char text[10] = "Rank Ag9 "; text[8] = (char)(32U + h % 95U);
        CHECK_CALL(p4_ui_text(&actual,x,y,text,0xbad1U,h,9U),
            reference_p4_ui_text(&expected,x,y,text,0xbad1U,h,9U));
    }
    for (unsigned ch = 0; ch <= 255U; ++ch) {
        const char text[4] = {(char)ch,'W','g','\0'};
        CHECK_CALL(p4_ui_text(&actual,11,9,text,(uint16_t)random_u32(),28U,0U),
            reference_p4_ui_text(&expected,11,9,text,0,28U,0U));
        CHECK_CALL(p4_ui_text(&actual,11,9,text,0xc35aU,38U,3U),
            reference_p4_ui_text(&expected,11,9,text,0xc35aU,38U,3U));
    }
    char long_text[300]; memset(long_text,'W',sizeof(long_text)); long_text[299]='\0';
    CHECK_CALL(p4_ui_text(&actual,-800,0,long_text,0xffffU,UINT_MAX,SIZE_MAX),
        reference_p4_ui_text(&expected,-800,0,long_text,0xffffU,UINT_MAX,SIZE_MAX));
    const int sizes[] = {1,2,3,7,16,31,96,127,129,511,4096};
    for (size_t c = 0; c < 90U; ++c) {
        const int x=positions[c%7U][0], y=positions[c%7U][1];
        const int w=sizes[c%11U], h=sizes[(c*3U+1U)%11U], r=(int)(c%5U)*23-1;
        CHECK_CALL(p4_ui_round_rect(&actual,x,y,w,h,r,0x1badU),
            reference_p4_ui_round_rect(&expected,x,y,w,h,r,0x1badU));
        const int sw=1+(int)(c*13U%127U), sh=1+(int)(c*17U%127U);
        CHECK_CALL(p4_ui_sprite(&actual,x,y,w,h,source,sw,sh,(c&1U)!=0U,0xf81fU),
            reference_p4_ui_sprite(&expected,x,y,w,h,source,sw,sh,(c&1U)!=0U,0xf81fU));
        CHECK_CALL(p4_draw_sprite_rgb565(&actual,x,y,source,(size_t)sw,(size_t)sh,128U,(c&1U)!=0U,0xf81fU),
            reference_p4_draw_sprite_rgb565(&expected,x,y,source,(size_t)sw,(size_t)sh,128U,(c&1U)!=0U,0xf81fU));
        const int size=3+(int)(c*19U%510U); const unsigned suit=(unsigned)(c%5U);
        /* Diamond was intentionally redrawn as a pointed rhombus. Only
         * unchanged suits participate in the historical pixel oracle. */
        if(suit!=1U){
            CHECK_CALL(p4_card_suit(&actual,x,y,size,suit,0x9834U),
                reference_p4_card_suit(&expected,x,y,size,suit,0x9834U));
        }
    }
    for (unsigned scale=1; scale<=8U; ++scale)
      for (unsigned flip=0; flip<4U; ++flip)
       for (size_t pos=0; pos<7U; ++pos) {
        const int x=positions[pos][0],y=positions[pos][1];
        p4_sprite_t sprite={.pixels=source,.sheet_width=128U,.sheet_height=128U,
            .stride_pixels=128U,.source_x=7U,.source_y=9U,.width=53U,.height=61U,
            .transparent_color=0xf81fU,.scale=(uint8_t)scale,.flip=(uint8_t)flip,
            .use_transparency=(pos&1U)!=0U};
        CHECK_CALL(p4_draw_sprite(&actual,x,y,&sprite),
            reference_p4_draw_sprite(&expected,x,y,&sprite));
    }
    const int radii[]={0,1,2,3,7,16,35,127,511,1024};
    for (size_t c=0;c<30U;++c) {
        const int x=positions[c%7U][0],y=positions[c%7U][1],r=radii[c%10U];
        CHECK_CALL(p4_draw_fill_circle(&actual,x,y,r,0x50a3U),
            reference_p4_draw_fill_circle(&expected,x,y,r,0x50a3U));
    }
    /* Forward overlap and flips/scaled overlap retain original read order. */
    for (unsigned mode=0;mode<2U;++mode) {
        reset(a,b,count);
        p4_draw_sprite_rgb565(&actual,1,1,actual.pixels,51U,17U,stride,mode!=0U,0xf81fU);
        reference_p4_draw_sprite_rgb565(&expected,1,1,expected.pixels,51U,17U,stride,mode!=0U,0xf81fU);
        same(a,b,count);
        reset(a,b,count);
        p4_ui_sprite(&actual,1,1,60,50,actual.pixels,23,17,mode!=0U,0xf81fU);
        reference_p4_ui_sprite(&expected,1,1,60,50,expected.pixels,23,17,mode!=0U,0xf81fU);
        same(a,b,count);
        reset(a,b,count);
        p4_sprite_t sa={.pixels=actual.pixels,.sheet_width=width,.sheet_height=height,
          .stride_pixels=stride,.width=31,.height=23,.scale=2,.flip=(uint8_t)mode,
          .use_transparency=mode!=0U,.transparent_color=0xf81fU};
        p4_sprite_t sb=sa;sb.pixels=expected.pixels;
        p4_draw_sprite(&actual,-3,1,&sa);reference_p4_draw_sprite(&expected,-3,1,&sb);
        same(a,b,count);
    }
    /* The new material accepts preexpanded rows; oracle uses the original
     * compact square and direct per-pixel reflection for all sizes 1..128. */
    uint16_t *expanded=malloc(128U*256U*sizeof(*expanded));assert(expanded);
    for (int size=1;size<=128;++size) {
        for(int y=0;y<size;++y)for(int x=0;x<size;++x){
            expanded[y*size*2+x]=source[y*size+x];
            expanded[y*size*2+size*2-x-1]=source[y*size+x];
        }
        const int x=positions[(unsigned)size%7U][0],y=positions[(unsigned)size%7U][1];
        reset(a,b,count);
        p4_ui_material_mirrored_rows(&actual,x,y,4096,4096,expanded,size);
        for(int py=0;py<(int)height;++py)for(int px=0;px<(int)width;++px){
            if(px<x||py<y||px>=x+4096||py>=y+4096)continue;
            int sy=py%(size*2),sx=px%(size*2);
            if(sy>=size)sy=size*2-sy-1;if(sx>=size)sx=size*2-sx-1;
            expected.pixels[(size_t)py*stride+(size_t)px]=source[sy*size+sx];
        }
        same(a,b,count);
    }
    /* Extreme/invalid input must be a no-op and keep odd-stride guards. */
    reset(a,b,count);
    p4_draw_fill_circle(&actual,INT_MIN,INT_MAX,1024,0);
    p4_draw_sprite_rgb565(&actual,INT_MAX,INT_MIN,source,128,128,128,false,0);
    p4_ui_text(&actual,INT_MAX,INT_MIN,"A",0,UINT_MAX,SIZE_MAX);
    p4_ui_sprite(&actual,INT_MIN,0,4096,1,source,128,1,false,0);
    p4_ui_round_rect(&actual,INT_MAX,0,4096,4096,2048,0);
    p4_card_suit(&actual,INT_MIN,INT_MAX,512,0,0);
    p4_ui_material_mirrored_rows(&actual,INT_MIN,INT_MAX,4096,4096,expanded,128);
    p4_game_surface_t invalid=actual;invalid.stride_pixels=width-1U;
    p4_ui_text(&invalid,0,0,"A",0,24,1);
    p4_ui_sprite(&invalid,0,0,8,8,source,8,8,false,0);
    p4_ui_material_mirrored_rows(&invalid,0,0,8,8,expanded,128);
    p4_draw_fill_circle(&invalid,10,10,7,0);
    same(a,b,count);
    free(expanded);free(source);free(a);free(b);
}
static void verify_diamond(unsigned width,unsigned height)
{
    const size_t stride=(size_t)width+7U,count=stride*height+38U;
    uint16_t *a=malloc(count*sizeof(*a)),*b=malloc(count*sizeof(*b));
    assert(a&&b);
    p4_game_surface_t actual={.pixels=a+19,.width=(uint16_t)width,
        .height=(uint16_t)height,.stride_pixels=stride};
    p4_game_surface_t expected=actual;expected.pixels=b+19;
    const int positions[][2]={{0,0},{23,17},{-17,-11},
        {(int)width-5,(int)height-3},{-(int)width,-(int)height},
        {(int)width+1,(int)height+1},{-8192,8192}};
    for(int size=3;size<=512;++size){
        const int x=positions[(unsigned)size%7U][0],y=positions[(unsigned)size%7U][1];
        reset(a,b,count);
        p4_card_suit(&actual,x,y,size,1U,0xb904U);
        /* Independent per-pixel geometric coverage, no production row-span
         * endpoint calculation. Q8 Manhattan distance defines the rhombus. */
        for(int py=0;py<size;++py)for(int px=0;px<size;++px){
            if(x+px<0||y+py<0||x+px>=(int)width||y+py>=(int)height)continue;
            const int dy=abs(2*py-(size-1));
            const int extent=((size-1-dy)/2)*1024/5;
            const int distance=abs(px*256-(size-1)*128);
            const int coverage=extent+256-distance;
            if(coverage<=0)continue;
            const unsigned alpha=coverage>=256?15U:(unsigned)(coverage*15+128)/256U;
            uint16_t *dst=expected.pixels+(size_t)(y+py)*stride+(size_t)(x+px);
            *dst=reference_p4_ui_blend(*dst,0xb904U,alpha);
        }
        same(a,b,count);
    }
    /* A pointed top/bottom and mirrored, connected, widening rows detect
     * the original clipped-hexagon defect independently of the formula. */
    for(int size=3;size<=128;++size){
        memset(a,0,count*sizeof(*a));p4_card_suit(&actual,11,7,size,1U,0xffffU);
        unsigned previous=0;
        for(int py=0;py<size;++py){
            unsigned covered=0;bool gap=false,started=false;
            for(int px=0;px<size;++px){
                const uint16_t value=actual.pixels[(size_t)(7+py)*stride+(size_t)(11+px)];
                assert(value==actual.pixels[(size_t)(7+py)*stride+(size_t)(11+size-1-px)]);
                assert(value==actual.pixels[(size_t)(7+size-1-py)*stride+(size_t)(11+px)]);
                if(value){assert(!gap);started=true;++covered;}
                else if(started)gap=true;
            }
            if(py==0||py==size-1)assert(covered==(size%2?1U:2U));
            if(py<=(size-1)/2)assert(covered>=previous);
            else assert(covered<=previous);
            previous=covered;
        }
    }
    free(a);free(b);
}

int main(void)
{
    verify_blend(); verify(320U,200U);verify(768U,480U);
    printf("Unchanged raster equivalence: %zu full guarded/padded surface comparisons; exhaustive RGB565 component blends passed.\n",comparisons);
    const size_t old_comparisons=comparisons;
    verify_diamond(320U,200U);verify_diamond(768U,480U);
    printf("Diamond shape: %zu guarded pixel-oracle comparisons; sizes3..512, pointed tips, symmetric connected rows and clipping passed.\n",comparisons-old_comparisons);
    return 0;
}
