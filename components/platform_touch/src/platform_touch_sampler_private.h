#ifndef PLATFORM_TOUCH_SAMPLER_PRIVATE_H
#define PLATFORM_TOUCH_SAMPLER_PRIVATE_H
#include "platform/touch.h"
/* Lifetime calls are externally serialized. A successful claim excludes raw
 * consumer polling and destruction until the worker is proven quiescent. */
esp_err_t platform_touch_sampler_claim(platform_touch_t *touch);
esp_err_t platform_touch_sampler_release(platform_touch_t *touch);
esp_err_t platform_touch_poll_sampled(platform_touch_t *touch,
                                      platform_touch_frame_t *frame);
#endif
