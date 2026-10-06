// SPDX-License-Identifier: MIT
/* Reproducible motion proof from the real cartridge callbacks. Output is raw
 * RGB24 on stdout for an ordinary video encoder, never a second game engine. */
#include "tide_maze_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
 tm_state *s=calloc(1,sizeof(*s));uint16_t *pixels=calloc(768U*480U,sizeof(*pixels));assert(s&&pixels);
 p4_game_instance_t game={0};p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_VIDEO_HIGH_RES|P4_GAME_CAP_CONTROLS,.game_id=p4_tide_maze_game.id};
 assert(p4_game_instance_start(&game,&p4_tide_maze_game,&services,s,sizeof(*s)));
 p4_game_surface_t f={pixels,768,768,480};p4_game_input_t in={.pressed=P4_BUTTON_A};
 assert(p4_game_instance_update(&game,&in,0)==P4_GAME_CONTINUE);
 unsigned char row[768*3];
 for(unsigned frame=0;frame<480;++frame){
  uint32_t held=frame<110?P4_BUTTON_RIGHT:frame<210?P4_BUTTON_DOWN:frame<285?P4_BUTTON_LEFT:frame<345?P4_BUTTON_UP:frame<390?P4_BUTTON_RIGHT:0;
  in=(p4_game_input_t){.held=held};assert(p4_game_instance_update(&game,&in,frame%3==2?16:17)==P4_GAME_CONTINUE);
  assert(p4_game_instance_render(&game,&f));
  for(unsigned y=0;y<480;++y){for(unsigned x=0;x<768;++x){uint16_t c=pixels[y*768+x];row[x*3]=(unsigned char)((c>>11)*255/31);row[x*3+1]=(unsigned char)(((c>>5)&63)*255/63);row[x*3+2]=(unsigned char)((c&31)*255/31);}assert(fwrite(row,1,sizeof(row),stdout)==sizeof(row));}
 }
 free(s);free(pixels);return 0;
}
