// SPDX-License-Identifier: MIT
#ifndef P4_HOST_MOUSE_H
#define P4_HOST_MOUSE_H

#include "p4/input.h"

/* Preserve gesture edges even when SDL delivers a whole drag between frames.
 * Adjacent moves coalesce; edges drain on ordinary service ticks, so neither
 * simulation time nor audio time is invented to replay a desktop gesture. */
enum { P4_HOST_MOUSE_CAPACITY = 16 };
typedef enum {
    P4_HOST_MOUSE_PRESS, P4_HOST_MOUSE_MOVE, P4_HOST_MOUSE_RELEASE,
    P4_HOST_MOUSE_CANCEL
} p4_host_mouse_kind_t;
typedef struct {
    p4_physical_touch_t point;
    p4_host_mouse_kind_t kind;
    bool valid, down;
} p4_host_mouse_sample_t;
typedef struct {
    p4_host_mouse_sample_t queue[P4_HOST_MOUSE_CAPACITY];
    p4_host_mouse_sample_t current;
    p4_physical_touch_t last_point;
    unsigned head, count;
    bool collecting_down;
} p4_host_mouse_t;

static inline void p4_host_mouse_cancel(p4_host_mouse_t *mouse)
{
    *mouse = (p4_host_mouse_t){
        .queue = {{.kind = P4_HOST_MOUSE_CANCEL}}, .count = 1U};
}
static inline bool p4_host_mouse_push(p4_host_mouse_t *mouse,
                                      p4_host_mouse_sample_t sample)
{
    if (mouse->count > 0U && sample.kind == P4_HOST_MOUSE_MOVE) {
        const unsigned last = (mouse->head + mouse->count - 1U) % P4_HOST_MOUSE_CAPACITY;
        if (mouse->queue[last].kind == P4_HOST_MOUSE_MOVE) {
            mouse->queue[last] = sample;
            return true;
        }
    }
    if (mouse->count == P4_HOST_MOUSE_CAPACITY) {
        /* Overflow fails closed: never drop an up edge and leave touch held. */
        p4_host_mouse_cancel(mouse);
        return false;
    }
    mouse->queue[(mouse->head + mouse->count) % P4_HOST_MOUSE_CAPACITY] = sample;
    ++mouse->count;
    return true;
}
static inline void p4_host_mouse_press(p4_host_mouse_t *mouse, p4_physical_touch_t point)
{
    if (mouse->collecting_down) p4_host_mouse_cancel(mouse);
    mouse->last_point = point;
    mouse->collecting_down = p4_host_mouse_push(mouse,
        (p4_host_mouse_sample_t){.point = point, .kind = P4_HOST_MOUSE_PRESS,
                                 .valid = true, .down = true});
}
static inline void p4_host_mouse_move(p4_host_mouse_t *mouse, p4_physical_touch_t point)
{
    if (!mouse->collecting_down ||
        (point.x == mouse->last_point.x && point.y == mouse->last_point.y)) return;
    mouse->last_point = point;
    (void)p4_host_mouse_push(mouse,
        (p4_host_mouse_sample_t){.point = point, .kind = P4_HOST_MOUSE_MOVE,
                                 .valid = true, .down = true});
}
static inline void p4_host_mouse_release(p4_host_mouse_t *mouse)
{
    if (!mouse->collecting_down) return;
    /* A button-up is an edge, not a held-motion sample. Release the last
     * held position, matching the tablet's position-while-contact model. */
    mouse->collecting_down = false;
    (void)p4_host_mouse_push(mouse,
        (p4_host_mouse_sample_t){.point = mouse->last_point,
                                 .kind = P4_HOST_MOUSE_RELEASE, .valid = true});
}
static inline p4_host_mouse_sample_t p4_host_mouse_next(p4_host_mouse_t *mouse)
{
    if (mouse->count > 0U) {
        mouse->current = mouse->queue[mouse->head];
        mouse->head = (mouse->head + 1U) % P4_HOST_MOUSE_CAPACITY;
        --mouse->count;
    }
    return mouse->current;
}
#endif
