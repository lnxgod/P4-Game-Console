// SPDX-License-Identifier: MIT
#ifndef DOOM_GAMEPAD_KEY_MERGE_H
#define DOOM_GAMEPAD_KEY_MERGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOOM_KEY_SOURCE_GAMEPAD = 0,
    DOOM_KEY_SOURCE_TOUCH,
    DOOM_KEY_SOURCE_COUNT,
} doom_key_source_t;

typedef struct {
    uint8_t owners[256];
} doom_key_merge_t;

/* Reset between engine sessions, before any input is delivered. */
void doom_key_merge_init(doom_key_merge_t *input);

/* Apply a mapped key transition from one source. Return true only when the
 * combined key state changes; then deliver this key/pressed pair to Doom.
 * Releases from one source cannot cancel a key still held by the other.
 * Duplicate events and invalid sources produce no engine event. */
bool doom_key_merge_update(doom_key_merge_t *input, doom_key_source_t source,
                           uint8_t key, bool pressed);

#ifdef __cplusplus
}
#endif
#endif
