// SPDX-License-Identifier: MIT

#ifndef P4_BBS_UI_H
#define P4_BBS_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/ansi.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_BBS_VISIBLE_DOORS = 6,
    P4_BBS_DOOR_TITLE_BYTES = 17,
    P4_BBS_DOOR_SUBTITLE_BYTES = 21,
    P4_BBS_BOARD_NAME_BYTES = 33,
    P4_BBS_STATUS_BYTES = 25,
    P4_BBS_BOOT_STATUS_BYTES = 33,
    P4_BBS_BOOT_DETAIL_BYTES = 49,
};

typedef struct {
    uint16_t number;
    char title[P4_BBS_DOOR_TITLE_BYTES];
    char subtitle[P4_BBS_DOOR_SUBTITLE_BYTES];
    uint8_t accent;
    bool enabled;
    bool menu;
} p4_bbs_door_t;

typedef struct {
    char board_name[P4_BBS_BOARD_NAME_BYTES];
    char section[P4_BBS_STATUS_BYTES];
    char connection[P4_BBS_STATUS_BYTES];
    p4_bbs_door_t doors[P4_BBS_VISIBLE_DOORS];
    size_t door_count;
    size_t selected_door;
    uint16_t page;
    uint16_t page_count;
    uint16_t uploads;
    uint16_t trades;
    bool local_board;
    bool can_go_up;
} p4_bbs_launcher_model_t;

typedef enum {
    P4_BBS_HIT_NONE = 0,
    P4_BBS_HIT_DOOR,
    P4_BBS_HIT_BACK,
    P4_BBS_HIT_PAGE_PREVIOUS,
    P4_BBS_HIT_PAGE_NEXT,
} p4_bbs_hit_kind_t;

typedef struct {
    p4_bbs_hit_kind_t kind;
    size_t door_index;
} p4_bbs_hit_t;

typedef enum {
    P4_BBS_BOOT_POST = 0,
    P4_BBS_BOOT_DISK,
    P4_BBS_BOOT_DIALING,
    P4_BBS_BOOT_TRAINING,
    P4_BBS_BOOT_SYNCING,
    P4_BBS_BOOT_CONNECTED,
    P4_BBS_BOOT_DEGRADED,
} p4_bbs_boot_phase_t;

typedef struct {
    p4_bbs_boot_phase_t phase;
    char status[P4_BBS_BOOT_STATUS_BYTES];
    char detail[P4_BBS_BOOT_DETAIL_BYTES];
    uint8_t progress_step;
    uint8_t progress_total;
} p4_bbs_boot_model_t;

/**
 * Build a complete 80x30 launcher by emitting CP437 plus ECMA-48 sequences
 * through the bounded terminal parser. No model string is interpreted as
 * ANSI; non-printable bytes are replaced with '?'.
 */
bool p4_bbs_build_launcher(p4_ansi_terminal_t *terminal,
                           const p4_bbs_launcher_model_t *model);

/** Map native 768x480 pointer coordinates to the launcher controls. */
p4_bbs_hit_t p4_bbs_hit_test(const p4_bbs_launcher_model_t *model,
                             uint16_t surface_x,
                             uint16_t surface_y);

/** Build the ANSI logo and BBS connection sequence used during startup. */
bool p4_bbs_build_boot_screen(p4_ansi_terminal_t *terminal,
                              const p4_bbs_boot_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
