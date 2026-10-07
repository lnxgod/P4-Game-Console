// SPDX-License-Identifier: MIT
#ifndef P4_VERIFIED_WAD_H
#define P4_VERIFIED_WAD_H
#include "verified_reader.h"
/* Call only after exact whole-file identity admission. No untrusted allocation. */
typedef enum { P4_WAD_IWAD, P4_WAD_PURE_HADES, P4_WAD_DWANGO5 } p4_wad_kind_t;
bool p4_verified_wad_validate(p4_verified_reader_t *reader, p4_wad_kind_t kind);
/* Progress runs after each underlying block read on the caller's task. The
 * reader is exclusively borrowed and its read/context fields are restored
 * before return, including failed validation. Progress must not use reader. */
bool p4_verified_wad_validate_with_progress(
    p4_verified_reader_t *reader, p4_wad_kind_t kind,
    void (*progress)(void *), void *context);
#endif
