// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p4/game.h"

extern const p4_game_descriptor_t p4_byte_buddy_game;
enum { LEGACY_BYTES=1397824, HD_BYTES=6924352, GUARD=29 };
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"line %d: %s\n",__LINE__,#test);return 1; } } while (0)

static bool start(p4_game_instance_t *instance, void *state,
                  p4_game_services_t *services, const uint8_t *art, size_t bytes)
{
    *instance=(p4_game_instance_t){0};
    *services=(p4_game_services_t){
        .available_capabilities=P4_GAME_CAP_VIDEO|
            P4_GAME_CAP_CONTROLS|P4_GAME_CAP_STORAGE,
        .resource_data=art,.resource_bytes=bytes,.resource_format_version=1};
    return p4_game_instance_start(instance,&p4_byte_buddy_game,services,state,
                                   p4_byte_buddy_game.state_bytes);
}

int main(void)
{
    FILE *file=fopen(BB_HD_ART_PATH,"rb");CHECK(file!=NULL);
    uint8_t *art=malloc(LEGACY_BYTES+HD_BYTES+1U);CHECK(art!=NULL);
    CHECK(fread(art,1,LEGACY_BYTES+HD_BYTES+1U,file)==LEGACY_BYTES+HD_BYTES);
    CHECK(fclose(file)==0);
    void *old_state=calloc(1,p4_byte_buddy_game.state_bytes);
    void *hd_state=calloc(1,p4_byte_buddy_game.state_bytes);
    CHECK(old_state!=NULL && hd_state!=NULL);
    p4_game_instance_t old={0},hd={0};
    p4_game_services_t old_services={0},hd_services={0};
    CHECK(start(&old,old_state,&old_services,art,LEGACY_BYTES));
    CHECK(start(&hd,hd_state,&hd_services,art,LEGACY_BYTES+HD_BYTES));
    const size_t capacity=775U*480U+GUARD*2U;
    uint16_t *old_pixels=malloc(capacity*sizeof(uint16_t));
    uint16_t *hd_pixels=malloc(capacity*sizeof(uint16_t));
    CHECK(old_pixels!=NULL && hd_pixels!=NULL);
    p4_game_surface_t a={.pixels=old_pixels+GUARD,.width=320,.height=200,.stride_pixels=327};
    p4_game_surface_t b=a;b.pixels=hd_pixels+GUARD;
    for (size_t i=0;i<capacity;++i) old_pixels[i]=hd_pixels[i]=0x5aa5;
    for (unsigned frame=0;frame<240U;++frame) {
        const p4_game_input_t input={.held=frame%40U==0U ? P4_BUTTON_A : 0U,
            .pressed=frame%40U==0U ? P4_BUTTON_A : 0U};
        CHECK(p4_game_instance_update(&old,&input,17U)==P4_GAME_CONTINUE);
        CHECK(p4_game_instance_update(&hd,&input,17U)==P4_GAME_CONTINUE);
        CHECK(p4_game_instance_render(&old,&a));CHECK(p4_game_instance_render(&hd,&b));
        CHECK(memcmp(old_pixels,hd_pixels,capacity*sizeof(uint16_t))==0);
    }
    for (size_t i=0;i<capacity;++i) old_pixels[i]=hd_pixels[i]=0x5aa5;
    old.services.available_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    hd.services.available_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    a.width=b.width=768;a.height=b.height=480;a.stride_pixels=b.stride_pixels=775;
    CHECK(p4_game_instance_render(&old,&a));CHECK(p4_game_instance_render(&hd,&b));
    CHECK(memcmp(old_pixels,hd_pixels,capacity*sizeof(uint16_t))!=0);
    for (unsigned y=0;y<480U;++y) for (unsigned x=768U;x<775U;++x)
        CHECK(hd_pixels[GUARD+y*775U+x]==0x5aa5);
    for (unsigned i=0;i<GUARD;++i) {
        CHECK(hd_pixels[i]==0x5aa5);
        CHECK(hd_pixels[GUARD+775U*480U+i]==0x5aa5);
    }
    p4_game_instance_stop(&old);p4_game_instance_stop(&hd);
    const size_t cuts[]={LEGACY_BYTES-1U,LEGACY_BYTES+1U,LEGACY_BYTES+63U,
                        LEGACY_BYTES+HD_BYTES-1U,LEGACY_BYTES+HD_BYTES+1U};
    for (unsigned i=0;i<sizeof(cuts)/sizeof(cuts[0]);++i)
        CHECK(!start(&hd,hd_state,&hd_services,art,cuts[i]));
    const unsigned fields[]={0,8,12,16,20,24,28,32,36,40,44,48,63};
    for (unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i) {
        const size_t offset=LEGACY_BYTES+fields[i];art[offset]^=0x80U;
        CHECK(!start(&hd,hd_state,&hd_services,art,LEGACY_BYTES+HD_BYTES));
        art[offset]^=0x80U;
    }
    free(old_pixels);free(hd_pixels);free(old_state);free(hd_state);free(art);
    puts("upscale resource: strict bounds, native bank used, 240 fallback frames identical");
    return 0;
}
