#include "diag_mailbox.h"
#include <limits.h>
#include <string.h>

static void increment_saturated(uint32_t *value)
{ if (*value < UINT32_MAX) ++*value; }

void p4_diag_init(p4_diag_mailbox_t *mailbox)
{
    memset(mailbox, 0, sizeof(*mailbox));
    atomic_init(&mailbox->write_ticket, 0U);
    atomic_init(&mailbox->read_ticket, 0U);
    atomic_init(&mailbox->active_generation, 0U);
}

bool p4_diag_begin_generation(p4_diag_mailbox_t *mailbox)
{
    if (mailbox->reserved || mailbox->generation_counter == UINT32_MAX ||
        atomic_load_explicit(&mailbox->active_generation,
                             memory_order_relaxed) != 0U) return false;
    ++mailbox->generation_counter;
    atomic_store_explicit(&mailbox->active_generation,
                          mailbox->generation_counter, memory_order_release);
    return true;
}

void p4_diag_end_generation(p4_diag_mailbox_t *mailbox)
{
    /* Owner cancels an unpublished reservation; never touches consumer data. */
    mailbox->reserved = false;
    atomic_store_explicit(&mailbox->active_generation, 0U, memory_order_release);
}

bool p4_diag_try_reserve(p4_diag_mailbox_t *mailbox,
                         p4_diag_reservation_t *reservation)
{
    if (!reservation) return false;
    if (mailbox->reserved) {
        increment_saturated(&mailbox->rejected_reentry);
        return false;
    }
    const uint32_t generation = atomic_load_explicit(
        &mailbox->active_generation, memory_order_relaxed);
    if (generation == 0U || mailbox->sequence == UINT64_MAX) return false;
    const unsigned write = atomic_load_explicit(&mailbox->write_ticket,
                                                memory_order_relaxed);
    const unsigned read = atomic_load_explicit(&mailbox->read_ticket,
                                               memory_order_acquire);
    if (write - read >= P4_DIAG_SLOTS) {
        increment_saturated(&mailbox->dropped_full);
        return false;
    }
    mailbox->reserved = true;
    *reservation = (p4_diag_reservation_t){write, generation, ++mailbox->sequence};
    return true;
}

static bool reservation_valid(const p4_diag_mailbox_t *mailbox,
                               p4_diag_reservation_t reservation)
{
    return mailbox->reserved && reservation.sequence == mailbox->sequence &&
        reservation.ticket == atomic_load_explicit(
        &mailbox->write_ticket, memory_order_relaxed) &&
        reservation.generation == atomic_load_explicit(
        &mailbox->active_generation, memory_order_relaxed);
}

bool p4_diag_publish(p4_diag_mailbox_t *mailbox,
                     p4_diag_reservation_t reservation,
                     uint64_t captured_us, const void *payload, size_t bytes)
{
    if (!reservation_valid(mailbox, reservation)) return false;
    if (!payload || bytes == 0U || bytes > P4_DIAG_PAYLOAD_BYTES) {
        mailbox->reserved = false;
        return false;
    }
    p4_diag_record_t *const record =
        &mailbox->records[reservation.ticket % P4_DIAG_SLOTS];
    *record = (p4_diag_record_t){
        .captured_us = captured_us, .sequence = reservation.sequence,
        .generation = reservation.generation,
        .dropped_full = mailbox->dropped_full,
        .payload_bytes = (uint32_t)bytes,
    };
    memcpy(record->payload, payload, bytes);
    mailbox->reserved = false;
    atomic_store_explicit(&mailbox->write_ticket,
                          reservation.ticket + 1U, memory_order_release);
    return true;
}

void p4_diag_cancel(p4_diag_mailbox_t *mailbox,
                    p4_diag_reservation_t reservation)
{
    if (reservation_valid(mailbox, reservation)) mailbox->reserved = false;
}

bool p4_diag_try_take(p4_diag_mailbox_t *mailbox, p4_diag_record_t *out)
{
    if (!out) return false;
    const unsigned read = atomic_load_explicit(&mailbox->read_ticket,
                                               memory_order_relaxed);
    const unsigned write = atomic_load_explicit(&mailbox->write_ticket,
                                                memory_order_acquire);
    if (read == write) return false;
    *out = mailbox->records[read % P4_DIAG_SLOTS];
    atomic_store_explicit(&mailbox->read_ticket, read + 1U,
                          memory_order_release);
    return true;
}

bool p4_diag_is_current(const p4_diag_mailbox_t *mailbox,
                        const p4_diag_record_t *record)
{
    return record && record->generation != 0U && record->generation ==
        atomic_load_explicit(&mailbox->active_generation, memory_order_acquire);
}
