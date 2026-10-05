// SPDX-License-Identifier: MIT
#include "p4/clock_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t reply[28];static unsigned sets,replies;
static p4_clock_status_t current={.present=true};
static bool send(void *ctx,const uint8_t *data,size_t n)
{ (void)ctx;assert(n==sizeof(reply));memcpy(reply,data,n);++replies;return true; }
static p4_clock_status_t status(void *ctx){(void)ctx;return current;}
static int set(void *ctx,uint32_t epoch){(void)ctx;++sets;current.unix_seconds=epoch;current.valid=true;return 0;}
static uint32_t crc(const uint8_t *p,size_t n)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0;i<n;++i){c^=p[i];for(unsigned j=0;j<8U;++j)c=(c>>1U)^((c&1U)?UINT32_C(0xedb88320):0U);}
    return ~c;
}
static void put(uint8_t *p,uint32_t value){for(unsigned i=0;i<4U;++i)p[i]=(uint8_t)(value>>(8U*i));}
static void request(uint8_t r[20],uint8_t cmd,uint32_t nonce,uint32_t value)
{ memset(r,0,20);memcpy(r,"P4K1",4);r[4]=1;r[5]=cmd;put(r+8,nonce);put(r+12,value);put(r+16,crc(r,16)); }
int main(void)
{
    p4_clock_control_t c={.send=send,.status=status,.set=set};uint8_t r[20];
    request(r,2,1,UINT32_C(1709251139));
    for(size_t i=0;i<sizeof(r);++i)(void)p4_clock_control_consume(&c,r+i,1,10U+i);
    assert(sets==1 && replies==1 && reply[6]==0 && reply[7]==3);
    (void)p4_clock_control_consume(&c,r,sizeof(r),50);assert(sets==1); /* No duplicate write. */
    request(r,2,1,UINT32_C(1709251140));
    (void)p4_clock_control_consume(&c,r,sizeof(r),60);assert(reply[6]==1 && sets==1);
    request(r,2,2,0);
    (void)p4_clock_control_consume(&c,r,sizeof(r),70);assert(reply[6]==1 && sets==1);
    request(r,2,2,UINT32_C(1709251139));r[19]^=1;
    (void)p4_clock_control_consume(&c,r,sizeof(r),80);assert(reply[6]==1 && sets==1);
    request(r,2,2,UINT32_C(1709251139));current.pending=true;
    (void)p4_clock_control_consume(&c,r,sizeof(r),90);assert(reply[6]==2 && sets==1);current.pending=false;
    current.present=false;
    (void)p4_clock_control_consume(&c,r,sizeof(r),100);assert(reply[6]==3 && sets==1);current.present=true;
    request(r,1,3,0);
    (void)p4_clock_control_consume(&c,r,8,110);
    (void)p4_clock_control_consume(&c,r,sizeof(r),2000);assert(reply[6]==0 && reply[5]==1 && sets==1);
    uint8_t pair[40];memcpy(pair,r,20);memcpy(pair+20,r,20);
    const unsigned before=replies;(void)p4_clock_control_consume(&c,pair,sizeof(pair),2010);
    assert(replies==before+2);
    for(unsigned i=0;i<10000U;++i){uint8_t noise=(uint8_t)(i*37U);(void)p4_clock_control_consume(&c,&noise,1,2020);}
    (void)p4_clock_control_consume(&c,r,sizeof(r),4000);assert(reply[6]==0 && sets==1);
    puts("USB clock framing, malformed input, duplicate write and timeout tests passed");
}
