#include "platform_touch_sample_queue.h"
#include <limits.h>
#include <string.h>

static void invalid_frame(platform_touch_frame_t *frame)
{
    platform_touch_frame_neutral(frame);
    if (frame) frame->valid=0U;
}
static bool fresh(int64_t now,int64_t then)
{
    return now>=then && then>=0 &&
        (uint64_t)(now-then)<=PLATFORM_TOUCH_SAMPLER_MAX_AGE_US;
}
static bool valid_sample(const platform_touch_frame_t *f,int64_t completed)
{
    if (!f || f->version!=PLATFORM_TOUCH_VERSION || f->size!=sizeof(*f) ||
        f->valid!=1U || f->contact_count>PLATFORM_TOUCH_MAX_CONTACTS ||
        completed<0 || (f->contact_count &&
            (f->timestamp_us<=0 || f->timestamp_us>completed))) return false;
    for (uint8_t i=0;i<f->contact_count;++i) {
        if (f->contacts[i].x>=PLATFORM_TOUCH_WIDTH ||
            f->contacts[i].y>=PLATFORM_TOUCH_HEIGHT) return false;
    }
    return true;
}
static bool same_contacts(const platform_touch_frame_t *a,
                          const platform_touch_frame_t *b)
{
    return a->valid==b->valid && a->contact_count==b->contact_count &&
        memcmp(a->contacts,b->contacts,
               (size_t)a->contact_count*sizeof(a->contacts[0]))==0;
}
static void increment(uint32_t *value)
{
    if (*value!=UINT32_MAX) ++*value;
}
static void begin_producer_stall(platform_touch_sample_queue_t *q)
{
    /* One episode and one neutral boundary, regardless of consumer cadence. */
    q->count=0U;q->read_index=0U;q->producer_stalled=true;
    increment(&q->stats.producer_stalls);
    increment(&q->stats.stale_neutralizations);
}
static void append(platform_touch_sample_queue_t *q,
                   const platform_touch_frame_t *f,int64_t completed)
{
    const unsigned index=((unsigned)q->read_index+q->count)%
        PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY;
    q->pending[index]=(platform_touch_sample_t){*f,completed};
    ++q->count;
}
void platform_touch_sample_queue_init(platform_touch_sample_queue_t *q,
                                     int64_t now)
{
    if (!q) return;
    memset(q,0,sizeof(*q));
    q->started_us=now;
    platform_touch_frame_neutral(&q->delivered);
}
esp_err_t platform_touch_sample_queue_publish(platform_touch_sample_queue_t *q,
    const platform_touch_frame_t *f,esp_err_t result,int64_t completed)
{
    if (!q) return ESP_ERR_INVALID_ARG;
    if (q->stats.fault!=ESP_OK) return q->stats.fault;
    if (q->stats.samples!=UINT64_MAX) ++q->stats.samples;
    if (result==ESP_OK && !valid_sample(f,completed))
        result=ESP_ERR_INVALID_RESPONSE;
    if (result!=ESP_OK) {
        q->count=0U;q->read_index=0U;q->stats.fault=result;
        invalid_frame(&q->delivered);
        return result;
    }
    const int64_t health=q->have_sample?q->stats.last_completed_us:q->started_us;
    /* A resumed producer can publish before the consumer notices its gap.
     * Keep an explicit neutral fence ahead of that current sample; otherwise
     * a new timestamp could silently preserve a key held across the stall. */
    const bool fence=!q->producer_stalled && completed>=health && health>=0 &&
        (uint64_t)(completed-health)>PLATFORM_TOUCH_SAMPLER_MAX_AGE_US;
    if (fence) {
        begin_producer_stall(q);
        platform_touch_frame_t neutral;
        platform_touch_frame_neutral(&neutral);
        append(q,&neutral,completed);
    }
    if (q->producer_stalled) {
        increment(&q->stats.recoveries);
        q->producer_stalled=false;
    }
    q->have_sample=true;
    q->stats.last_completed_us=completed;
    if (fence) {
        /* Even a current neutral sample follows the boundary as a distinct
         * report. Later publications may coalesce only that current report. */
        append(q,f,completed);
        return ESP_OK;
    }
    if (q->count) {
        const unsigned last=((unsigned)q->read_index+q->count-1U)%
            PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY;
        if (same_contacts(&q->pending[last].frame,f)) {
            q->pending[last]=(platform_touch_sample_t){*f,completed};
            return ESP_OK;
        }
    }
    if (q->count==PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY) {
        /* Losing history must release the prior state before any current
         * contact is reapplied. Never silently overwrite a release. */
        q->count=0U;q->read_index=0U;increment(&q->stats.overflows);
        platform_touch_frame_t neutral;
        platform_touch_frame_neutral(&neutral);
        append(q,&neutral,completed);
        if (f->contact_count==0U) return ESP_OK;
    }
    append(q,f,completed);
    return ESP_OK;
}
esp_err_t platform_touch_sample_queue_take(platform_touch_sample_queue_t *q,
    int64_t now,platform_touch_frame_t *f,bool *available)
{
    if (available) *available=false;
    invalid_frame(f);
    if (!q || !f || !available) return ESP_ERR_INVALID_ARG;
    if (q->stats.fault!=ESP_OK) {
        *available=true;
        return q->stats.fault;
    }
    const int64_t health=q->have_sample?q->stats.last_completed_us:q->started_us;
    if (now<health || health<0) {
        q->stats.fault=ESP_ERR_TIMEOUT;q->count=0U;q->read_index=0U;
        invalid_frame(&q->delivered);*available=true;
        return ESP_ERR_TIMEOUT;
    }
    if (!fresh(now,health)) {
        platform_touch_frame_neutral(f);
        if (!q->producer_stalled) {
            begin_producer_stall(q);
            q->delivered=*f;*available=true;
        }
        /* Scheduling delay is not an I2C fault. The same claimed worker may
         * publish again; do not poison its queue or request lifecycle work. */
        return ESP_OK;
    }
    if (q->count) {
        const platform_touch_sample_t next=q->pending[q->read_index];
        if (!fresh(now,next.completed_us) || (next.frame.contact_count &&
                !fresh(now,next.frame.timestamp_us))) {
            /* An old queued press must never be replayed after a long engine
             * pause. Drop history and neutralize; the next producer sample
             * can establish current input again. */
            q->count=0U;q->read_index=0U;
            increment(&q->stats.stale_neutralizations);
            platform_touch_frame_neutral(f);
        } else {
            *f=next.frame;
            q->read_index=(uint8_t)(((unsigned)q->read_index+1U)%
                PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY);
            --q->count;
        }
        q->delivered=*f;*available=true;
        return ESP_OK;
    }
    if (q->delivered.contact_count &&
        !fresh(now,q->delivered.timestamp_us)) {
        platform_touch_frame_neutral(f);q->delivered=*f;
        increment(&q->stats.stale_neutralizations);*available=true;
        return ESP_OK;
    }
    platform_touch_frame_neutral(f);
    return ESP_OK;
}
void platform_touch_sample_queue_discard(platform_touch_sample_queue_t *q)
{
    if (!q) return;
    q->read_index=0U;q->count=0U;
    platform_touch_frame_neutral(&q->delivered);
}
void platform_touch_sample_queue_stats(const platform_touch_sample_queue_t *q,
                                      platform_touch_sampler_stats_t *out)
{
    if (q && out) *out=q->stats;
}
