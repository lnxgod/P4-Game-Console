// SPDX-License-Identifier: MIT
/* Production ownership service with a joined pthread runtime. Hardware submit
 * is the only injected boundary; accepted leases remain immutable through it. */
#define _POSIX_C_SOURCE 200809L
#include "platform/display_worker.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WIDTH=8, HEIGHT=4, PIXEL_BYTES=WIDTH*HEIGHT*2 };
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool hold_submit,hold_idle,idle_observed,fail_start;
    unsigned allocations,frees,fail_allocation,started,finished,joins;
    int submit_result;
    unsigned char expected[PIXEL_BYTES];
} fixture_t;
typedef struct {
    pthread_t thread;
    void (*entry)(void *);
    void *argument;
    atomic_bool quiescent;
} task_t;
static fixture_t *active;
static uint64_t now_ms(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC,&now)==0);
    return (uint64_t)now.tv_sec*1000U+(uint64_t)now.tv_nsec/1000000U;
}
static void pause_tick(void)
{
    const struct timespec delay={.tv_sec=0,.tv_nsec=1000000L};
    (void)nanosleep(&delay,NULL);
}
static void *allocate_pixels(size_t bytes)
{
    assert(bytes==PIXEL_BYTES);
    ++active->allocations;
    if(active->fail_allocation==active->allocations)return NULL;
    return malloc(bytes);
}
static void free_pixels(void *pixels)
{
    assert(pixels);
    ++active->frees;
    free(pixels);
}
static void *task_entry(void *argument)
{
    task_t *task=argument;
    task->entry(task->argument);
    atomic_store_explicit(&task->quiescent,true,memory_order_release);
    return NULL;
}
static int start_task(void (*entry)(void *),void *argument,void **out)
{
    if(active->fail_start)return DW_NO_MEMORY;
    task_t *task=calloc(1U,sizeof(*task));assert(task);
    task->entry=entry;task->argument=argument;
    atomic_init(&task->quiescent,false);
    assert(pthread_create(&task->thread,NULL,task_entry,task)==0);
    *out=task;
    return DW_OK;
}
static int join_task(void *opaque,uint32_t timeout)
{
    task_t *task=opaque;
    const uint64_t began=now_ms();
    ++active->joins;
    while(!atomic_load_explicit(&task->quiescent,memory_order_acquire)){
        if(now_ms()-began>=timeout)return DW_TIMEOUT;
        pause_tick();
    }
    assert(pthread_join(task->thread,NULL)==0);
    free(task);
    return DW_OK;
}
static int submit(void *context,const void *pixels,size_t stride,uint32_t timeout)
{
    fixture_t *fixture=context;
    assert(fixture==active&&stride==WIDTH&&timeout==77U);
    assert(pthread_mutex_lock(&fixture->lock)==0);
    assert(!memcmp(pixels,fixture->expected,PIXEL_BYTES));
    ++fixture->started;
    assert(pthread_cond_broadcast(&fixture->changed)==0);
    while(fixture->hold_submit)
        assert(pthread_cond_wait(&fixture->changed,&fixture->lock)==0);
    /* Another producer lease may have been written/cancelled during this
     * wait; the accepted surface must still contain exactly the same bytes. */
    assert(!memcmp(pixels,fixture->expected,PIXEL_BYTES));
    ++fixture->finished;
    const int result=fixture->submit_result;
    assert(pthread_mutex_unlock(&fixture->lock)==0);
    return result;
}
void display_worker_test_after_idle_observation(void)
{
    assert(pthread_mutex_lock(&active->lock)==0);
    active->idle_observed=true;
    assert(pthread_cond_broadcast(&active->changed)==0);
    while(active->hold_idle)
        assert(pthread_cond_wait(&active->changed,&active->lock)==0);
    assert(pthread_mutex_unlock(&active->lock)==0);
}
static const display_worker_ops_t ops={allocate_pixels,free_pixels,now_ms,
    pause_tick,start_task,join_task,submit};
static void initialize(fixture_t *fixture)
{
    *fixture=(fixture_t){0};active=fixture;
    assert(pthread_mutex_init(&fixture->lock,NULL)==0);
    assert(pthread_cond_init(&fixture->changed,NULL)==0);
}
static void destroy(fixture_t *fixture)
{
    assert(pthread_cond_destroy(&fixture->changed)==0);
    assert(pthread_mutex_destroy(&fixture->lock)==0);
    active=NULL;
}
static display_worker_t *create(fixture_t *fixture)
{
    display_worker_t *worker=NULL;
    assert(display_worker_create(&ops,fixture,WIDTH,HEIGHT,2U,&worker)==DW_OK);
    assert(worker&&fixture->allocations==2U&&fixture->frees==0U);
    return worker;
}
static display_worker_lease_t acquire_frame(display_worker_t *worker,unsigned char value)
{
    display_worker_lease_t lease={0};
    assert(display_worker_acquire(worker,&lease,1000U)==DW_OK);
    memset(lease.pixels,value,PIXEL_BYTES);
    return lease;
}
static void expect_frame(fixture_t *fixture,const display_worker_lease_t *lease)
{ memcpy(fixture->expected,lease->pixels,PIXEL_BYTES); }
static void wait_until_started(fixture_t *fixture)
{
    assert(pthread_mutex_lock(&fixture->lock)==0);
    while(!fixture->started)
        assert(pthread_cond_wait(&fixture->changed,&fixture->lock)==0);
    assert(pthread_mutex_unlock(&fixture->lock)==0);
}
static void release_submit(fixture_t *fixture)
{
    assert(pthread_mutex_lock(&fixture->lock)==0);
    fixture->hold_submit=false;
    assert(pthread_cond_broadcast(&fixture->changed)==0);
    assert(pthread_mutex_unlock(&fixture->lock)==0);
}
static void construction_failure_test(void)
{
    fixture_t fixture;initialize(&fixture);
    display_worker_t *worker=NULL;
    assert(display_worker_create(&ops,&fixture,SIZE_MAX,HEIGHT,2U,&worker)==DW_INVALID);
    assert(!worker&&!fixture.allocations);
    fixture.fail_allocation=2U;
    assert(display_worker_create(&ops,&fixture,WIDTH,HEIGHT,2U,&worker)==DW_NO_MEMORY);
    assert(!worker&&fixture.allocations==2U&&fixture.frees==1U&&!fixture.joins);
    destroy(&fixture);
    initialize(&fixture);fixture.fail_start=true;
    assert(display_worker_create(&ops,&fixture,WIDTH,HEIGHT,2U,&worker)==DW_NO_MEMORY);
    assert(!worker&&fixture.allocations==2U&&fixture.frees==2U&&!fixture.joins);
    destroy(&fixture);
}
static void lease_and_timeout_test(void)
{
    fixture_t fixture;initialize(&fixture);fixture.hold_submit=true;
    display_worker_t *worker=create(&fixture);
    display_worker_lease_t first=acquire_frame(worker,0x42U),stale=first;
    expect_frame(&fixture,&first);
    assert(display_worker_commit(worker,&first,1000U,77U)==DW_OK&&!first.pixels);
    wait_until_started(&fixture);
    display_worker_lease_t second=acquire_frame(worker,0xa7U),retained=second;
    assert(second.pixels!=stale.pixels&&second.generation!=stale.generation);
    assert(display_worker_cancel(worker,&stale)==DW_INVALID);
    assert(display_worker_commit(worker,&second,0U,77U)==DW_TIMEOUT);
    assert(second.pixels==retained.pixels&&second.generation==retained.generation&&second.slot==retained.slot);
    assert(display_worker_stop(&worker,0U)==DW_INVALID&&fixture.frees==0U);
    assert(display_worker_cancel(worker,&second)==DW_OK&&!second.pixels);
    assert(display_worker_flush(worker,0U)==DW_TIMEOUT);
    assert(display_worker_stop(&worker,0U)==DW_TIMEOUT&&worker&&fixture.frees==0U);
    display_worker_stats_t stats;
    assert(display_worker_stats(worker,&stats)==DW_OK&&stats.closing&&stats.accepted==1U);
    assert(display_worker_acquire(worker,&second,0U)==DW_INVALID);
    release_submit(&fixture);
    assert(display_worker_stop(&worker,1000U)==DW_OK&&!worker);
    assert(fixture.started==1U&&fixture.finished==1U&&fixture.frees==2U&&fixture.joins==2U);
    destroy(&fixture);
}
static void failure_latches_and_joins_test(void)
{
    fixture_t fixture;initialize(&fixture);fixture.submit_result=-37;
    display_worker_t *worker=create(&fixture);
    display_worker_lease_t frame=acquire_frame(worker,0x23U);
    expect_frame(&fixture,&frame);
    assert(display_worker_commit(worker,&frame,1000U,77U)==DW_OK);
    assert(display_worker_flush(worker,1000U)==-37);
    display_worker_stats_t stats;
    assert(display_worker_stats(worker,&stats)==DW_OK);
    assert(stats.accepted==1U&&!stats.completed&&stats.failures==1U&&stats.error==-37);
    assert(display_worker_acquire(worker,&frame,0U)==-37&&fixture.frees==0U);
    assert(display_worker_stop(&worker,1000U)==DW_OK&&!worker&&fixture.frees==2U);
    destroy(&fixture);
}
static void final_commit_close_race_test(void)
{
    fixture_t fixture;initialize(&fixture);fixture.hold_idle=true;
    display_worker_t *worker=create(&fixture);
    assert(pthread_mutex_lock(&fixture.lock)==0);
    while(!fixture.idle_observed)
        assert(pthread_cond_wait(&fixture.changed,&fixture.lock)==0);
    assert(pthread_mutex_unlock(&fixture.lock)==0);
    /* The worker observed empty before this accepted commit. Close publishes
     * afterwards. It must re-read pending instead of discarding the frame. */
    display_worker_lease_t frame=acquire_frame(worker,0x6cU);
    expect_frame(&fixture,&frame);
    assert(display_worker_commit(worker,&frame,1000U,77U)==DW_OK);
    assert(display_worker_stop(&worker,0U)==DW_TIMEOUT&&worker&&fixture.frees==0U);
    assert(pthread_mutex_lock(&fixture.lock)==0);
    fixture.hold_idle=false;
    assert(pthread_cond_broadcast(&fixture.changed)==0);
    assert(pthread_mutex_unlock(&fixture.lock)==0);
    assert(display_worker_stop(&worker,1000U)==DW_OK&&!worker);
    assert(fixture.started==1U&&fixture.finished==1U&&fixture.frees==2U&&fixture.joins==2U);
    destroy(&fixture);
}
int main(void)
{
    construction_failure_test();
    lease_and_timeout_test();
    failure_latches_and_joins_test();
    final_commit_close_race_test();
    puts("Display worker immutable leases, failure/timeout ownership and close drain passed");
    return 0;
}
