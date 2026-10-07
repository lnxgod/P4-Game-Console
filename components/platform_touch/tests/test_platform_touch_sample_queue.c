#include "platform_touch_sample_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Detects dropped short taps, stale held keys, replay after remote override,
 * overflow without a release fence, and malformed producer frames. */
static platform_touch_frame_t contact(uint16_t x, int64_t time)
{
    platform_touch_frame_t f;
    platform_touch_frame_neutral(&f);
    f.contact_count=1;f.timestamp_us=time;
    f.contacts[0].x=x;f.contacts[0].y=200;
    return f;
}
static platform_touch_frame_t take(platform_touch_sample_queue_t *q,int64_t now)
{
    platform_touch_frame_t f;bool available=false;
    assert(platform_touch_sample_queue_take(q,now,&f,&available)==ESP_OK);
    assert(available && f.valid);
    return f;
}
int main(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_frame_t f,neutral;
    platform_touch_frame_neutral(&neutral);
    bool available=true;
    platform_touch_sample_queue_init(&q,1000);
    assert(platform_touch_sample_queue_take(&q,1000,&f,&available)==ESP_OK);
    assert(!available && f.contact_count==0);

    /* A complete tap between two 30 Hz engine polls remains two transitions. */
    f=contact(100,10000);
    assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,11000)==ESP_OK);
    assert(platform_touch_sample_queue_publish(&q,&neutral,ESP_OK,27000)==ESP_OK);
    f=take(&q,33000);assert(f.contact_count==1 && f.contacts[0].x==100);
    f=take(&q,33000);assert(f.contact_count==0);
    assert(platform_touch_sample_queue_take(&q,33000,&f,&available)==ESP_OK && !available);

    /* Unchanged queued reports coalesce, preserving the fresh timestamp. */
    f=contact(110,35000);assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,36000)==ESP_OK);
    f.timestamp_us=50000;assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,51000)==ESP_OK);
    f=take(&q,60000);assert(f.timestamp_us==50000 && f.contacts[0].x==110);
    assert(platform_touch_sample_queue_take(&q,60000,&f,&available)==ESP_OK && !available);

    /* A functioning GT911 may repeat an old acquisition: do not refresh it. */
    f=contact(110,50000);assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,100000)==ESP_OK);
    f=take(&q,100001);assert(f.contact_count==0);
    assert(q.stats.stale_neutralizations==1);
    assert(platform_touch_sample_queue_take(&q,100010,&f,&available)==ESP_OK && !available);

    /* Worker publication stalling is separate from contact freshness. */
    assert(platform_touch_sample_queue_take(&q,150001,&f,&available)==ESP_OK);
    assert(available && f.valid && !f.contact_count && q.stats.fault==ESP_OK);
    assert(q.stats.producer_stalls==1 && q.stats.recoveries==0);
    assert(platform_touch_sample_queue_take(&q,150002,&f,&available)==ESP_OK && !available);
    f=contact(112,151000);
    assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,152000)==ESP_OK);
    f=take(&q,153000);assert(f.contact_count==1 && f.contacts[0].x==112);
    assert(q.stats.producer_stalls==1 && q.stats.recoveries==1);

    /* Overflow must release before accepting a fresh current contact. */
    platform_touch_sample_queue_init(&q,200000);
    for (unsigned i=0;i<9;++i) {
        f=contact((uint16_t)(100+i),200001+(int64_t)i);
        assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,200001+(int64_t)i)==ESP_OK);
    }
    assert(q.stats.overflows==1);
    f=take(&q,200010);assert(f.contact_count==0);
    f=take(&q,200010);assert(f.contact_count==1 && f.contacts[0].x==108);

    /* Remote override cannot replay physical taps that happened underneath it. */
    f=contact(120,210000);assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,210001)==ESP_OK);
    platform_touch_sample_queue_discard(&q);
    assert(platform_touch_sample_queue_take(&q,210002,&f,&available)==ESP_OK && !available);
    f=contact(130,220000);assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,220001)==ESP_OK);
    f=take(&q,220002);assert(f.contacts[0].x==130);

    /* Device failure releases the held key and remains a fault until restart. */
    assert(platform_touch_sample_queue_publish(&q,NULL,ESP_ERR_TIMEOUT,221000)==ESP_ERR_TIMEOUT);
    assert(platform_touch_sample_queue_take(&q,221001,&f,&available)==ESP_ERR_TIMEOUT);
    assert(available && !f.valid && f.contact_count==0);

    /* Invalid geometry and future acquisition times fail closed. */
    platform_touch_sample_queue_init(&q,300000);
    f=contact(PLATFORM_TOUCH_WIDTH,300001);
    assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,300002)==ESP_ERR_INVALID_RESPONSE);
    platform_touch_sample_queue_init(&q,300000);
    f=contact(100,300004);
    assert(platform_touch_sample_queue_publish(&q,&f,ESP_OK,300002)==ESP_ERR_INVALID_RESPONSE);
    assert(platform_touch_sample_queue_take(&q,300003,&f,&available)==ESP_ERR_INVALID_RESPONSE);
    assert(!f.valid && !f.contact_count);
    puts("touch sample queue: transition, freshness, overflow and override contracts passed");
    return 0;
}
