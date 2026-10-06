// SPDX-License-Identifier: MIT
#include "p4/multiplayer_group.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 for(uint8_t count=2;count<=4;++count) {
  p4_mp_group_start_t g[4]={0};uint8_t b[12];assert(p4_mp_group_begin(&g[0],77,count,0));
  for(uint64_t now=0;now<2000;now+=20)for(uint8_t i=0;i<count;++i) {
   if(!p4_mp_group_poll(&g[i],now,b))continue;
   if(i==0){for(uint8_t j=1;j<count;++j)if((now/20+j)%7)assert(p4_mp_group_receive(&g[j],j,0,77,b,12,now)||g[j].phase==P4_MP_GROUP_DUE);}
   else if((now/20+i)%5)assert(p4_mp_group_receive(&g[0],0,i,77,b,12,now)||g[0].phase==P4_MP_GROUP_DUE);
  }
  for(unsigned i=0;i<count;++i)assert(g[i].phase==P4_MP_GROUP_DUE&&g[i].count==count);
 }
 p4_mp_group_start_t g={0};uint8_t b[12];assert(p4_mp_group_begin(&g,1,4,0));
 assert(p4_mp_group_poll(&g,0,b));p4_mp_group_start_t client={0};
 assert(!p4_mp_group_receive(&client,1,2,1,b,12,0));
 assert(!p4_mp_group_receive(&client,1,0,2,b,12,0));
 assert(!p4_mp_group_receive(&client,1,0,1,b,11,0));
 assert(p4_mp_group_receive(&client,1,0,1,b,12,0));
 assert(p4_mp_group_poll(&client,0,b));assert(p4_mp_group_receive(&g,0,1,1,b,12,0));
 assert(!p4_mp_group_poll(&g,8000,b));assert(g.phase==P4_MP_GROUP_FAILED);
 puts("PASS: 2/3/4 player start, loss/retry, all-member gate, sender/token/count/length bounds and missing-peer timeout");
}
