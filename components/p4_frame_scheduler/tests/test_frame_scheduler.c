// SPDX-License-Identifier: MIT
#include "p4/frame_scheduler.h"
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(p4_tick_scheduler_t)==16U,"Scheduler state ABI changed");
_Static_assert(offsetof(p4_tick_scheduler_t,clock_hz)==8U,"Clock offset changed");
_Static_assert(offsetof(p4_tick_scheduler_t,tick_hz)==12U,"Tick offset changed");
_Static_assert(sizeof(p4_scheduler_status_t)==4U,"Status ABI changed");
static void check_rate(uint32_t clock_hz,uint32_t tick_hz)
{
    p4_tick_scheduler_t s;
    assert(p4_tick_scheduler_init(&s,clock_hz,tick_hz)==P4_SCHEDULER_OK);
    uint64_t total=0;
    for(uint32_t i=0;i<tick_hz*120U;++i){
        uint32_t interval=0;
        assert(p4_tick_scheduler_next(&s,&interval)==P4_SCHEDULER_OK);
        assert(interval==clock_hz/tick_hz || interval==clock_hz/tick_hz+1U);
        total+=interval;
        assert(total==((uint64_t)(i+1U)*clock_hz)/tick_hz);
        assert(s.phase==((uint64_t)(i+1U)*clock_hz)%tick_hz);
    }
    assert(total==(uint64_t)clock_hz*120U && s.phase==0);
}
int main(void)
{
    const uint32_t clocks[]={100U,1000U,16000U,44100U,48000U,UINT32_MAX};
    const uint32_t rates[]={30U,57U,60U};
    for(size_t c=0;c<sizeof(clocks)/sizeof(clocks[0]);++c)
        for(size_t r=0;r<sizeof(rates)/sizeof(rates[0]);++r)check_rate(clocks[c],rates[r]);
    p4_tick_scheduler_t s={0};uint32_t interval=99;
    assert(p4_tick_scheduler_init(NULL,1000,60)==P4_SCHEDULER_INVALID_ARGUMENT);
    assert(p4_tick_scheduler_init(&s,0,60)==P4_SCHEDULER_INVALID_ARGUMENT);
    assert(p4_tick_scheduler_init(&s,1000,0)==P4_SCHEDULER_INVALID_ARGUMENT);
    assert(p4_tick_scheduler_init(&s,50,60)==P4_SCHEDULER_INVALID_ARGUMENT);
    assert(p4_tick_scheduler_next(NULL,&interval)==P4_SCHEDULER_INVALID_ARGUMENT);
    assert(p4_tick_scheduler_next(&s,&interval)==P4_SCHEDULER_INVALID_ARGUMENT && interval==99);
    assert(p4_tick_scheduler_init(&s,1000,60)==P4_SCHEDULER_OK);
    assert(p4_tick_scheduler_next(&s,NULL)==P4_SCHEDULER_INVALID_ARGUMENT && s.phase==0);
    puts("scheduler ABI and per-tick conservation passed across 18 video/audio clocks for 120 seconds each");
    return 0;
}
