// SPDX-License-Identifier: MIT
#ifndef P4_HOST_FRAME_PACER_H
#define P4_HOST_FRAME_PACER_H
#include <stdint.h>
typedef struct { uint64_t deadline_ns; unsigned remainder; } p4_host_pacer_t;
static inline uint64_t p4_host_pacer_next(p4_host_pacer_t *p, uint64_t now_ns)
{
    // Exact 60 Hz over 60 intervals; a late frame never triggers a catch-up burst.
    p->deadline_ns += UINT64_C(16666666);
    p->remainder += 40U;
    if (p->remainder >= 60U) { ++p->deadline_ns; p->remainder -= 60U; }
    if (p->deadline_ns < now_ns) p->deadline_ns = now_ns;
    return p->deadline_ns;
}
#endif
