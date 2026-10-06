// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef TM_RECORD_RENDER_HASHES
#include "render_hashes.h"
#endif
/* Frozen pre-optimization pixels, covering both native sizes, three labyrinths,
 * stressed water, both marble colors/depths/stripe phases, every overlay, stride
 * padding and buffer guards. Render alone must never mutate game state. */
int main(void){
 unsigned case_number=0;
 for(unsigned size=0;size<2;++size){
  const unsigned w=size?768U:320U,h=size?480U:200U,stride=w+7;
  const size_t count=(size_t)stride*h+32;
  uint16_t *pixels=malloc(count*sizeof(*pixels));assert(pixels);
  p4_game_surface_t f={.pixels=pixels+16,.stride_pixels=stride,.width=(uint16_t)w,.height=(uint16_t)h};
  for(unsigned sample=0;sample<96;++sample){
   for(size_t i=0;i<count;++i)pixels[i]=0xa55a;
   tm_state s={0};tm_reset(&s,sample%TM_LEVELS);s.linked=(sample&1U)!=0;s.host=true;
   s.intent[0]=(tm_intent){1000,-850,900,1000,false};
   for(unsigned tick=0;tick<sample%31U;++tick)tm_fluid(&s);
   s.animation_ms=sample*977U;s.accumulator=sample%TM_STEP;s.phase=(tm_phase)(sample%8U);
   for(unsigned p=0;p<2;++p){
    s.ball[p].x=(19+(int)((sample*17+p*29)%203U))*TM_Q+(int32_t)(sample*7U%256U);
    s.ball[p].y=(19+(int)((sample*11+p*41)%107U))*TM_Q;
    s.previous_x[p]=s.ball[p].x-71;s.previous_y[p]=s.ball[p].y+45;
   }
   tm_state before=s;p4_game_context_t ctx={.state=&s};assert(tm_render(&ctx,&f));assert(memcmp(&s,&before,sizeof(s))==0);
   uint64_t hash=UINT64_C(1469598103934665603);
   for(unsigned y=0;y<h;++y){
    for(unsigned x=0;x<w;++x){hash^=f.pixels[(size_t)y*stride+x];hash*=UINT64_C(1099511628211);}
    for(unsigned x=w;x<stride;++x)assert(f.pixels[(size_t)y*stride+x]==0xa55a);
   }
   for(unsigned i=0;i<16;++i){assert(pixels[i]==0xa55a);assert(pixels[count-1-i]==0xa55a);}
#ifdef TM_RECORD_RENDER_HASHES
   printf("UINT64_C(0x%016llx),\n",(unsigned long long)hash);
#else
   if(hash!=tm_render_hashes[case_number])fprintf(stderr,"case %u: %016llx != %016llx\n",case_number,(unsigned long long)hash,(unsigned long long)tm_render_hashes[case_number]);
   assert(hash==tm_render_hashes[case_number]);
#endif
   ++case_number;
  }free(pixels);
 }
#ifndef TM_RECORD_RENDER_HASHES
 assert(case_number==sizeof(tm_render_hashes)/sizeof(tm_render_hashes[0]));
 puts("192 original-scene hashes match: both resolutions, water, actors, menus, guards and no state mutation");
#endif
 return 0;
}
