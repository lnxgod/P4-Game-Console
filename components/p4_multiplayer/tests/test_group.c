// SPDX-License-Identifier: MIT
#include "p4/multiplayer_group.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void commit_recovery(void) {
 const uint16_t token=p4_mp_group_token(7,1);
 assert(token==6 && p4_mp_group_token(0,0)==1);
 p4_mp_group_start_t host={0},guest={0};uint8_t request[12],reply[12];
 assert(p4_mp_group_begin(&host,token,4,0));
 assert(p4_mp_group_poll(&host,0,request));
 assert(p4_mp_group_receive(&guest,3,0,token,request,12,0));
 assert(p4_mp_group_poll(&guest,0,request));
 assert(p4_mp_group_commit_reply(token,4,3,request,12,reply));
 assert(p4_mp_group_receive(&guest,3,0,token,reply,12,1400));
 assert(guest.phase==P4_MP_GROUP_COMMITTED && guest.launch_ms==2400);
 assert(p4_mp_group_receive(&guest,3,0,token,reply,12,1500));
 assert(guest.launch_ms==2400); /* Retries cannot postpone launch. */
 assert(!p4_mp_group_commit_reply(token,4,0,request,12,reply));
 assert(!p4_mp_group_commit_reply(token,4,4,request,12,reply));
 assert(!p4_mp_group_commit_reply(token,3,2,request,12,reply));
 assert(!p4_mp_group_commit_reply(token,5,3,request,12,reply));
 assert(!p4_mp_group_commit_reply(token+1,4,3,request,12,reply));
 assert(!p4_mp_group_commit_reply(0,4,3,request,12,reply));
 assert(!p4_mp_group_commit_reply(token,4,3,request,11,reply));
 assert(!p4_mp_group_commit_reply(token,4,3,request,13,reply));
 assert(!p4_mp_group_commit_reply(token,4,3,NULL,12,reply));
 assert(!p4_mp_group_commit_reply(token,4,3,request,12,NULL));
 for(unsigned i=0;i<12;++i) {
  uint8_t bad[12];memcpy(bad,request,12);bad[i]^=0x80;
  assert(!p4_mp_group_commit_reply(token,4,3,bad,12,reply));
 }
 for(uint8_t kind=1;kind<=3;kind+=2) {
  request[5]=kind;assert(!p4_mp_group_commit_reply(token,4,3,request,12,reply));
 }
}
int main(void) {
 commit_recovery();
 for(uint8_t count=2;count<=4;++count) {
  p4_mp_group_start_t g[4]={0};uint8_t b[12];assert(p4_mp_group_begin(&g[0],77,count,0));
  for(uint64_t now=0;now<2000;now+=20)for(uint8_t i=0;i<count;++i) {
   if(!p4_mp_group_poll(&g[i],now,b))continue;
   /* Exercise the production envelope: PING would reject this 12-byte control. */
   uint8_t wire[P4_MP_MAX_DATAGRAM_BYTES]; size_t length=0;
   assert(p4_mp_packet_encode(P4_MP_GROUP_PACKET_TYPE,7,100U+i,1,0,b,sizeof(b),
       wire,sizeof(wire),&length)==P4_MP_OK);
   p4_mp_packet_view_t decoded;
   assert(p4_mp_packet_decode(wire,length,&decoded)==P4_MP_OK);
   assert(decoded.type==P4_MP_PACKET_GAME_MESSAGE && decoded.payload_length==sizeof(b));
   memcpy(b,decoded.payload,sizeof(b));
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
