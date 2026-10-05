// SPDX-License-Identifier: MIT
#include "p4/clock_control.h"
#include <string.h>
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8U | (uint32_t)p[2]<<16U | (uint32_t)p[3]<<24U; }
static void put32(uint8_t *p, uint32_t n)
{ for (unsigned i=0; i<4U; ++i) p[i]=(uint8_t)(n>>(8U*i)); }
static uint32_t crc(const uint8_t *p, size_t n)
{
    uint32_t c=UINT32_MAX;
    for (size_t i=0; i<n; ++i) {
        c^=p[i];
        for (unsigned j=0; j<8U; ++j) c=(c>>1U)^((c&1U)?UINT32_C(0xedb88320):0U);
    }
    return ~c;
}
static void handle(p4_clock_control_t *c)
{
    const uint8_t *r=c->request;
    const uint8_t command=r[5];
    const uint32_t nonce=get32(r+8), seconds=get32(r+12);
    int result=0;
    p4_clock_status_t status=c->status(c->context);
    if (r[4]!=1U || r[6] || r[7] || !nonce || get32(r+16)!=crc(r,16) ||
        (command!=1U && command!=2U) || (command==1U && seconds) ||
        (command==2U && (seconds<UINT32_C(946684800) || seconds>UINT32_C(4102444799)))) {
        result=1;
    } else if (command==2U) {
        if (nonce==c->last_nonce) result=seconds==c->last_seconds?c->last_result:1;
        else if (!status.present) result=3;
        else if (status.pending) result=2;
        else {
            result=c->set(c->context,seconds);
            c->last_nonce=nonce; c->last_seconds=seconds; c->last_result=result;
            status=c->status(c->context);
        }
    }
    uint8_t reply[P4_CLOCK_RESPONSE_BYTES]={0};
    memcpy(reply,"P4L1",4);reply[4]=1;reply[5]=command;reply[6]=(uint8_t)result;
    reply[7]=(uint8_t)((status.present?1U:0U)|(status.valid?2U:0U)|(status.pending?4U:0U));
    put32(reply+8,nonce);put32(reply+12,status.valid?status.unix_seconds:0U);
    put32(reply+16,(uint32_t)status.error);put32(reply+24,crc(reply,24));
    (void)c->send(c->context,reply,sizeof(reply));
}
bool p4_clock_control_consume(p4_clock_control_t *c,const uint8_t *data,size_t size,uint64_t now)
{
    if (!c || !c->send || !c->status || !c->set || (!data && size)) return false;
    if (c->used && now-c->last_byte_ms>1000U) c->used=0;
    bool claimed=c->used>=4U;
    for (size_t i=0; i<size; ++i) {
        const uint8_t b=data[i];
        if (c->used<4U && b!=(uint8_t)"P4K1"[c->used]) {
            c->used=b=='P'?1U:0U;
            if (c->used) c->request[0]=b;
        } else {
            c->request[c->used++]=b;
            if (c->used>=4U) claimed=true;
            if (c->used==P4_CLOCK_REQUEST_BYTES) { handle(c); c->used=0; }
        }
        c->last_byte_ms=now;
    }
    return claimed;
}
