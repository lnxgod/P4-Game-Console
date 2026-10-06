// SPDX-License-Identifier: MIT
#include "doom_gamepad/key_merge.h"
#include <stddef.h>
#include <string.h>

_Static_assert(DOOM_KEY_SOURCE_COUNT <= 8, "source mask must fit one byte");

void doom_key_merge_init(doom_key_merge_t *input)
{
    if (input != NULL) memset(input, 0, sizeof(*input));
}

bool doom_key_merge_update(doom_key_merge_t *input, doom_key_source_t source,
                           uint8_t key, bool pressed)
{
    if (input == NULL || (unsigned)source >= DOOM_KEY_SOURCE_COUNT) return false;
    const bool was_pressed = input->owners[key] != 0U;
    const uint8_t mask = (uint8_t)(1U << (unsigned)source);
    if (pressed) {
        input->owners[key] |= mask;
    } else {
        input->owners[key] &= (uint8_t)~mask;
    }
    return was_pressed != (input->owners[key] != 0U);
}
