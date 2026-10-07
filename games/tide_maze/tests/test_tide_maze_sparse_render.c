// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef TM_RECORD_SPARSE_HASHES
#include "sparse_render_hashes.h"
#endif
/* Frozen original-render pixels exercise unused vertices with empty, isolated,
 * dense and irregular wet masks, both surfaces, guards and unchanged state. */
int main(void){
 unsigned case_number=0;
 for(unsigned native=0;native<2;++native){
  const unsigned w=native?768U:320U,h=native?480U:200U,stride=w+7U;
  const size_t count=(size_t)stride*h+32U;
  uint16_t *pixels=malloc(count*sizeof(*pixels));assert(pixels);
  p4_game_surface_t f={.pixels=pixels+16,.stride_pixels=stride,.width=(uint16_t)w,.height=(uint16_t)h};
  for(unsigned sample=0;sample<12;++sample){
   for(size_t i=0;i<count;++i)pixels[i]=0xa55a;
   tm_state s={0};tm_reset(&s,sample%TM_LEVELS);
   for(unsigned i=0;i<TM_CELLS;++i){
    s.wet[i]=(uint8_t)(sample==1U||(sample>=6U&&(i*37U+sample*13U)%11U<sample-4U));
    s.water[i]=(int16_t)((i*71U+sample*131U)%1025U);
   }
   if(sample>=2U&&sample<6U){const unsigned isolated[]={2U+2U*TM_W,27U+3U*TM_W,3U+15U*TM_W,25U+15U*TM_W};s.wet[isolated[sample-2U]]=1;}
   s.animation_ms=sample*1703U;s.phase=TM_PLAY;s.linked=true;s.host=true;
   tm_state before=s;p4_game_context_t ctx={.state=&s};assert(tm_render(&ctx,&f));assert(memcmp(&s,&before,sizeof(s))==0);
   uint64_t hash=UINT64_C(1469598103934665603);
   for(unsigned y=0;y<h;++y){
    for(unsigned x=0;x<w;++x){hash^=f.pixels[(size_t)y*stride+x];hash*=UINT64_C(1099511628211);}
    for(unsigned x=w;x<stride;++x)assert(f.pixels[(size_t)y*stride+x]==0xa55a);
   }
   for(unsigned i=0;i<16;++i){assert(pixels[i]==0xa55a);assert(pixels[count-1U-i]==0xa55a);}
#ifdef TM_RECORD_SPARSE_HASHES
   printf("UINT64_C(0x%016llx),\n",(unsigned long long)hash);
#else
   assert(hash==tm_sparse_render_hashes[case_number]);
#endif
   ++case_number;
  }
  free(pixels);
 }
#ifndef TM_RECORD_SPARSE_HASHES
 assert(case_number==sizeof(tm_sparse_render_hashes)/sizeof(tm_sparse_render_hashes[0]));
 puts("24 original sparse-water scene hashes match: native/legacy, guards, no state mutation");
#endif
 return 0;
}
