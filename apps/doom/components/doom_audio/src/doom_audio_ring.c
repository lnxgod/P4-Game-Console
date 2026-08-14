// SPDX-License-Identifier: GPL-2.0-or-later

#include "doom/audio_ring.h"

#include <stddef.h>
#include <string.h>

_Static_assert(
    (DOOM_AUDIO_COMMAND_RING_CAPACITY &
     (DOOM_AUDIO_COMMAND_RING_CAPACITY - 1)) == 0,
    "Doom audio command ring capacity must be a power of two"
);

void doom_audio_command_ring_init(doom_audio_command_ring_t *ring)
{
    if (ring == NULL) {
        return;
    }
    memset(ring->commands, 0, sizeof(ring->commands));
    atomic_init(&ring->write_index, UINT32_C(0));
    atomic_init(&ring->read_index, UINT32_C(0));
}

bool doom_audio_command_ring_push(doom_audio_command_ring_t *ring,
                                  const doom_audio_command_t *command)
{
    if (ring == NULL || command == NULL) {
        return false;
    }
    const uint32_t write_index =
        atomic_load_explicit(&ring->write_index, memory_order_relaxed);
    const uint32_t read_index =
        atomic_load_explicit(&ring->read_index, memory_order_acquire);
    if ((uint32_t)(write_index - read_index) >=
        (uint32_t)DOOM_AUDIO_COMMAND_RING_CAPACITY) {
        return false;
    }
    ring->commands[write_index &
                   (uint32_t)(DOOM_AUDIO_COMMAND_RING_CAPACITY - 1)] = *command;
    atomic_store_explicit(
        &ring->write_index, write_index + UINT32_C(1), memory_order_release);
    return true;
}

bool doom_audio_command_ring_pop(doom_audio_command_ring_t *ring,
                                 doom_audio_command_t *out_command)
{
    if (ring == NULL || out_command == NULL) {
        return false;
    }
    const uint32_t read_index =
        atomic_load_explicit(&ring->read_index, memory_order_relaxed);
    const uint32_t write_index =
        atomic_load_explicit(&ring->write_index, memory_order_acquire);
    if (read_index == write_index) {
        return false;
    }
    *out_command = ring->commands[
        read_index & (uint32_t)(DOOM_AUDIO_COMMAND_RING_CAPACITY - 1)];
    atomic_store_explicit(
        &ring->read_index, read_index + UINT32_C(1), memory_order_release);
    return true;
}
