// SPDX-License-Identifier: MIT
#include "p4/multiplayer_group.h"
#include <string.h>
bool p4_mp_group_begin(p4_mp_group_start_t *g,uint16_t token,uint8_t count,uint64_t now) {
 if(!g||!token||count<2||count>4||g->phase!=P4_MP_GROUP_IDLE)return false;
 *g=(p4_mp_group_start_t){.phase=P4_MP_GROUP_WAITING,.host=true,.count=count,
  .token=token,.ready_mask=1,.started_ms=now};return true;
}
bool p4_mp_group_receive(p4_mp_group_start_t *g,uint8_t local,uint8_t sender,
 uint16_t token,const uint8_t *b,size_t n,uint64_t now) {
 if(!g||!b||n!=12||memcmp(b,"P4GS",4)||b[4]!=1||b[5]<1||b[5]>3||
    b[6]<2||b[6]>4||b[7]||b[10]||b[11]||!token||
    ((uint16_t)b[8]|(uint16_t)((uint16_t)b[9]<<8))!=token||
    local>=b[6]||sender>=b[6]||sender==local)return false;
 unsigned kind=b[5];
 if(g->phase==P4_MP_GROUP_IDLE) {
  if(local==0||sender!=0||kind!=1)return false;
  *g=(p4_mp_group_start_t){.phase=P4_MP_GROUP_WAITING,.count=b[6],.slot=local,
   .token=token,.started_ms=now};
 }
 if(g->phase==P4_MP_GROUP_FAILED||g->phase==P4_MP_GROUP_DUE||g->token!=token||g->count!=b[6]||g->slot!=local)return false;
 if(g->host) {
  if(kind!=2||!sender)return false;
  g->ready_mask|=(uint8_t)(1u<<sender);
  if(g->phase==P4_MP_GROUP_WAITING&&g->ready_mask==(1u<<g->count)-1u) {
   g->phase=P4_MP_GROUP_COMMITTED;g->launch_ms=now+P4_MP_GROUP_HOLD_MS;g->next_send_ms=0;
  }
 } else {
  if(sender!=0||kind==2)return false;
  if(kind==3&&g->phase==P4_MP_GROUP_WAITING) {
   g->phase=P4_MP_GROUP_COMMITTED;g->launch_ms=now+P4_MP_GROUP_HOLD_MS;
  }
 }
 return true;
}
bool p4_mp_group_poll(p4_mp_group_start_t *g,uint64_t now,uint8_t out[12]) {
 if(!g||!out||g->phase==P4_MP_GROUP_IDLE||g->phase==P4_MP_GROUP_FAILED||g->phase==P4_MP_GROUP_DUE)return false;
 if(g->phase==P4_MP_GROUP_COMMITTED&&now>=g->launch_ms){g->phase=P4_MP_GROUP_DUE;return false;}
 if(now>=g->started_ms&&now-g->started_ms>=P4_MP_GROUP_TIMEOUT_MS){g->phase=P4_MP_GROUP_FAILED;return false;}
 if(g->next_send_ms&&now<g->next_send_ms)return false;
 g->next_send_ms=now+100;memset(out,0,12);memcpy(out,"P4GS",4);
 out[4]=1;out[5]=(uint8_t)(g->host?(g->phase==P4_MP_GROUP_COMMITTED?3:1):2);
 out[6]=g->count;out[8]=(uint8_t)g->token;out[9]=(uint8_t)(g->token>>8);return true;
}
