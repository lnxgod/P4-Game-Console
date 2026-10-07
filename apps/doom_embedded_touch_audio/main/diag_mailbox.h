/* Offline candidate only. Single owner/producer, single consumer; not ISR API. */
#ifndef P4_DIAG_MAILBOX_H
#define P4_DIAG_MAILBOX_H
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { P4_DIAG_SLOTS = 2, P4_DIAG_PAYLOAD_BYTES = 2048 };
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "32-bit mailbox atomics must be lock-free");
_Static_assert(sizeof(unsigned) == sizeof(uint32_t), "32-bit ticket arithmetic");

typedef struct {
    uint64_t captured_us;
    uint64_t sequence;
    uint32_t generation;
    uint32_t dropped_full;
    uint32_t payload_bytes;
    unsigned char payload[P4_DIAG_PAYLOAD_BYTES];
} p4_diag_record_t;

typedef struct {
    atomic_uint write_ticket;
    atomic_uint read_ticket;
    atomic_uint active_generation;
    p4_diag_record_t records[P4_DIAG_SLOTS];
    /* Remaining fields belong exclusively to the producer. */
    uint64_t sequence;
    uint32_t generation_counter;
    uint32_t dropped_full;
    uint32_t rejected_reentry;
    bool reserved;
} p4_diag_mailbox_t;

typedef struct {
    unsigned ticket;
    uint32_t generation;
    uint64_t sequence; /* Unique even when a reservation is cancelled. */
} p4_diag_reservation_t;

/* Initialize once, before consumer creation. Never reset a running mailbox. */
void p4_diag_init(p4_diag_mailbox_t *mailbox);
/* Called by the sole engine owner at visit begin/end, never by consumer. */
bool p4_diag_begin_generation(p4_diag_mailbox_t *mailbox);
void p4_diag_end_generation(p4_diag_mailbox_t *mailbox);
/* Reserve before collectors. Full/reentrant/inactive => no collection. */
bool p4_diag_try_reserve(p4_diag_mailbox_t *mailbox,
                         p4_diag_reservation_t *reservation);
/* Copy only scalar-value payloads; no borrowed pointers, callbacks or handles. */
bool p4_diag_publish(p4_diag_mailbox_t *mailbox,
                     p4_diag_reservation_t reservation,
                     uint64_t captured_us, const void *payload, size_t bytes);
void p4_diag_cancel(p4_diag_mailbox_t *mailbox,
                    p4_diag_reservation_t reservation);
/* Copies a record then releases its slot BEFORE formatting/output. */
bool p4_diag_try_take(p4_diag_mailbox_t *mailbox, p4_diag_record_t *out);
/* Use before each output line. In-flight old lines retain their old tag. */
bool p4_diag_is_current(const p4_diag_mailbox_t *mailbox,
                        const p4_diag_record_t *record);
#endif
