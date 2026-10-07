#include "doom/video_convert.h"
#include "doom/video_indexed.h"
#include "doom_touch/input.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { WIDTH = 768, HEIGHT = 480, PAD = 7, GUARD = 16 };
static uint16_t rgb565(uint32_t x)
{ return (uint16_t)(((x >> 8U) & 0xf800U) | ((x >> 5U) & 0x7e0U) | ((x >> 3U) & 0x1fU)); }
static void fill16(uint16_t *p, size_t n, uint16_t v)
{ for (size_t i=0U;i<n;++i) p[i]=v; }
int main(void)
{
    const size_t stride = WIDTH + PAD, words = stride * HEIGHT;
    uint32_t *source = malloc(words * sizeof(*source));
    uint32_t *overlay = malloc(words * sizeof(*overlay));
    uint8_t *indices = malloc(words);
    uint16_t *storage = malloc((words + 2U*GUARD)*sizeof(*storage));
    uint16_t *reference = malloc(words * sizeof(*reference));
    uint16_t *dest = storage + GUARD;
    uint32_t palette[256], row[WIDTH];
    assert(source && overlay && indices && storage && reference);
    for (size_t i=0U;i<256U;++i) palette[i]=(uint32_t)(i*65537U) ^ UINT32_C(0x7391ab);
    for (size_t y=0U;y<HEIGHT;++y) {
        for (size_t x=0U;x<stride;++x) {
            indices[y*stride+x]=(uint8_t)((x*73U+y*151U) & 255U);
            source[y*stride+x]=(uint32_t)((x*63689U+y*378551U) & 0xffffffU);
        }
    }
    fill16(storage,words+2U*GUARD,0x5a5aU);
    assert(doom_video_convert_xrgb8888_to_rgb565_768x480(source,stride,dest,stride));
    for(size_t y=0U;y<HEIGHT;++y)for(size_t x=0U;x<stride;++x)
        assert(dest[y*stride+x] == (x<WIDTH ? rgb565(source[y*stride+x]) : 0x5a5aU));
    for(size_t i=0U;i<GUARD;++i)assert(storage[i]==0x5a5aU && dest[words+i]==0x5a5aU);
    /* Distinct native neighbours must survive; this input cannot be reproduced
     * by duplicating a completed 320x200 frame. */
    assert(dest[321U] != dest[320U]);
    const uint32_t masks[] = {0U,1U,16U,32U,UINT32_C(0x1555),UINT32_C(0x2aaa),UINT32_C(0x3fff)};
    for(size_t m=0U;m<sizeof(masks)/sizeof(*masks);++m) {
        for(size_t y=0U;y<HEIGHT;++y)for(size_t x=0U;x<stride;++x)source[y*stride+x]=palette[indices[y*stride+x]];
        assert(doom_touch_overlay_render_xrgb8888_sized(source,stride,overlay,stride,WIDTH,HEIGHT,masks[m]));
        fill16(reference,words,0x5a5aU);
        assert(doom_video_convert_xrgb8888_to_rgb565_768x480(overlay,stride,reference,stride));
        fill16(storage,words+2U*GUARD,0x5a5aU);
        assert(doom_video_convert_indexed_touch_to_rgb565_768x480(indices,stride,palette,masks[m],dest,stride,row));
        assert(memcmp(dest,reference,words*sizeof(*dest))==0);
        for(size_t i=0U;i<GUARD;++i)assert(storage[i]==0x5a5aU && dest[words+i]==0x5a5aU);
    }
    fill16(storage,words+2U*GUARD,0x5a5aU);
    memset(row,0xa5,sizeof(row));
    assert(!doom_video_convert_indexed_touch_to_rgb565_768x480(NULL,stride,palette,0U,dest,stride,row));
    assert(!doom_video_convert_indexed_touch_to_rgb565_768x480(indices,WIDTH-1U,palette,0U,dest,stride,row));
    assert(!doom_video_convert_indexed_touch_to_rgb565_768x480(indices,stride,palette,0U,dest,WIDTH-1U,row));
    assert(!doom_video_convert_indexed_touch_to_rgb565_768x480(indices,SIZE_MAX,palette,0U,dest,stride,row));
    assert(!doom_video_convert_indexed_touch_to_rgb565_768x480(indices,stride,palette,UINT32_C(1)<<14U,dest,stride,row));
    assert(!doom_video_convert_indexed_touch_to_rgb565_768x480(indices,stride,palette,0U,(uint16_t *)indices,stride,row));
    assert(!doom_video_convert_indexed_touch_to_rgb565_768x480(indices,stride,palette,0U,dest,stride,(uint32_t *)dest));
    assert(!doom_video_convert_indexed_touch_to_rgb565_768x480(indices,stride,(uint32_t *)((uint8_t *)palette+1U),0U,dest,stride,row));
    assert(!doom_video_convert_xrgb8888_to_rgb565_768x480(source,WIDTH-1U,dest,stride));
    assert(!doom_video_convert_xrgb8888_to_rgb565_768x480(source,SIZE_MAX,dest,stride));
    assert(!doom_video_convert_xrgb8888_to_rgb565_768x480(source,stride,(uint16_t *)source,stride));
    assert(!doom_video_convert_xrgb8888_to_rgb565_768x480((uint32_t *)((uint8_t *)source+1U),stride,dest,stride));
    assert(!doom_video_convert_xrgb8888_to_rgb565_768x480(source,stride,(uint16_t *)((uint8_t *)dest+1U),stride));
    for(size_t i=0U;i<words+2U*GUARD;++i)assert(storage[i]==0x5a5aU);
    for(size_t i=0U;i<WIDTH;++i)assert(row[i]==UINT32_C(0xa5a5a5a5));
    free(source);free(overlay);free(indices);free(storage);free(reference);
    puts("PASS native 768x480 one-to-one RGB565, indexed overlay parity, padded strides, guards, malformed extents and alias rejection");
    return 0;
}
