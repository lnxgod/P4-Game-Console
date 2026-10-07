// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "p4/game.h"
#include "p4_air_hockey_internal.h"
#include "table_presentation.h"
#include "rink_marks.h"
#include <string.h>
extern const p4_game_descriptor_t p4_p4_air_hockey_game;
#include "legacy_surface.h"
static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;}} while(0)
static void frame(p4_game_instance_t *i,p4_game_surface_t *s,uint16_t *m,const char *name)
{
 const size_t words=s->stride_pixels*s->height;
 for(size_t n=0;n<words+32U;++n)m[n]=0x55aaU;
 p4_air_hockey_visual_snap(i->context.state); /* Static scene oracle, independent of motion delay. */
 CHECK(p4_game_instance_render(i,s));
 for(size_t n=0;n<16U;++n){CHECK(m[n]==0x55aaU);CHECK(m[16U+words+n]==0x55aaU);}
 for(unsigned y=0;y<s->height;++y)for(size_t x=s->width;x<s->stride_pixels;++x)CHECK(s->pixels[y*s->stride_pixels+x]==0x55aaU);
 CHECK(s->pixels[0]!=0x55aaU);CHECK(s->pixels[(s->height-1U)*s->stride_pixels+s->width-1U]!=0x55aaU);
 const char *dir=getenv("P4_CAPTURE_DIR");if(dir==NULL)return;
 char path[1024];int count=snprintf(path,sizeof(path),"%s/%s-%u.ppm",dir,name,s->width);CHECK(count>0&&(size_t)count<sizeof(path));
 FILE *f=fopen(path,"wb");CHECK(f!=NULL);if(f==NULL)return;
 fprintf(f,"P6\n%u %u\n255\n",s->width,s->height);
 for(unsigned y=0;y<s->height;++y)for(unsigned x=0;x<s->width;++x){unsigned v=s->pixels[y*s->stride_pixels+x];fputc((int)(((v>>11U)&31U)*255U/31U),f);fputc((int)(((v>>5U)&63U)*255U/63U),f);fputc((int)((v&31U)*255U/31U),f);}
 CHECK(fclose(f)==0);
}
static void step(p4_game_instance_t *i,uint32_t buttons,bool touch,uint16_t x,uint16_t y)
{
 const p4_game_input_t input={.pressed=buttons,.held=buttons,.touch_valid=touch,.touch_count=touch?1U:0U,.touches={{x,y}}};
 CHECK(p4_game_instance_update(i,&input,16U)==P4_GAME_CONTINUE);
}
static uint32_t frame_hash(const p4_game_surface_t *surface)
{
    uint32_t hash = UINT32_C(2166136261);
    for (unsigned y = 0U; y < surface->height; ++y)
        for (unsigned x = 0U; x < surface->width; ++x)
            hash = (hash ^ surface->pixels[y * surface->stride_pixels + x]) *
                UINT32_C(16777619);
    return hash;
}
/* Independent old geometric definition, retained only as an exact pixel oracle. */
static void reference_ring(p4_game_surface_t *surface, int cx, int cy, int radius)
{
    const int x = p4_ui_x(surface, cx), y = p4_ui_y(surface, cy);
    const int r = p4_ui_x(surface, radius);
    const int inner = r - (surface->width == 768U ? 2 : 1);
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r && dx * dx + dy * dy >= inner * inner)
                p4_draw_pixel(surface, x + dx, y + dy, 0x6e7fU);
}
static void test_raster_equivalence(void)
{
    const int centers[][2] = {{160,100},{57,100},{263,100},{0,0},{319,199},{-8,100},{160,207}};
    const int radii[] = {1,2,17,23,64,128};
    uint16_t original[16], mirrored[32];
    for (unsigned y=0U;y<4U;++y) for(unsigned x=0U;x<4U;++x) {
        const uint16_t value=(uint16_t)(100U+y*16U+x);
        original[y*4U+x]=value;mirrored[y*8U+x]=value;mirrored[y*8U+7U-x]=value;
    }
    for (unsigned mode=0U;mode<2U;++mode) {
        const uint16_t width=mode==0U?320U:768U,height=mode==0U?200U:480U;
        const size_t stride=(size_t)width+7U,words=stride*height+32U;
        uint16_t *a=malloc(words*sizeof(*a)),*b=malloc(words*sizeof(*b));
        CHECK(a!=NULL&&b!=NULL);if(a==NULL||b==NULL){free(a);free(b);continue;}
        p4_game_surface_t actual={.pixels=a+16U,.stride_pixels=stride,.width=width,.height=height};
        p4_game_surface_t expected=actual;expected.pixels=b+16U;
        for(size_t c=0U;c<sizeof(centers)/sizeof(centers[0]);++c)
            for(size_t r=0U;r<sizeof(radii)/sizeof(radii[0]);++r) {
                memset(a,0x55,words*sizeof(*a));memset(b,0x55,words*sizeof(*b));
                hockey_rink_ring(&actual,centers[c][0],centers[c][1],radii[r],0x6e7fU);
                reference_ring(&expected,centers[c][0],centers[c][1],radii[r]);
                CHECK(memcmp(a,b,words*sizeof(*a))==0);
            }
        memset(a,0x55,words*sizeof(*a));memset(b,0x55,words*sizeof(*b));
        table_material(&actual,-9,-5,75,51,mirrored,4);
        for(int y=0;y<p4_ui_y(&expected,46);++y)for(int x=0;x<p4_ui_x(&expected,66);++x){
            int sx=x%8,sy=y%8;if(sx>=4)sx=7-sx;if(sy>=4)sy=7-sy;
            expected.pixels[(size_t)y*stride+(size_t)x]=original[sy*4+sx];
        }
        CHECK(memcmp(a,b,words*sizeof(*a))==0);
        free(a);free(b);
    }
}
int main(void)
{
 test_raster_equivalence();
 CHECK((p4_p4_air_hockey_game.required_capabilities&P4_GAME_CAP_VIDEO_HIGH_RES)!=0U);
 CHECK((p4_p4_air_hockey_game.optional_capabilities&P4_GAME_CAP_VIDEO_HIGH_RES)==0U);
 /* Real cartridge admission must reject low-resolution-only services. */
 p4_game_instance_t rejected={0};
 p4_air_hockey_state_t rejected_state;
 const p4_game_services_t legacy_services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS};
 CHECK(!p4_game_instance_start(&rejected,&p4_p4_air_hockey_game,&legacy_services,&rejected_state,sizeof(rejected_state)));
 CHECK(!rejected.active);
 for(unsigned mode=0;mode<2U;++mode){
 const uint16_t width=mode==0U?320U:768U,height=mode==0U?200U:480U;
 const size_t stride=(size_t)width+7U,words=stride*height;
 uint16_t *memory=calloc(words+32U,sizeof(*memory));CHECK(memory!=NULL);if(memory==NULL)continue;
 p4_air_hockey_state_t state;p4_game_instance_t instance={0};
 const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|(mode==0U?0U:P4_GAME_CAP_VIDEO_HIGH_RES)};
 CHECK(test_start_game(&instance,&p4_p4_air_hockey_game,&services,&state,sizeof(state)));
 CHECK((instance.descriptor==&p4_p4_air_hockey_game)==(mode!=0U));
 p4_game_surface_t surface={.pixels=memory+16U,.stride_pixels=stride,.width=width,.height=height};
 frame(&instance,&surface,memory,"title");
 CHECK(state.ui_screen==P4_AIR_HOCKEY_UI_TITLE);
 step(&instance,0U,true,132U,130U); CHECK(state.ui_screen==P4_AIR_HOCKEY_UI_PLAY);
 step(&instance,0U,false,0U,0U);
 frame(&instance,&surface,memory,"initial");
 const int32_t before=state.paddle_x[0];step(&instance,0U,true,120U,100U);CHECK(state.paddle_x[0]>before);
 state.phase=P4_AIR_HOCKEY_PLAY;frame(&instance,&surface,memory,"play");
 const uint32_t integer_frame = frame_hash(&surface);
 state.puck_x += 128; p4_air_hockey_visual_snap(&state); /* Half of a canonical pixel is visible at 768px. */
 CHECK(p4_game_instance_render(&instance,&surface));
 CHECK((integer_frame != frame_hash(&surface)) == (width == 768U));
 state.puck_x -= 128;
 step(&instance,0U,false,0U,0U);
 step(&instance,0U,true,285U,12U); CHECK(state.ui_screen==P4_AIR_HOCKEY_UI_PAUSED);
 frame(&instance,&surface,memory,"paused");
 step(&instance,0U,false,0U,0U); step(&instance,P4_BUTTON_START,false,0U,0U);
 CHECK(state.ui_screen==P4_AIR_HOCKEY_UI_PLAY);
 state.local_player_slot=1U;state.mode=P4_AIR_HOCKEY_NETWORK;
 frame(&instance,&surface,memory,"client");
 state.mode=P4_AIR_HOCKEY_OFFLINE;
 state.phase=P4_AIR_HOCKEY_GAME_OVER;state.winner=1U;frame(&instance,&surface,memory,"result");
 step(&instance,0U,true,132U,130U);CHECK(state.phase==P4_AIR_HOCKEY_SERVE);
 p4_game_instance_stop(&instance);free(memory);
 }
 return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
