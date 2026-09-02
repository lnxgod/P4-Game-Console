// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/game.h"

#include "byte_buddy_internal.h"
#include "byte_buddy_save.h"

extern const p4_game_descriptor_t p4_byte_buddy_game;

typedef struct {
    p4_game_signal_snapshot_t snapshot;
    uint32_t requests;
    uint8_t batch;
    int8_t focused_rssi_dbm;
    uint8_t mode;
    uint64_t first_token_override;
    uint8_t first_channel_override;
    uint8_t first_flags_override;
} preview_signal_scan_t;

typedef enum {
    PREVIEW_SCAN_READY = 0,
    PREVIEW_SCAN_SCANNING,
    PREVIEW_SCAN_BUSY,
    PREVIEW_SCAN_OFFLINE,
    PREVIEW_SCAN_EMPTY,
    PREVIEW_SCAN_MOVED,
} preview_scan_mode_t;

typedef struct {
    const char *prefix;
    unsigned frame_count;
    uint64_t attack_hashes[BYTE_BUDDY_SIGNAL_ATTACK_COUNT];
    bool attack_hash_valid[BYTE_BUDDY_SIGNAL_ATTACK_COUNT];
    bool require_semantic_digests;
    bool print_semantic_digests;
} preview_motion_writer_t;

typedef struct {
    uint16_t left;
    uint16_t top;
    uint16_t right;
    uint16_t bottom;
} preview_roi_t;

typedef struct {
    uint8_t payload[BYTE_BUDDY_SAVE_PAYLOAD_BYTES];
    p4_game_save_ticket_t ticket;
    uint32_t committed_sequence;
} preview_save_host_t;

enum {
    PREVIEW_SIGNAL_INTRO_MS = 450,
    PREVIEW_SIGNAL_TRAVEL_MS = 220,
    PREVIEW_SIGNAL_IMPACT_MS = 280,
    PREVIEW_SIGNAL_RECOVERY_MS = 420,
    PREVIEW_SIGNAL_VICTORY_MS = 900,
    PREVIEW_SIGNAL_DEFEAT_MS = 760,
    PREVIEW_SIGNAL_RETREAT_REMAINING_MS = 504,
    PREVIEW_SIGNAL_STRIKE_WAIT_MS = 400,
    PREVIEW_SIGNAL_PASSIVE_FX_MS = 520,
    PREVIEW_SIGNAL_HP_DEFEAT_CUTOFF_MS = 11000,
    PREVIEW_SIGNAL_MOTION_MAX_FRAMES = 1200,
    PREVIEW_LEGACY_FOCUS_RSSI_DBM = -65,
    PREVIEW_BUTTON_RELEASE_MS = 16,
    PREVIEW_SCENE_TRANSITION_MS = 240,
    PREVIEW_SIGNAL_ACTIVE_AFTER_TRANSITION_MS =
        PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS -
        PREVIEW_SCENE_TRANSITION_MS + 1,
    PREVIEW_CARE_NEED_REFRESH_MS = 10000,
    PREVIEW_CARE_VARIANT_COUNT = 4,
    /* A tap's release update consumes 16 ms of the 1.4 s action clip. */
    PREVIEW_PLAY_ACTION_AFTER_TAP_MS = 1384,
    PREVIEW_PLAY_EVOLUTION_TAIL_MS = 1800,
    PREVIEW_EVOLUTION_AFTER_TAP_MS = 1368,
    PREVIEW_ATLAS_HEADER_BYTES = 64,
    PREVIEW_ATLAS_FRAME_WIDTH = 64,
    PREVIEW_ATLAS_FRAME_HEIGHT = 64,
    PREVIEW_ATLAS_FRAMES_PER_SHEET = 16,
    PREVIEW_ATLAS_PALETTE_ENTRIES = 16,
    PREVIEW_ATLAS_PALETTE_BYTES = 32,
    PREVIEW_ATLAS_PACKED_FRAME_BYTES = 2048,
    PREVIEW_ATLAS_EXPECTED_SHEETS = 42,
    PREVIEW_ATLAS_EXPECTED_FRAMES = 672,
    PREVIEW_ATLAS_EXPECTED_BYTES = 1397824,
    PREVIEW_V18_SHEET = 29,
    PREVIEW_COMPLETION_FIRST_SHEET = 30,
    PREVIEW_COMPLETION_SHEET_COUNT = 12,
    PREVIEW_COMPLETION_FRAME_COUNT = 192,
    PREVIEW_COMPLETION_ACTION_COUNT = 7,
    PREVIEW_COMPLETION_STAGE_COUNT = 4,
    PREVIEW_COMPLETION_PHASE_COUNT = 4,
    PREVIEW_COMPLETION_HATCH_VARIANT_COUNT = 4,
    PREVIEW_COMPLETION_HATCH_MILESTONE_COUNT = 4,
    PREVIEW_COMPLETION_CONTACT_COLUMNS = 4,
    PREVIEW_COMPLETION_CONTACT_ROWS = 3,
    PREVIEW_RUNTIME_ACTION_COUNT = 7,
    PREVIEW_RUNTIME_STAGE_COUNT = 4,
    PREVIEW_RUNTIME_PHASE_COUNT = 4,
    PREVIEW_RUNTIME_HATCH_MORPH_COUNT = 4,
    PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT = 4,
    PREVIEW_RUNTIME_SIGNAL_LIST_FRAMES = PREVIEW_RUNTIME_STAGE_COUNT,
    PREVIEW_RUNTIME_ACTION_FRAMES =
        PREVIEW_RUNTIME_ACTION_COUNT * PREVIEW_RUNTIME_STAGE_COUNT *
            PREVIEW_RUNTIME_PHASE_COUNT +
        PREVIEW_RUNTIME_SIGNAL_LIST_FRAMES,
    PREVIEW_RUNTIME_HATCH_FRAMES =
        PREVIEW_RUNTIME_HATCH_MORPH_COUNT *
        PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT *
        PREVIEW_RUNTIME_PHASE_COUNT,
    PREVIEW_RUNTIME_COMPLETION_FRAMES =
        PREVIEW_RUNTIME_ACTION_FRAMES + PREVIEW_RUNTIME_HATCH_FRAMES,
    PREVIEW_RUNTIME_PHASE_STEP_MS = 461,
    PREVIEW_RUNTIME_SIGNAL_LIST_HOLD_MS = 1600,
};

_Static_assert(
    PREVIEW_COMPLETION_SHEET_COUNT ==
        1 + PREVIEW_COMPLETION_ACTION_COUNT +
            PREVIEW_COMPLETION_HATCH_VARIANT_COUNT,
    "completion atlas semantic sheet partition changed");
_Static_assert(
    PREVIEW_COMPLETION_FRAME_COUNT ==
        PREVIEW_COMPLETION_SHEET_COUNT * PREVIEW_ATLAS_FRAMES_PER_SHEET,
    "completion preview frame budget changed");
_Static_assert(
    PREVIEW_COMPLETION_CONTACT_COLUMNS *
        PREVIEW_COMPLETION_CONTACT_ROWS ==
            PREVIEW_COMPLETION_SHEET_COUNT,
    "completion contact sheet no longer covers every sheet");
_Static_assert(
    PREVIEW_ATLAS_EXPECTED_BYTES == PREVIEW_ATLAS_HEADER_BYTES +
        PREVIEW_ATLAS_EXPECTED_FRAMES *
            (PREVIEW_ATLAS_PALETTE_BYTES +
             PREVIEW_ATLAS_PACKED_FRAME_BYTES),
    "completion atlas byte budget changed");
_Static_assert(
    PREVIEW_RUNTIME_ACTION_FRAMES == 116 &&
        PREVIEW_RUNTIME_HATCH_FRAMES == 64 &&
        PREVIEW_RUNTIME_COMPLETION_FRAMES == 180,
    "runtime-composited preview budget changed");

typedef enum {
    PREVIEW_SCREEN_SIGNAL_LIST = 0,
    PREVIEW_SCREEN_SIGNAL_TRACKER,
    PREVIEW_SCREEN_PULSE_ACTIVE,
    PREVIEW_SCREEN_WEAVE_ACTIVE,
    PREVIEW_SCREEN_SIGNAL_REWARD,
    PREVIEW_SCREEN_COUNT,
} preview_screen_signature_t;

static unsigned s_clip_frame_count;
static unsigned s_preview_care_index;

static uint64_t preview_signal_token(unsigned batch, unsigned index);
static uint64_t controlled_signal_token(
    uint8_t core, uint8_t halo, uint8_t sigil,
    uint8_t aura, uint8_t hue, uint8_t rarity);

static bool preview_request_signal(void *context, uint64_t focus_token)
{
    preview_signal_scan_t *const scan = context;
    if (scan == NULL) {
        return false;
    }
    ++scan->requests;
    if (scan->mode == PREVIEW_SCAN_BUSY) {
        return false;
    }
    scan->snapshot = (p4_game_signal_snapshot_t){
        .generation = scan->requests,
        .status = P4_GAME_SIGNAL_READY,
        .count = 8U,
        .results = {
            {.token = UINT64_C(0x00123456789abcde),
             .label = "SKY GARDEN",
             .rssi_dbm = -84,
             .channel = 1U, .flags = P4_GAME_SIGNAL_PROTECTED},
            {.token = UINT64_C(0x3ff0000000045678),
             .label = "LIBRARY MESH", .rssi_dbm = -72,
             .channel = 6U, .flags = P4_GAME_SIGNAL_PROTECTED},
            {.token = UINT64_C(0x0aa0000000789abc),
             .label = "MOONLIGHT", .rssi_dbm = -66,
             .channel = 11U, .flags = P4_GAME_SIGNAL_PROTECTED},
            {.token = UINT64_C(0x0880000000abcdef),
             .label = "STAR PORT", .rssi_dbm = -76,
             .channel = 36U, .flags = P4_GAME_SIGNAL_PROTECTED},
            {.token = UINT64_C(0x0440000002fedcba),
             .label = "HIDDEN SIGNAL", .rssi_dbm = -61,
             .channel = 44U, .flags = P4_GAME_SIGNAL_HIDDEN},
            {.token = UINT64_C(0x13579bdf2468ace0),
             .label = "COMET CAFE", .rssi_dbm = -58,
             .channel = 6U, .flags = P4_GAME_SIGNAL_PROTECTED},
            {.token = UINT64_C(0x0fedcba987654321),
             .label = "AURORA LAB", .rssi_dbm = -60,
             .channel = 11U, .flags = 0U},
            {.token = UINT64_C(0x55aa33cc77ee0011),
             .label = "DRAGON DEN", .rssi_dbm = -52,
             .channel = 149U, .flags = P4_GAME_SIGNAL_PROTECTED},
        },
    };
    if (scan->mode == PREVIEW_SCAN_SCANNING) {
        scan->snapshot.status = P4_GAME_SIGNAL_SCANNING;
        scan->snapshot.count = 0U;
        return true;
    }
    if (scan->mode == PREVIEW_SCAN_OFFLINE) {
        scan->snapshot.status = P4_GAME_SIGNAL_UNAVAILABLE;
        scan->snapshot.count = 0U;
        return true;
    }
    if (scan->mode == PREVIEW_SCAN_EMPTY) {
        scan->snapshot.count = 0U;
        return true;
    }
    for (size_t index = 0U; index < scan->snapshot.count; ++index) {
        scan->snapshot.results[index].token =
            preview_signal_token(scan->batch, (unsigned)index);
        scan->snapshot.results[index].flags |= P4_GAME_SIGNAL_SIMULATED;
        if (focus_token != 0U &&
            scan->snapshot.results[index].token == focus_token) {
            const p4_game_signal_t focused =
                scan->snapshot.results[index];
            scan->snapshot.results[index] = scan->snapshot.results[0];
            scan->snapshot.results[0] = focused;
            scan->snapshot.results[0].rssi_dbm =
                scan->focused_rssi_dbm == 0
                    ? -43 : scan->focused_rssi_dbm;
        }
    }
    if (scan->mode == PREVIEW_SCAN_MOVED && focus_token != 0U) {
        for (size_t index = 0U; index < scan->snapshot.count; ++index) {
            if (scan->snapshot.results[index].token != focus_token) {
                continue;
            }
            for (size_t move = index + 1U;
                 move < scan->snapshot.count; ++move) {
                scan->snapshot.results[move - 1U] =
                    scan->snapshot.results[move];
            }
            --scan->snapshot.count;
            break;
        }
    }
    if (scan->first_token_override != 0U &&
        scan->snapshot.count != 0U) {
        scan->snapshot.results[0].token = scan->first_token_override;
        scan->snapshot.results[0].channel = scan->first_channel_override;
        scan->snapshot.results[0].flags = (uint8_t)(
            scan->first_flags_override | P4_GAME_SIGNAL_SIMULATED);
        scan->snapshot.results[0].rssi_dbm = -43;
        (void)snprintf(scan->snapshot.results[0].label,
                       sizeof(scan->snapshot.results[0].label),
                       "GENOME TEST");
    }
    return true;
}

static bool preview_read_signal(void *context,
                                p4_game_signal_snapshot_t *snapshot)
{
    const preview_signal_scan_t *const scan = context;
    if (scan == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = scan->snapshot;
    return true;
}

static bool preview_queue_save(
    void *context, const char *slot_id, uint32_t schema_version,
    uint32_t expected_sequence, const uint8_t *data, size_t data_bytes,
    p4_game_save_ticket_t *ticket_out)
{
    preview_save_host_t *const save = context;
    if (save == NULL || slot_id == NULL || data == NULL ||
        ticket_out == NULL || strcmp(slot_id, "AUTO") != 0 ||
        schema_version != BYTE_BUDDY_SAVE_SCHEMA_VERSION ||
        data_bytes != sizeof(save->payload)) {
        return false;
    }
    memcpy(save->payload, data, data_bytes);
    save->ticket = save->ticket == UINT32_MAX
        ? 1U : save->ticket + 1U;
    if (save->ticket == P4_GAME_SAVE_INVALID_TICKET) {
        save->ticket = 1U;
    }
    save->committed_sequence = expected_sequence == UINT32_MAX
        ? 1U : expected_sequence + 1U;
    if (save->committed_sequence == 0U) {
        save->committed_sequence = 1U;
    }
    *ticket_out = save->ticket;
    return true;
}

static bool preview_read_save_status(
    void *context, p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out)
{
    const preview_save_host_t *const save = context;
    if (save == NULL || status_out == NULL ||
        committed_sequence_out == NULL ||
        ticket == P4_GAME_SAVE_INVALID_TICKET ||
        ticket != save->ticket || save->committed_sequence == 0U) {
        return false;
    }
    *status_out = P4_GAME_SAVE_COMMITTED;
    *committed_sequence_out = save->committed_sequence;
    return true;
}

static bool write_ppm(const char *path, const p4_game_surface_t *surface)
{
    FILE *const file = fopen(path, "wb");
    if (file == NULL || fprintf(file, "P6\n%u %u\n255\n", surface->width,
                                surface->height) < 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return false;
    }
    for (uint16_t y = 0U; y < surface->height; ++y) {
        for (uint16_t x = 0U; x < surface->width; ++x) {
            const uint16_t pixel = surface->pixels[
                (size_t)y * surface->stride_pixels + x];
            const unsigned char rgb[3] = {
                (unsigned char)(((pixel >> 11U) & 31U) * 255U / 31U),
                (unsigned char)(((pixel >> 5U) & 63U) * 255U / 63U),
                (unsigned char)((pixel & 31U) * 255U / 31U),
            };
            if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
                (void)fclose(file);
                return false;
            }
        }
    }
    return fclose(file) == 0;
}

static uint8_t *read_resource(const char *path, size_t *out_bytes)
{
    if (path == NULL || out_bytes == NULL) {
        return NULL;
    }
    FILE *const file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return NULL;
    }
    const long file_bytes = ftell(file);
    if (file_bytes <= 0L || file_bytes > 8L * 1024L * 1024L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    uint8_t *const data = malloc((size_t)file_bytes);
    if (data == NULL) {
        (void)fclose(file);
        return NULL;
    }
    const bool read_ok = fread(data, (size_t)file_bytes, 1U, file) == 1U;
    const bool close_ok = fclose(file) == 0;
    if (!read_ok || !close_ok) {
        free(data);
        return NULL;
    }
    *out_bytes = (size_t)file_bytes;
    return data;
}

static bool update(p4_game_instance_t *instance,
                   const p4_game_input_t *input)
{
    return p4_game_instance_update(instance, input, 16U) ==
        P4_GAME_CONTINUE;
}

static bool update_elapsed(p4_game_instance_t *instance,
                           const p4_game_input_t *input,
                           uint32_t elapsed_ms)
{
    return p4_game_instance_update(instance, input, elapsed_ms) ==
        P4_GAME_CONTINUE;
}

static bool advance_ms(p4_game_instance_t *instance, uint32_t elapsed_ms)
{
    const p4_game_input_t input = {.touch_valid = true};
    while (elapsed_ms != 0U) {
        const uint32_t step = elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
            ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
        if (!update_elapsed(instance, &input, step)) {
            return false;
        }
        elapsed_ms -= step;
    }
    return true;
}

static bool advance_touch_ms(
    p4_game_instance_t *instance, uint16_t x, uint16_t y,
    uint32_t elapsed_ms)
{
    const p4_game_input_t held = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    while (elapsed_ms != 0U) {
        const uint32_t step = elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
            ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
        if (!update_elapsed(instance, &held, step)) {
            return false;
        }
        elapsed_ms -= step;
    }
    const p4_game_input_t released = {.touch_valid = true};
    return update(instance, &released);
}

static bool advance_held_touch_ms(
    p4_game_instance_t *instance, uint16_t x, uint16_t y,
    uint32_t elapsed_ms)
{
    const p4_game_input_t held = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    while (elapsed_ms != 0U) {
        const uint32_t step = elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
            ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
        if (!update_elapsed(instance, &held, step)) {
            return false;
        }
        elapsed_ms -= step;
    }
    return true;
}

static bool settle_scene_transition(p4_game_instance_t *instance)
{
    return advance_ms(instance, PREVIEW_SCENE_TRANSITION_MS);
}

static bool tap(p4_game_instance_t *instance, uint16_t x, uint16_t y)
{
    const p4_game_input_t down = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    const p4_game_input_t up = {.touch_valid = true};
    return update(instance, &down) && update(instance, &up);
}

static bool press_button(p4_game_instance_t *instance, uint32_t button)
{
    const p4_game_input_t down = {
        .held = button,
        .pressed = button,
    };
    const p4_game_input_t up = {.released = button};
    return update(instance, &down) && update(instance, &up);
}

static bool trigger_care_credit(p4_game_instance_t *instance)
{
    static const uint16_t care_points[PREVIEW_CARE_VARIANT_COUNT][2] = {
        {20U, 145U},
        {180U, 145U},
        {280U, 145U},
        {160U, 80U},
    };
    const unsigned variant =
        s_preview_care_index % PREVIEW_CARE_VARIANT_COUNT;

    /*
     * Preview growth must obey the same need-aware anti-spam rule as play.
     * Ten simulated seconds per varied action keeps its primary need at or
     * below the varied-care threshold, including Feed's Energy boost and the
     * Clean/Rest Joy boosts before Pet.  Avoid Play here because it changes
     * scenes and would make evolution setup depend on minigame timing.
     */
    if (!advance_ms(instance, PREVIEW_CARE_NEED_REFRESH_MS) ||
        !tap(instance, care_points[variant][0], care_points[variant][1])) {
        return false;
    }
    ++s_preview_care_index;
    return true;
}

static bool care_credit(p4_game_instance_t *instance)
{
    if (!trigger_care_credit(instance)) {
        return false;
    }
    const p4_game_input_t released = {.touch_valid = true};
    /* Also let the longest authored evolution overlay release input. */
    for (unsigned wait = 0U; wait < 13U; ++wait) {
        if (!update_elapsed(instance, &released, 100U)) {
            return false;
        }
    }
    return true;
}

static bool care_credits(p4_game_instance_t *instance, unsigned count)
{
    for (unsigned credit = 0U; credit < count; ++credit) {
        if (!care_credit(instance)) {
            return false;
        }
    }
    return true;
}

static bool animate(p4_game_instance_t *instance, unsigned frames)
{
    const p4_game_input_t input = {.touch_valid = true};
    for (unsigned frame = 0U; frame < frames; ++frame) {
        if (!update(instance, &input)) {
            return false;
        }
    }
    return true;
}

static bool render_to(p4_game_instance_t *instance,
                      p4_game_surface_t *surface, const char *path)
{
    return p4_game_instance_render(instance, surface) &&
        write_ppm(path, surface);
}

static bool render_clip(p4_game_instance_t *instance,
                        p4_game_surface_t *surface,
                        const char *prefix, const char *label,
                        unsigned frames, unsigned updates_between)
{
    char path[1024];
    for (unsigned frame = 0U; frame < frames; ++frame) {
        const int bytes = snprintf(path, sizeof(path), "%s-%s-%02u.ppm",
                                   prefix, label, frame);
        if (bytes < 0 || (size_t)bytes >= sizeof(path) ||
            !render_to(instance, surface, path)) {
            return false;
        }
        ++s_clip_frame_count;
        if (frame + 1U < frames &&
            !animate(instance, updates_between)) {
            return false;
        }
    }
    return true;
}

static uint64_t surface_region_hash(
    const p4_game_surface_t *surface,
    uint16_t left, uint16_t top, uint16_t right, uint16_t bottom)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    if (surface == NULL || surface->pixels == NULL ||
        left >= right || top >= bottom ||
        right > surface->width || bottom > surface->height) {
        return 0U;
    }
    for (uint16_t y = top; y < bottom; ++y) {
        for (uint16_t x = left; x < right; ++x) {
            const uint16_t pixel = surface->pixels[
                (size_t)y * surface->stride_pixels + x];
            hash ^= (uint8_t)(pixel & UINT16_C(0x00ff));
            hash *= UINT64_C(1099511628211);
            hash ^= (uint8_t)(pixel >> 8U);
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static bool capture_region_hash(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_roi_t roi, uint64_t *hash)
{
    if (hash == NULL ||
        !p4_game_instance_render(instance, surface)) {
        return false;
    }
    *hash = surface_region_hash(
        surface, roi.left, roi.top, roi.right, roi.bottom);
    return *hash != 0U;
}

static bool sample_region_hashes(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_roi_t roi, uint32_t duration_ms, uint64_t hashes[4])
{
    uint32_t elapsed_ms = 0U;
    for (unsigned sample = 0U; sample < 4U; ++sample) {
        if (!capture_region_hash(
                instance, surface, roi, &hashes[sample])) {
            return false;
        }
        if (sample + 1U < 4U) {
            const uint32_t next_ms = (uint32_t)(
                (uint64_t)(duration_ms - 1U) * (sample + 1U) / 3U);
            if (next_ms <= elapsed_ms ||
                !advance_ms(instance, next_ms - elapsed_ms)) {
                return false;
            }
            elapsed_ms = next_ms;
        }
    }
    return true;
}

typedef struct {
    const char *label;
    unsigned local_frame;
    uint64_t expected_hash;
} preview_semantic_digest_t;

static bool semantic_digest_matches(
    const char *label, unsigned local_frame, uint64_t actual_hash)
{
    static const preview_semantic_digest_t expected[] = {
        {"counter-nova", 0U, UINT64_C(0x5eb7be7a64d2ef43)},
        {"counter-nova", 1U, UINT64_C(0x98d9e638e0f884e0)},
        {"counter-nova", 2U, UINT64_C(0xe14b419cf7caf48f)},
        {"counter-nova", 3U, UINT64_C(0xcaad1214caebcb41)},
        {"counter-flare", 0U, UINT64_C(0xc617fa3bb44d073a)},
        {"counter-flare", 1U, UINT64_C(0xdb94953fc4481876)},
        {"counter-flare", 2U, UINT64_C(0x0a753cdf4ec97e9a)},
        {"counter-flare", 3U, UINT64_C(0xbc7836a166a74436)},
        {"counter-glacier", 0U, UINT64_C(0x7470a2a899bc4a73)},
        {"counter-glacier", 1U, UINT64_C(0x73c5e0ef190f7617)},
        {"counter-glacier", 2U, UINT64_C(0xa912d531ae27a19f)},
        {"counter-glacier", 3U, UINT64_C(0x9735653ad0d7c1b7)},
        {"counter-jam", 0U, UINT64_C(0x3dd547a2a2054786)},
        {"counter-jam", 1U, UINT64_C(0x4a15e0c65415f85b)},
        {"counter-jam", 2U, UINT64_C(0x1c8f709ff566bece)},
        {"counter-jam", 3U, UINT64_C(0xa75e229c8fb6ed72)},
        {"outcome-victory", 0U, UINT64_C(0xa6c1d7cbd4ca8e60)},
        {"outcome-victory", 1U, UINT64_C(0xf7377c04272206a7)},
        {"outcome-victory", 2U, UINT64_C(0xdb0cb90d4805efde)},
        {"outcome-victory", 3U, UINT64_C(0x036826d74b30232b)},
        {"outcome-hp", 0U, UINT64_C(0xf7f003d231c6565a)},
        {"outcome-hp", 1U, UINT64_C(0x319343ec872e92ab)},
        {"outcome-hp", 2U, UINT64_C(0xd1e627160f8e64a2)},
        {"outcome-hp", 3U, UINT64_C(0x5b34c53bf6b57187)},
        {"outcome-timeout", 0U, UINT64_C(0x22319ddda541b027)},
        {"outcome-timeout", 1U, UINT64_C(0x2d77157305f97c0d)},
        {"outcome-timeout", 2U, UINT64_C(0xd5ebf0236a6e5620)},
        {"outcome-timeout", 3U, UINT64_C(0x26900480dd53881f)},
        {"outcome-retreat", 0U, UINT64_C(0xb7e684cd2afe1f9a)},
        {"outcome-retreat", 1U, UINT64_C(0x3a38db7bf3477e1b)},
        {"outcome-retreat", 2U, UINT64_C(0x9219249d34df8d29)},
        {"outcome-retreat", 3U, UINT64_C(0x8068ec5b8e9a4591)},
        {"evolution-genome", 0U, UINT64_C(0xd88b8c936c0d3fab)},
        {"evolution-genome", 1U, UINT64_C(0xaa5f5fcb3de8b5b3)},
        {"evolution-genome", 2U, UINT64_C(0x01bbda9727cef1ff)},
        {"evolution-genome", 3U, UINT64_C(0x84bfb4ae0f8646f1)},
        {"passive-ward", 0U, UINT64_C(0x9041ab5d35df5857)},
        {"passive-ward", 1U, UINT64_C(0xa8dc5848c465feee)},
        {"passive-ward", 2U, UINT64_C(0x468bbd67f970abb0)},
        {"passive-ward", 3U, UINT64_C(0x1286bbbc32baa2e3)},
        {"passive-echo", 0U, UINT64_C(0x5dbcb7a51aeeabe5)},
        {"passive-echo", 1U, UINT64_C(0x56685e34c3201d7e)},
        {"passive-echo", 2U, UINT64_C(0x85d2af44e8190185)},
        {"passive-echo", 3U, UINT64_C(0xc83b2bd48b2e45af)},
        {"passive-siphon", 0U, UINT64_C(0x7f05a94d69e6a219)},
        {"passive-siphon", 1U, UINT64_C(0xc1beda95f53e97b1)},
        {"passive-siphon", 2U, UINT64_C(0x9af0717d075c8dbe)},
        {"passive-siphon", 3U, UINT64_C(0xd980530e820d6439)},
        {"passive-overclock", 0U, UINT64_C(0x58f5d01999a01997)},
        {"passive-overclock", 1U, UINT64_C(0x653ffb72b09fb22d)},
        {"passive-overclock", 2U, UINT64_C(0xdf79da19af9cd506)},
        {"passive-overclock", 3U, UINT64_C(0x901e10f461af0fe1)},
        {"scan-scanning", 0U, UINT64_C(0xa21a7776719ac2ed)},
        {"scan-scanning", 1U, UINT64_C(0xd42262011122ce8b)},
        {"scan-scanning", 2U, UINT64_C(0x0a2c742d00160f78)},
        {"scan-scanning", 3U, UINT64_C(0x751e3d7a8af5a259)},
        {"scan-busy", 0U, UINT64_C(0xe0f250ba5bf3e0ce)},
        {"scan-busy", 1U, UINT64_C(0x42ffaf27704ca650)},
        {"scan-busy", 2U, UINT64_C(0x56f6b9e6ea5402d1)},
        {"scan-busy", 3U, UINT64_C(0x0d7c566214d6c885)},
        {"scan-offline", 0U, UINT64_C(0x69299ab1c55deafa)},
        {"scan-offline", 1U, UINT64_C(0xc53a6e618e70cdc1)},
        {"scan-offline", 2U, UINT64_C(0x90a729a507ac6c9d)},
        {"scan-offline", 3U, UINT64_C(0x0ef0cd8ae6aebb34)},
        {"scan-empty", 0U, UINT64_C(0xc4fa459e5dffd339)},
        {"scan-empty", 1U, UINT64_C(0x2c5c462d83e39bd9)},
        {"scan-empty", 2U, UINT64_C(0xf80ce870e27ad1ca)},
        {"scan-empty", 3U, UINT64_C(0xf7dded9dc212567a)},
        {"need-joy", 0U, UINT64_C(0xfb11a26fcd179b32)},
        {"need-joy", 1U, UINT64_C(0x2c6d1245179f64b3)},
        {"need-joy", 2U, UINT64_C(0x6d7e9326bda90661)},
        {"need-joy", 3U, UINT64_C(0x63267cf0acfc950a)},
        {"need-energy", 0U, UINT64_C(0x0bcd9375a4ddbd0c)},
        {"need-energy", 1U, UINT64_C(0xbfba83db82602bf0)},
        {"need-energy", 2U, UINT64_C(0x0bce454b5c181dbd)},
        {"need-energy", 3U, UINT64_C(0xf84531ba6df092a1)},
        {"need-hunger", 0U, UINT64_C(0xcb8d1798a97b36e7)},
        {"need-hunger", 1U, UINT64_C(0xd607582cca9badca)},
        {"need-hunger", 2U, UINT64_C(0x9f5a76bf5a24cabe)},
        {"need-hunger", 3U, UINT64_C(0xa76f9ac936a97659)},
        {"need-hygiene", 0U, UINT64_C(0x9f01a1a6266becc2)},
        {"need-hygiene", 1U, UINT64_C(0xce5c5d0e4700dc81)},
        {"need-hygiene", 2U, UINT64_C(0x8484d3fdc5a97c28)},
        {"need-hygiene", 3U, UINT64_C(0x96db50d7304492c6)},
        {"evolution-winged-signal", 0U,
         UINT64_C(0x4725213c16bd0b94)},
        {"evolution-winged-signal", 1U,
         UINT64_C(0x74069be5a61dfcf5)},
        {"evolution-winged-signal", 2U,
         UINT64_C(0x877ea0a7b6807273)},
        {"evolution-winged-signal", 3U,
         UINT64_C(0xafd52dac1f484234)},
        {"evolution-flying", 0U, UINT64_C(0xd7a77e6ddd5d2be8)},
        {"evolution-flying", 1U, UINT64_C(0xa7b7ec15d4026a8e)},
        {"evolution-flying", 2U, UINT64_C(0x0633440d0ed3cfab)},
        {"evolution-flying", 3U, UINT64_C(0xc07094aaba8af2c2)},
        {"evolution-elemental", 0U, UINT64_C(0xa7fb02c4ab197084)},
        {"evolution-elemental", 1U, UINT64_C(0x8c3644584af9d3b7)},
        {"evolution-elemental", 2U, UINT64_C(0x81620ad82adcf292)},
        {"evolution-elemental", 3U, UINT64_C(0xd923b302c40a3469)},
        {"activity-star-intro", 0U, UINT64_C(0xa1ffd5865272be4a)},
        {"activity-star-intro", 1U, UINT64_C(0x26d9c3f6c255bf3c)},
        {"activity-star-intro", 2U, UINT64_C(0x2088678244447cf7)},
        {"activity-star-intro", 3U, UINT64_C(0x2220aee4c222b5da)},
        {"activity-star-miss", 0U, UINT64_C(0xcac079a7b142a0bf)},
        {"activity-star-miss", 1U, UINT64_C(0xffd6d98018c12566)},
        {"activity-star-miss", 2U, UINT64_C(0x47bf7276daa56032)},
        {"activity-star-miss", 3U, UINT64_C(0x00f1af702f6767f9)},
        {"activity-star-summary", 0U, UINT64_C(0xe8926b90cc801190)},
        {"activity-star-summary", 1U,
         UINT64_C(0x06ac0e290e44a2d1)},
        {"activity-star-summary", 2U,
         UINT64_C(0x9f41cadfdca11356)},
        {"activity-star-summary", 3U,
         UINT64_C(0x6f01aa40267dceb9)},
        {"activity-shop-onset", 0U, UINT64_C(0xb614774b6ca342f7)},
        {"activity-shop-unlocked", 0U,
         UINT64_C(0xe72260d0582dc73d)},
        {"activity-shop-equipped", 0U,
         UINT64_C(0x29820f85adf31dfc)},
        {"activity-shop-unavailable", 0U,
         UINT64_C(0x0a7ae6aa6200ea51)},
    };
    _Static_assert(
        sizeof(expected) / sizeof(expected[0]) == 112U,
        "every authored coverage frame needs one semantic digest");
    unsigned matches = 0U;
    for (size_t index = 0U;
         index < sizeof(expected) / sizeof(expected[0]); ++index) {
        if (expected[index].local_frame != local_frame ||
            strcmp(expected[index].label, label) != 0) {
            continue;
        }
        if (actual_hash != expected[index].expected_hash) {
            fprintf(stderr,
                    "%s frame %u semantic digest %016llx, expected "
                    "%016llx\n",
                    label, local_frame,
                    (unsigned long long)actual_hash,
                    (unsigned long long)expected[index].expected_hash);
            return false;
        }
        ++matches;
    }
    if (matches != 1U) {
        fprintf(stderr,
                "%s frame %u has %u pinned semantic digests, expected 1\n",
                label, local_frame, matches);
        return false;
    }
    return true;
}

static bool render_motion_frame_roi(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    const char *label, unsigned local_frame, preview_roi_t roi,
    uint64_t *region_hash)
{
    char path[1024];
    if (writer == NULL || writer->prefix == NULL || label == NULL ||
        writer->frame_count >= PREVIEW_SIGNAL_MOTION_MAX_FRAMES) {
        return false;
    }
    const int bytes = snprintf(
        path, sizeof(path), "%s-%04u-%s-%03u.ppm",
        writer->prefix, writer->frame_count, label, local_frame);
    if (bytes < 0 || (size_t)bytes >= sizeof(path) ||
        !p4_game_instance_render(instance, surface)) {
        return false;
    }
    const uint64_t hash = surface_region_hash(
        surface, roi.left, roi.top, roi.right, roi.bottom);
    if (writer->print_semantic_digests) {
        fprintf(stdout, "        {\"%s\", %uU, UINT64_C(0x%016llx)},\n",
                label, local_frame, (unsigned long long)hash);
    }
    if (writer->require_semantic_digests &&
        !semantic_digest_matches(label, local_frame, hash)) {
        return false;
    }
    if (region_hash != NULL) {
        *region_hash = hash;
    }
    if (strcmp(writer->prefix, "-") != 0 &&
        !write_ppm(path, surface)) {
        return false;
    }
    ++writer->frame_count;
    return true;
}

static bool render_motion_frame(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    const char *label, unsigned local_frame, uint64_t *region_hash)
{
    return render_motion_frame_roi(
        writer, instance, surface, label, local_frame,
        (preview_roi_t){44U, 38U, 294U, 122U}, region_hash);
}

static uint32_t preview_30hz_target_ms(unsigned frame)
{
    return (uint32_t)(((uint64_t)frame + 1U) * 1000U / 30U);
}

static bool render_motion_phase(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    const char *label, uint32_t duration_ms,
    int guard_frame, const p4_game_point_t *held_touch,
    bool require_distinct, uint64_t *representative_hash)
{
    uint64_t first_hash = 0U;
    uint64_t last_hash = 0U;
    uint32_t elapsed_ms = 0U;
    unsigned frame = 0U;
    while (elapsed_ms < duration_ms) {
        uint64_t hash = 0U;
        if (!render_motion_frame(
                writer, instance, surface, label, frame, &hash)) {
            return false;
        }
        if (frame == 0U) {
            first_hash = hash;
        }
        last_hash = hash;

        p4_game_input_t input = {.touch_valid = true};
        if (held_touch != NULL) {
            input.touch_count = 1U;
            input.touches[0] = *held_touch;
        } else if (guard_frame >= 0 &&
                   frame == (unsigned)guard_frame) {
            input.touch_count = 1U;
            input.touches[0] = (p4_game_point_t){250U, 180U};
        }
        uint32_t next_ms = preview_30hz_target_ms(frame);
        if (next_ms > duration_ms) {
            next_ms = duration_ms;
        }
        const uint32_t step_ms = next_ms - elapsed_ms;
        if (step_ms == 0U ||
            !update_elapsed(instance, &input, step_ms)) {
            return false;
        }
        elapsed_ms = next_ms;
        ++frame;
    }
    if (representative_hash != NULL) {
        *representative_hash = last_hash;
    }
    if (require_distinct && frame > 1U && first_hash == last_hash) {
        fprintf(stderr, "static signal-motion phase: %s\n", label);
        return false;
    }
    return true;
}

static bool render_sampled_phase(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    const char *label, uint32_t duration_ms, unsigned samples,
    preview_roi_t roi, unsigned minimum_distinct,
    uint64_t *representative_hash)
{
    uint64_t hashes[16] = {0};
    uint32_t elapsed_ms = 0U;
    if (samples < 2U || samples > 16U || duration_ms < samples) {
        return false;
    }
    for (unsigned frame = 0U; frame < samples; ++frame) {
        if (!render_motion_frame_roi(
                writer, instance, surface, label, frame, roi,
                &hashes[frame])) {
            return false;
        }
        if (frame + 1U < samples) {
            const uint32_t next_ms = (uint32_t)(
                (uint64_t)(duration_ms - 1U) * (frame + 1U) /
                (samples - 1U));
            if (next_ms <= elapsed_ms ||
                !advance_ms(instance, next_ms - elapsed_ms)) {
                return false;
            }
            elapsed_ms = next_ms;
        }
    }
    unsigned distinct = 0U;
    for (unsigned frame = 0U; frame < samples; ++frame) {
        bool seen = false;
        for (unsigned prior = 0U; prior < frame; ++prior) {
            if (hashes[frame] == hashes[prior]) {
                seen = true;
                break;
            }
        }
        distinct += seen ? 0U : 1U;
    }
    if (distinct < minimum_distinct) {
        fprintf(stderr,
                "static authored phase: %s (%u/%u distinct samples)\n",
                label, distinct, samples);
        return false;
    }
    if (representative_hash != NULL) {
        *representative_hash = hashes[samples / 2U];
    }
    return true;
}

static bool hashes_are_pairwise_distinct(
    const uint64_t *hashes, size_t count, const char *label)
{
    if (hashes == NULL || label == NULL) {
        return false;
    }
    for (size_t index = 0U; index < count; ++index) {
        for (size_t prior = 0U; prior < index; ++prior) {
            if (hashes[index] == hashes[prior]) {
                fprintf(stderr,
                        "%s variants %zu and %zu render identically\n",
                        label, prior, index);
                return false;
            }
        }
    }
    return true;
}

static uint64_t preview_ordered_hash_digest(
    const uint64_t *hashes, size_t count)
{
    uint64_t digest = UINT64_C(1469598103934665603);
    if (hashes == NULL) {
        return 0U;
    }
    for (size_t index = 0U; index < count; ++index) {
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            digest ^= (uint8_t)(hashes[index] >> (byte * 8U));
            digest *= UINT64_C(1099511628211);
        }
    }
    return digest;
}

typedef struct {
    const char *label;
    uint64_t expected_hash;
} preview_atlas_digest_t;

static uint16_t preview_atlas_read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] |
                      (uint16_t)((uint16_t)data[1] << 8U));
}

static uint32_t preview_atlas_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8U |
        (uint32_t)data[2] << 16U | (uint32_t)data[3] << 24U;
}

static bool preview_completion_atlas_valid(
    const uint8_t *art, size_t art_bytes)
{
    const uint32_t expected_pixel_offset =
        PREVIEW_ATLAS_HEADER_BYTES +
        PREVIEW_ATLAS_EXPECTED_FRAMES * PREVIEW_ATLAS_PALETTE_BYTES;
    if (art == NULL || art_bytes != PREVIEW_ATLAS_EXPECTED_BYTES ||
        memcmp(art, "BBDART2\0", 8U) != 0 ||
        preview_atlas_read_u32(art + 8U) != 2U ||
        preview_atlas_read_u32(art + 12U) !=
            PREVIEW_ATLAS_EXPECTED_SHEETS ||
        preview_atlas_read_u32(art + 16U) !=
            PREVIEW_ATLAS_FRAME_WIDTH ||
        preview_atlas_read_u32(art + 20U) !=
            PREVIEW_ATLAS_FRAME_HEIGHT ||
        preview_atlas_read_u32(art + 24U) !=
            PREVIEW_ATLAS_FRAMES_PER_SHEET ||
        preview_atlas_read_u32(art + 28U) !=
            PREVIEW_ATLAS_PALETTE_ENTRIES ||
        preview_atlas_read_u32(art + 32U) !=
            PREVIEW_ATLAS_EXPECTED_FRAMES ||
        preview_atlas_read_u32(art + 36U) !=
            PREVIEW_ATLAS_HEADER_BYTES ||
        preview_atlas_read_u32(art + 40U) != expected_pixel_offset ||
        preview_atlas_read_u32(art + 44U) !=
            PREVIEW_ATLAS_EXPECTED_BYTES) {
        fprintf(stderr,
                "completion atlas contract mismatch: expected "
                "%u sheets, %u frames, %u bytes\n",
                PREVIEW_ATLAS_EXPECTED_SHEETS,
                PREVIEW_ATLAS_EXPECTED_FRAMES,
                PREVIEW_ATLAS_EXPECTED_BYTES);
        return false;
    }
    for (size_t reserved = 48U;
         reserved < PREVIEW_ATLAS_HEADER_BYTES; ++reserved) {
        if (art[reserved] != 0U) {
            fprintf(stderr,
                    "completion atlas header byte %zu is not reserved zero\n",
                    reserved);
            return false;
        }
    }
    return true;
}

static bool preview_decode_atlas_frame(
    const uint8_t *art, size_t art_bytes, unsigned global_frame,
    uint16_t pixels[PREVIEW_ATLAS_FRAME_WIDTH *
                    PREVIEW_ATLAS_FRAME_HEIGHT])
{
    if (!preview_completion_atlas_valid(art, art_bytes) ||
        pixels == NULL || global_frame >= PREVIEW_ATLAS_EXPECTED_FRAMES) {
        return false;
    }
    const size_t palette_offset = PREVIEW_ATLAS_HEADER_BYTES +
        (size_t)global_frame * PREVIEW_ATLAS_PALETTE_BYTES;
    const size_t pixel_bank_offset = PREVIEW_ATLAS_HEADER_BYTES +
        (size_t)PREVIEW_ATLAS_EXPECTED_FRAMES *
            PREVIEW_ATLAS_PALETTE_BYTES;
    const size_t packed_offset = pixel_bank_offset +
        (size_t)global_frame * PREVIEW_ATLAS_PACKED_FRAME_BYTES;
    const size_t pixel_count = (size_t)PREVIEW_ATLAS_FRAME_WIDTH *
        PREVIEW_ATLAS_FRAME_HEIGHT;
    for (size_t pixel = 0U; pixel < pixel_count; ++pixel) {
        const uint8_t packed = art[packed_offset + pixel / 2U];
        const unsigned palette_index = (pixel & 1U) == 0U
            ? (unsigned)(packed >> 4U)
            : (unsigned)(packed & UINT8_C(0x0f));
        pixels[pixel] = preview_atlas_read_u16(
            art + palette_offset + palette_index * 2U);
    }
    return true;
}

static bool preview_completion_label(
    unsigned sheet_offset, unsigned frame,
    char *label, size_t label_bytes)
{
    static const char *const signature_names[4] = {
        "feed", "play", "pet", "signal",
    };
    static const char *const action_names[
        PREVIEW_COMPLETION_ACTION_COUNT] = {
        "feed", "play", "clean", "rest", "pet", "grow", "signal",
    };
    static const char *const stage_names[
        PREVIEW_COMPLETION_STAGE_COUNT] = {
        "baby", "winged", "flying", "elemental",
    };
    static const char *const hatch_names[
        PREVIEW_COMPLETION_HATCH_VARIANT_COUNT] = {
        "nebula", "sungold", "jade", "glacier",
    };
    static const char *const milestone_names[
        PREVIEW_COMPLETION_HATCH_MILESTONE_COUNT] = {
        "first-crack", "peek", "emerge", "hatch",
    };
    int written = -1;
    if (label == NULL || label_bytes == 0U ||
        sheet_offset >= PREVIEW_COMPLETION_SHEET_COUNT ||
        frame >= PREVIEW_ATLAS_FRAMES_PER_SHEET) {
        return false;
    }
    const unsigned row = frame / PREVIEW_COMPLETION_PHASE_COUNT;
    const unsigned phase = frame % PREVIEW_COMPLETION_PHASE_COUNT;
    if (sheet_offset == 0U) {
        written = snprintf(
            label, label_bytes, "action-fx-%s-phase-%u",
            signature_names[row], phase);
    } else if (sheet_offset <= PREVIEW_COMPLETION_ACTION_COUNT) {
        written = snprintf(
            label, label_bytes, "action-%s-%s-phase-%u",
            action_names[sheet_offset - 1U], stage_names[row], phase);
    } else {
        written = snprintf(
            label, label_bytes, "hatch-%s-%s-phase-%u",
            hatch_names[sheet_offset - 1U -
                        PREVIEW_COMPLETION_ACTION_COUNT],
            milestone_names[row], phase);
    }
    return written >= 0 && (size_t)written < label_bytes;
}

static bool preview_validate_v18_cells(
    const uint8_t *art, size_t art_bytes,
    uint16_t pixels[PREVIEW_ATLAS_FRAME_WIDTH *
                    PREVIEW_ATLAS_FRAME_HEIGHT])
{
    static const preview_atlas_digest_t expected[
        PREVIEW_ATLAS_FRAMES_PER_SHEET] = {
        {"rooftop-observatory", UINT64_C(0x23ff30806b1d7f5d)},
        {"signal-city-skyline", UINT64_C(0xfb34acf7c4694068)},
        {"crescent-moon", UINT64_C(0xb2f9a43daccd98fc)},
        {"rooftop-nest", UINT64_C(0x88560f71787e84ec)},
        {"rain-cloud", UINT64_C(0xb6e6f751e719dcad)},
        {"cyan-violet-drift", UINT64_C(0x57cba34fb3217279)},
        {"golden-swirl", UINT64_C(0x8552842ce42c02c1)},
        {"steady-vortex", UINT64_C(0xe516ec0bcfffdb24)},
        {"home-heart-gate", UINT64_C(0x9ca2a2230bbba2b8)},
        {"signal-star-gate", UINT64_C(0x7c2dbdcdc0396d08)},
        {"shop-stone-gate", UINT64_C(0xec1815b283c28035)},
        {"lineage-helix-gate", UINT64_C(0x596ce4240252be26)},
        {"arena-heavy", UINT64_C(0x8aebbea7ed3336cf)},
        {"arena-quick", UINT64_C(0x3efd2f09d41c877d)},
        {"arena-echo", UINT64_C(0xab9ce21b8538539b)},
        {"arena-shift", UINT64_C(0xce22a7f8db8890fe)},
    };
    uint64_t hashes[PREVIEW_ATLAS_FRAMES_PER_SHEET] = {0};
    const p4_game_surface_t cell = {
        .pixels = pixels,
        .stride_pixels = PREVIEW_ATLAS_FRAME_WIDTH,
        .width = PREVIEW_ATLAS_FRAME_WIDTH,
        .height = PREVIEW_ATLAS_FRAME_HEIGHT,
    };
    for (unsigned frame = 0U;
         frame < PREVIEW_ATLAS_FRAMES_PER_SHEET; ++frame) {
        if (!preview_decode_atlas_frame(
                art, art_bytes,
                PREVIEW_V18_SHEET * PREVIEW_ATLAS_FRAMES_PER_SHEET + frame,
                pixels)) {
            return false;
        }
        hashes[frame] = surface_region_hash(
            &cell, 0U, 0U,
            PREVIEW_ATLAS_FRAME_WIDTH, PREVIEW_ATLAS_FRAME_HEIGHT);
        fprintf(stdout,
                "completion-motion-v18 cell=%u label=%s digest=%016llx\n",
                frame, expected[frame].label,
                (unsigned long long)hashes[frame]);
        if (hashes[frame] != expected[frame].expected_hash) {
            fprintf(stderr,
                    "v18 sheet 29 cell %u (%s) digest %016llx, "
                    "expected %016llx\n",
                    frame, expected[frame].label,
                    (unsigned long long)hashes[frame],
                    (unsigned long long)expected[frame].expected_hash);
            return false;
        }
    }
    return hashes_are_pairwise_distinct(
        hashes, PREVIEW_ATLAS_FRAMES_PER_SHEET,
        "v18 environment chrome cells");
}

static bool preview_action_sequences_are_distinct(
    const uint64_t action_hashes[PREVIEW_COMPLETION_ACTION_COUNT]
                                [PREVIEW_COMPLETION_STAGE_COUNT]
                                [PREVIEW_COMPLETION_PHASE_COUNT])
{
    static const char *const action_names[
        PREVIEW_COMPLETION_ACTION_COUNT] = {
        "feed", "play", "clean", "rest", "pet", "grow", "signal",
    };
    static const char *const stage_names[
        PREVIEW_COMPLETION_STAGE_COUNT] = {
        "baby", "winged", "flying", "elemental",
    };
    for (unsigned stage = 0U;
         stage < PREVIEW_COMPLETION_STAGE_COUNT; ++stage) {
        for (unsigned action = 0U;
             action < PREVIEW_COMPLETION_ACTION_COUNT; ++action) {
            for (unsigned prior = 0U; prior < action; ++prior) {
                bool identical = true;
                for (unsigned phase = 0U;
                     phase < PREVIEW_COMPLETION_PHASE_COUNT; ++phase) {
                    if (action_hashes[action][stage][phase] !=
                        action_hashes[prior][stage][phase]) {
                        identical = false;
                        break;
                    }
                }
                if (identical) {
                    fprintf(stderr,
                            "%s %s and %s ordered action sequences "
                            "are identical\n",
                            stage_names[stage], action_names[prior],
                            action_names[action]);
                    return false;
                }
            }
        }
    }
    return true;
}

static bool render_completion_motion_sequence(
    const uint8_t *art, size_t art_bytes, uint16_t *pixels,
    const char *prefix, unsigned expected_frames)
{
    static const preview_atlas_digest_t expected_sheet_digests[
        PREVIEW_COMPLETION_SHEET_COUNT] = {
        {"action-signature-fx", UINT64_C(0xddbe3b357abcbbe2)},
        {"action-feed", UINT64_C(0xb4bbee6515f5d7fa)},
        {"action-play", UINT64_C(0x09e07713391c96b2)},
        {"action-clean", UINT64_C(0xea4464a848772f04)},
        {"action-rest", UINT64_C(0x19d5fca2b6de6adb)},
        {"action-pet", UINT64_C(0x8b20c58ac3bc96e7)},
        {"action-grow", UINT64_C(0x3de54e7ad8e51ce5)},
        {"action-signal", UINT64_C(0xafa74d28d72a29be)},
        {"hatch-nebula", UINT64_C(0x0c6533bfc06bfce9)},
        {"hatch-sungold", UINT64_C(0x67d927f81ef481cb)},
        {"hatch-jade", UINT64_C(0xb7b77301f54b6197)},
        {"hatch-glacier", UINT64_C(0x13a07361d5b6a7fa)},
    };
    static const char *const action_names[
        PREVIEW_COMPLETION_ACTION_COUNT] = {
        "feed", "play", "clean", "rest", "pet", "grow", "signal",
    };
    static const char *const stage_names[
        PREVIEW_COMPLETION_STAGE_COUNT] = {
        "baby", "winged", "flying", "elemental",
    };
    static const char *const hatch_names[
        PREVIEW_COMPLETION_HATCH_VARIANT_COUNT] = {
        "nebula", "sungold", "jade", "glacier",
    };
    static const char *const milestone_names[
        PREVIEW_COMPLETION_HATCH_MILESTONE_COUNT] = {
        "first-crack", "peek", "emerge", "hatch",
    };
    const unsigned contact_width =
        PREVIEW_COMPLETION_CONTACT_COLUMNS * 4U *
        PREVIEW_ATLAS_FRAME_WIDTH;
    const unsigned contact_height =
        PREVIEW_COMPLETION_CONTACT_ROWS * 4U *
        PREVIEW_ATLAS_FRAME_HEIGHT;
    uint16_t *contact_pixels = NULL;
    uint64_t sheet_hashes[PREVIEW_COMPLETION_SHEET_COUNT]
                         [PREVIEW_ATLAS_FRAMES_PER_SHEET] = {{0}};
    uint64_t fx_hashes[PREVIEW_ATLAS_FRAMES_PER_SHEET] = {0};
    uint64_t action_hashes[PREVIEW_COMPLETION_ACTION_COUNT]
                          [PREVIEW_COMPLETION_STAGE_COUNT]
                          [PREVIEW_COMPLETION_PHASE_COUNT] = {{{0}}};
    uint64_t hatch_hashes[PREVIEW_COMPLETION_HATCH_VARIANT_COUNT]
                         [PREVIEW_COMPLETION_HATCH_MILESTONE_COUNT]
                         [PREVIEW_COMPLETION_PHASE_COUNT] = {{{0}}};
    unsigned frame_count = 0U;
    bool success = pixels != NULL && prefix != NULL &&
        preview_completion_atlas_valid(art, art_bytes) &&
        preview_validate_v18_cells(art, art_bytes, pixels);
    if (success && strcmp(prefix, "-") != 0) {
        contact_pixels = calloc(
            (size_t)contact_width * contact_height,
            sizeof(*contact_pixels));
        success = contact_pixels != NULL;
    }
    const p4_game_surface_t cell = {
        .pixels = pixels,
        .stride_pixels = PREVIEW_ATLAS_FRAME_WIDTH,
        .width = PREVIEW_ATLAS_FRAME_WIDTH,
        .height = PREVIEW_ATLAS_FRAME_HEIGHT,
    };
    for (unsigned sheet_offset = 0U;
         success && sheet_offset < PREVIEW_COMPLETION_SHEET_COUNT;
         ++sheet_offset) {
        for (unsigned frame = 0U;
             success && frame < PREVIEW_ATLAS_FRAMES_PER_SHEET; ++frame) {
            const unsigned global_frame =
                (PREVIEW_COMPLETION_FIRST_SHEET + sheet_offset) *
                    PREVIEW_ATLAS_FRAMES_PER_SHEET + frame;
            char label[96];
            char path[1024];
            success = preview_decode_atlas_frame(
                    art, art_bytes, global_frame, pixels) &&
                preview_completion_label(
                    sheet_offset, frame, label, sizeof(label));
            if (!success) {
                break;
            }
            const uint64_t hash = surface_region_hash(
                &cell, 0U, 0U,
                PREVIEW_ATLAS_FRAME_WIDTH, PREVIEW_ATLAS_FRAME_HEIGHT);
            fprintf(stdout,
                    "completion-motion[%03u] sheet=%u cell=%u "
                    "label=%s digest=%016llx\n",
                    frame_count,
                    PREVIEW_COMPLETION_FIRST_SHEET + sheet_offset,
                    frame, label, (unsigned long long)hash);
            if (strcmp(prefix, "-") != 0) {
                const int written = snprintf(
                    path, sizeof(path), "%s-%03u-%s.ppm",
                    prefix, frame_count, label);
                if (written < 0 || (size_t)written >= sizeof(path) ||
                    !write_ppm(path, &cell)) {
                    success = false;
                    break;
                }
                const unsigned destination_x =
                    ((sheet_offset % PREVIEW_COMPLETION_CONTACT_COLUMNS) *
                         4U + frame % 4U) *
                    PREVIEW_ATLAS_FRAME_WIDTH;
                const unsigned destination_y =
                    ((sheet_offset / PREVIEW_COMPLETION_CONTACT_COLUMNS) *
                         4U + frame / 4U) *
                    PREVIEW_ATLAS_FRAME_HEIGHT;
                for (unsigned y = 0U;
                     y < PREVIEW_ATLAS_FRAME_HEIGHT; ++y) {
                    (void)memcpy(
                        contact_pixels +
                            (size_t)(destination_y + y) * contact_width +
                            destination_x,
                        pixels + (size_t)y * PREVIEW_ATLAS_FRAME_WIDTH,
                        (size_t)PREVIEW_ATLAS_FRAME_WIDTH *
                            sizeof(*pixels));
                }
            }
            if (sheet_offset == 0U) {
                fx_hashes[frame] = hash;
            } else if (sheet_offset <=
                       PREVIEW_COMPLETION_ACTION_COUNT) {
                action_hashes[sheet_offset - 1U]
                             [frame / PREVIEW_COMPLETION_PHASE_COUNT]
                             [frame % PREVIEW_COMPLETION_PHASE_COUNT] = hash;
            } else {
                hatch_hashes[sheet_offset - 1U -
                             PREVIEW_COMPLETION_ACTION_COUNT]
                            [frame / PREVIEW_COMPLETION_PHASE_COUNT]
                            [frame % PREVIEW_COMPLETION_PHASE_COUNT] = hash;
            }
            sheet_hashes[sheet_offset][frame] = hash;
            ++frame_count;
        }
    }
    if (success && contact_pixels != NULL) {
        char path[1024];
        const int written = snprintf(
            path, sizeof(path), "%s-contact-sheet.ppm", prefix);
        const p4_game_surface_t contact = {
            .pixels = contact_pixels,
            .stride_pixels = (uint16_t)contact_width,
            .width = (uint16_t)contact_width,
            .height = (uint16_t)contact_height,
        };
        success = written >= 0 && (size_t)written < sizeof(path) &&
            write_ppm(path, &contact);
    }
    if (success) {
        success = hashes_are_pairwise_distinct(
            fx_hashes, PREVIEW_ATLAS_FRAMES_PER_SHEET,
            "action signature FX cells");
    }
    for (unsigned action = 0U;
         success && action < PREVIEW_COMPLETION_ACTION_COUNT; ++action) {
        for (unsigned stage = 0U;
             success && stage < PREVIEW_COMPLETION_STAGE_COUNT; ++stage) {
            char label[96];
            const int written = snprintf(
                label, sizeof(label), "%s %s action phases",
                stage_names[stage], action_names[action]);
            success = written >= 0 && (size_t)written < sizeof(label) &&
                hashes_are_pairwise_distinct(
                    action_hashes[action][stage],
                    PREVIEW_COMPLETION_PHASE_COUNT, label);
        }
    }
    if (success) {
        success = preview_action_sequences_are_distinct(action_hashes);
    }
    for (unsigned variant = 0U;
         success && variant < PREVIEW_COMPLETION_HATCH_VARIANT_COUNT;
         ++variant) {
        for (unsigned milestone = 0U;
             success && milestone <
                 PREVIEW_COMPLETION_HATCH_MILESTONE_COUNT;
             ++milestone) {
            char label[96];
            const int written = snprintf(
                label, sizeof(label), "%s %s hatch phases",
                hatch_names[variant], milestone_names[milestone]);
            success = written >= 0 && (size_t)written < sizeof(label) &&
                hashes_are_pairwise_distinct(
                    hatch_hashes[variant][milestone],
                    PREVIEW_COMPLETION_PHASE_COUNT, label);
        }
    }
    for (unsigned sheet_offset = 0U;
         success && sheet_offset < PREVIEW_COMPLETION_SHEET_COUNT;
         ++sheet_offset) {
        const uint64_t actual_digest = preview_ordered_hash_digest(
            sheet_hashes[sheet_offset],
            PREVIEW_ATLAS_FRAMES_PER_SHEET);
        fprintf(stdout,
                "completion-motion-sheet-digest sheet=%u label=%s "
                "digest=%016llx\n",
                PREVIEW_COMPLETION_FIRST_SHEET + sheet_offset,
                expected_sheet_digests[sheet_offset].label,
                (unsigned long long)actual_digest);
        if (actual_digest !=
            expected_sheet_digests[sheet_offset].expected_hash) {
            fprintf(stderr,
                    "completion sheet %u (%s) digest %016llx, "
                    "expected %016llx\n",
                    PREVIEW_COMPLETION_FIRST_SHEET + sheet_offset,
                    expected_sheet_digests[sheet_offset].label,
                    (unsigned long long)actual_digest,
                    (unsigned long long)expected_sheet_digests[
                        sheet_offset].expected_hash);
            success = false;
        }
    }
    fprintf(stdout, "completion-motion-expected-frames=%u\n",
            PREVIEW_COMPLETION_FRAME_COUNT);
    fprintf(stdout, "completion-motion-frames=%u\n", frame_count);
    if (expected_frames != PREVIEW_COMPLETION_FRAME_COUNT ||
        frame_count != PREVIEW_COMPLETION_FRAME_COUNT) {
        fprintf(stderr,
                "expected completion-motion argument/count %u/%u, "
                "got %u/%u\n",
                PREVIEW_COMPLETION_FRAME_COUNT,
                PREVIEW_COMPLETION_FRAME_COUNT,
                expected_frames, frame_count);
        success = false;
    }
    free(contact_pixels);
    return success;
}

static uint64_t preview_signal_token(unsigned batch, unsigned index)
{
    static const uint16_t rarity_roll[4] = {
        UINT16_C(0x0200), UINT16_C(0x0080),
        UINT16_C(0x0010), UINT16_C(0x0001),
    };
    if (batch >= 4U || index >= 8U) {
        return 0U;
    }
    const unsigned ordinal = batch * 8U + index;
    const unsigned core = ordinal & 3U;
    const unsigned halo = (ordinal + 1U) & 3U;
    const unsigned sigil = (ordinal + 2U) & 3U;
    const unsigned aura = (ordinal + 3U) & 3U;
    const unsigned hue = ordinal & 7U;
    return ((uint64_t)(ordinal + 1U) << 32U) |
        ((uint64_t)core << 21U) |
        ((uint64_t)halo << 23U) |
        ((uint64_t)aura << 25U) |
        ((uint64_t)sigil << 27U) |
        ((uint64_t)hue << 18U) |
        rarity_roll[ordinal & 3U];
}

static byte_buddy_signal_encounter_t preview_signal_encounter(
    unsigned batch, unsigned index)
{
    static const uint8_t channels[8] = {
        1U, 6U, 11U, 36U, 44U, 6U, 11U, 149U,
    };
    static const uint8_t flags[8] = {
        P4_GAME_SIGNAL_PROTECTED, P4_GAME_SIGNAL_PROTECTED,
        P4_GAME_SIGNAL_PROTECTED, P4_GAME_SIGNAL_PROTECTED,
        P4_GAME_SIGNAL_HIDDEN, P4_GAME_SIGNAL_PROTECTED,
        0U, P4_GAME_SIGNAL_PROTECTED,
    };
    if (index >= 8U) {
        return (byte_buddy_signal_encounter_t){0};
    }
    return byte_buddy_signal_encounter(
        preview_signal_token(batch, index), -43,
        channels[index],
        (uint8_t)(flags[index] | P4_GAME_SIGNAL_SIMULATED));
}

static uint16_t preview_signal_open_ms(
    byte_buddy_signal_encounter_t encounter)
{
    const unsigned committed = (unsigned)encounter.telegraph_ms +
        PREVIEW_SIGNAL_TRAVEL_MS + PREVIEW_SIGNAL_IMPACT_MS +
        PREVIEW_SIGNAL_RECOVERY_MS;
    unsigned open_ms = encounter.attack_period_ms > committed
        ? (unsigned)encounter.attack_period_ms - committed : 300U;
    if (encounter.hidden) {
        open_ms = open_ms > 120U ? open_ms - 120U : 180U;
    }
    if (open_ms < 180U) {
        open_ms = 180U;
    } else if (open_ms > 1300U) {
        open_ms = 1300U;
    }
    return (uint16_t)open_ms;
}

static uint16_t surface_pixel(
    const p4_game_surface_t *surface, uint16_t x, uint16_t y)
{
    if (surface == NULL || surface->pixels == NULL ||
        x >= surface->width || y >= surface->height) {
        return 0U;
    }
    return surface->pixels[(size_t)y * surface->stride_pixels + x];
}

static unsigned count_surface_color(
    const p4_game_surface_t *surface,
    uint16_t left, uint16_t top, uint16_t right, uint16_t bottom,
    uint16_t color)
{
    unsigned count = 0U;
    for (uint16_t y = top; y < bottom; ++y) {
        for (uint16_t x = left; x < right; ++x) {
            if (surface_pixel(surface, x, y) == color) {
                ++count;
            }
        }
    }
    return count;
}

static bool screen_signature_matches(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_screen_signature_t signature)
{
    typedef struct {
        const char *name;
        uint64_t header_hash;
        uint64_t marker_hash;
    } preview_screen_digest_t;
    static const preview_screen_digest_t expected[PREVIEW_SCREEN_COUNT] = {
        [PREVIEW_SCREEN_SIGNAL_LIST] = {
            "signal-list", UINT64_C(0x1e23f951cb7875ef), UINT64_C(0),
        },
        [PREVIEW_SCREEN_SIGNAL_TRACKER] = {
            "signal-tracker", UINT64_C(0x4a7e20afeaa0dac7), UINT64_C(0),
        },
        [PREVIEW_SCREEN_PULSE_ACTIVE] = {
            "pulse-active", UINT64_C(0x7105e280040818ae), UINT64_C(0),
        },
        [PREVIEW_SCREEN_WEAVE_ACTIVE] = {
            "weave-active", UINT64_C(0x1d987b421aa3e27f), UINT64_C(0),
        },
        [PREVIEW_SCREEN_SIGNAL_REWARD] = {
            "signal-reward", UINT64_C(0x1e23f951cb7875ef),
            UINT64_C(0xb50bb8c37c89c4a3),
        },
    };
    if ((unsigned)signature >= PREVIEW_SCREEN_COUNT ||
        !p4_game_instance_render(instance, surface)) {
        return false;
    }
    const preview_screen_digest_t digest = expected[signature];
    const uint64_t header_hash = surface_region_hash(
        surface, 60U, 2U, 154U, 22U);
    const uint64_t marker_hash =
        signature == PREVIEW_SCREEN_SIGNAL_REWARD
            ? surface_region_hash(surface, 118U, 72U, 232U, 84U)
            : 0U;
    if (header_hash != digest.header_hash ||
        marker_hash != digest.marker_hash) {
        fprintf(stderr,
                "%s semantic signature changed "
                "(header %016llx, marker %016llx; expected "
                "%016llx, %016llx)\n",
                digest.name,
                (unsigned long long)header_hash,
                (unsigned long long)marker_hash,
                (unsigned long long)digest.header_hash,
                (unsigned long long)digest.marker_hash);
        return false;
    }
    if ((signature == PREVIEW_SCREEN_PULSE_ACTIVE ||
         signature == PREVIEW_SCREEN_WEAVE_ACTIVE) &&
        surface_pixel(surface, 51U, 49U) == UINT16_C(0x000b) &&
        surface_pixel(surface, 268U, 49U) == UINT16_C(0x000b) &&
        surface_pixel(surface, 50U, 48U) ==
            surface_pixel(surface, 269U, 48U)) {
        fprintf(stderr,
                "%s is not active; an intro/outcome card is visible\n",
                digest.name);
        return false;
    }
    return true;
}

static bool settle_signal_battle_active(
    p4_game_instance_t *instance)
{
    return settle_scene_transition(instance) &&
        advance_ms(instance, PREVIEW_SIGNAL_ACTIVE_AFTER_TRANSITION_MS);
}

static bool prime_signal_battle_sample(
    p4_game_instance_t *instance, uint64_t token)
{
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    if (byte_buddy_signal_battle_pattern(genome) ==
        BYTE_BUDDY_BATTLE_PULSE_RUSH) {
        return tap(instance, 60U, 180U);
    }
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(token, -43);
    const byte_buddy_signal_weave_rules_t rules =
        byte_buddy_signal_weave_rules(genome, profile.strength, 0U);
    const byte_buddy_signal_weave_node_t node =
        byte_buddy_signal_weave_node(genome, 0U);
    const uint32_t preview_hold_ms = rules.hold_ms > 2U
        ? rules.hold_ms / 2U : 1U;
    return advance_held_touch_ms(
        instance, node.x, node.y, preview_hold_ms);
}

static bool render_battle_result(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    uint16_t border_color)
{
    return p4_game_instance_render(instance, surface) &&
        surface_pixel(surface, 44U, 48U) == border_color;
}

static bool complete_resonance_weave(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    uint64_t token, byte_buddy_signal_encounter_t encounter)
{
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(
            token, (int8_t)PREVIEW_LEGACY_FOCUS_RSSI_DBM);
    const byte_buddy_signal_weave_rules_t rules =
        byte_buddy_signal_weave_rules(genome, profile.strength, 0U);
    for (uint8_t step = 0U; step < rules.required_locks; ++step) {
        const byte_buddy_signal_weave_node_t node =
            byte_buddy_signal_weave_node(genome, step);
        const p4_game_input_t held = {
            .touch_valid = true,
            .touch_count = 1U,
            .touches = {{.x = node.x, .y = node.y}},
        };
        const uint32_t hold_budget_ms = (uint32_t)rules.hold_ms +
            (encounter.arena == BYTE_BUDDY_SIGNAL_ARENA_SHIFT
                ? 800U : 400U);
        uint32_t held_ms = 0U;
        uint32_t guard_attempt_ms = 0U;
        while (held_ms < hold_budget_ms) {
            const uint32_t step_ms = hold_budget_ms - held_ms > 50U
                ? 50U : hold_budget_ms - held_ms;
            if (!update_elapsed(instance, &held, step_ms) ||
                !p4_game_instance_render(instance, surface)) {
                return false;
            }
            const uint16_t border = surface_pixel(surface, 44U, 48U);
            if (border == UINT16_C(0xffe0)) {
                return true;
            }
            if (border == UINT16_C(0xf81f)) {
                return false;
            }
            held_ms += step_ms;
            guard_attempt_ms += step_ms;
            if (guard_attempt_ms >= 500U &&
                held_ms < hold_budget_ms) {
                const p4_game_input_t released = {.touch_valid = true};
                if (!update(instance, &released) ||
                    !press_button(instance, P4_BUTTON_START) ||
                    !p4_game_instance_render(instance, surface)) {
                    return false;
                }
                const uint16_t guard_border =
                    surface_pixel(surface, 44U, 48U);
                if (guard_border == UINT16_C(0xffe0)) {
                    return true;
                }
                if (guard_border == UINT16_C(0xf81f)) {
                    return false;
                }
                guard_attempt_ms = 0U;
            }
        }
        const p4_game_input_t released = {.touch_valid = true};
        for (unsigned settle = 0U; settle < 3U; ++settle) {
            if (!update_elapsed(instance, &released, 100U)) {
                return false;
            }
        }
    }
    return true;
}

static bool enter_signal_battle_index(
    p4_game_instance_t *instance, unsigned batch, unsigned index)
{
    const uint64_t token = preview_signal_token(batch, index);
    if (token == 0U || !settle_scene_transition(instance) ||
        !tap(instance, 100U, 180U) ||
        !settle_scene_transition(instance) ||
        !tap(instance, 30U, 180U) ||
        (index >= 5U && !tap(instance, 280U, 180U)) ||
        !tap(instance, 70U,
             (uint16_t)(42U + (index % 5U) * 25U)) ||
        !settle_scene_transition(instance) ||
        !tap(instance, 70U, 180U) ||
        !press_button(instance, P4_BUTTON_A)) {
        return false;
    }
    return true;
}

static bool finish_active_signal_battle_index(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    unsigned batch, unsigned index)
{
    const uint64_t token = preview_signal_token(batch, index);
    if (token == 0U || surface == NULL) {
        return false;
    }
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    const byte_buddy_signal_encounter_t encounter =
        preview_signal_encounter(batch, index);
    if (byte_buddy_signal_battle_pattern(genome) ==
        BYTE_BUDDY_BATTLE_RESONANCE_WEAVE) {
        const bool completed = complete_resonance_weave(
            instance, surface, token, encounter);
        const bool rendered = p4_game_instance_render(instance, surface);
        const uint16_t result_border = rendered
            ? surface_pixel(surface, 44U, 48U) : 0U;
        if (!completed || !rendered ||
            result_border != UINT16_C(0xffe0)) {
            fprintf(stderr,
                    "legacy Resonance victory failed at batch %u index %u "
                    "border 0x%04x\n",
                    batch, index, result_border);
            return false;
        }
        return advance_ms(instance, PREVIEW_SIGNAL_VICTORY_MS);
    }
    for (unsigned strike = 0U; strike < 32U; ++strike) {
        if (!tap(instance, 60U, 180U) ||
            !p4_game_instance_render(instance, surface)) {
            return false;
        }
        const uint16_t border = surface_pixel(surface, 44U, 48U);
        if (border == UINT16_C(0xffe0)) {
            return advance_ms(instance, PREVIEW_SIGNAL_VICTORY_MS);
        }
        if (border == UINT16_C(0xf81f) ||
            !advance_ms(instance, PREVIEW_SIGNAL_STRIKE_WAIT_MS)) {
            fprintf(stderr,
                    "legacy Pulse defeat at batch %u index %u strike %u\n",
                    batch, index, strike);
            return false;
        }
    }
    fprintf(stderr, "legacy Pulse stalled at batch %u index %u\n",
            batch, index);
    return false;
}

static bool finish_signal_battle_index(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    unsigned batch, unsigned index)
{
    return advance_ms(
            instance,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS) &&
        finish_active_signal_battle_index(
            instance, surface, batch, index);
}

static bool defeat_signal_index(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    unsigned batch, unsigned index)
{
    return enter_signal_battle_index(instance, batch, index) &&
        finish_signal_battle_index(instance, surface, batch, index);
}

static bool defeat_signal_range(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan,
    unsigned batch, unsigned first, unsigned limit)
{
    if (scan == NULL || batch >= 4U || first > limit || limit > 8U) {
        return false;
    }
    scan->batch = (uint8_t)batch;
    for (unsigned index = first; index < limit; ++index) {
        if (!defeat_signal_index(instance, surface, batch, index)) {
            fprintf(stderr,
                    "legacy collection failed at batch %u index %u\n",
                    batch, index);
            return false;
        }
    }
    return true;
}

static bool render_signal_attack_cycle(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    unsigned index, bool capture_retreat)
{
    static const char *const labels[BYTE_BUDDY_SIGNAL_ATTACK_COUNT] = {
        "arc", "prism", "thorn", "comet",
    };
    if (index >= BYTE_BUDDY_SIGNAL_ATTACK_COUNT ||
        !enter_signal_battle_index(instance, 0U, index)) {
        return false;
    }
    const byte_buddy_signal_encounter_t encounter =
        preview_signal_encounter(0U, index);
    if (encounter.attack != index) {
        fprintf(stderr, "signal attack family mismatch at %u\n", index);
        return false;
    }

    char label[64];
    int bytes = snprintf(label, sizeof(label), "%s-intro", labels[index]);
    if (bytes < 0 || (size_t)bytes >= sizeof(label) ||
        !render_motion_phase(
            writer, instance, surface, label,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS,
            -1, NULL, true, NULL)) {
        return false;
    }
    bytes = snprintf(label, sizeof(label), "%s-open", labels[index]);
    if (bytes < 0 || (size_t)bytes >= sizeof(label) ||
        !render_motion_phase(
            writer, instance, surface, label,
            preview_signal_open_ms(encounter), -1, NULL, true, NULL)) {
        return false;
    }
    bytes = snprintf(label, sizeof(label), "%s-windup", labels[index]);
    if (bytes < 0 || (size_t)bytes >= sizeof(label) ||
        !render_motion_phase(
            writer, instance, surface, label, encounter.telegraph_ms,
            index == BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST ? 4 : -1,
            NULL, true, NULL)) {
        return false;
    }
    bytes = snprintf(label, sizeof(label), "%s-travel", labels[index]);
    uint64_t representative = 0U;
    if (bytes < 0 || (size_t)bytes >= sizeof(label) ||
        !render_motion_phase(
            writer, instance, surface, label,
            PREVIEW_SIGNAL_TRAVEL_MS, -1, NULL, true,
            &representative)) {
        return false;
    }
    for (unsigned prior = 0U; prior < index; ++prior) {
        if (writer->attack_hash_valid[prior] &&
            writer->attack_hashes[prior] == representative) {
            fprintf(stderr,
                    "signal attack families %u and %u render identically\n",
                    prior, index);
            return false;
        }
    }
    writer->attack_hashes[index] = representative;
    writer->attack_hash_valid[index] = true;

    bytes = snprintf(label, sizeof(label), "%s-impact", labels[index]);
    if (bytes < 0 || (size_t)bytes >= sizeof(label) ||
        !render_motion_phase(
            writer, instance, surface, label,
            PREVIEW_SIGNAL_IMPACT_MS, -1, NULL, true, NULL)) {
        return false;
    }
    const bool guarded = index == BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST;
    const byte_buddy_signal_defense_t defense =
        byte_buddy_signal_defense(
            encounter, byte_buddy_dragon_ability(BYTE_BUDDY_ELEMENT_FIRE),
            guarded, 0U);
    bytes = snprintf(label, sizeof(label), "%s-recovery", labels[index]);
    if (bytes < 0 || (size_t)bytes >= sizeof(label) ||
        !render_motion_phase(
            writer, instance, surface, label,
            PREVIEW_SIGNAL_RECOVERY_MS + defense.delay_ms,
            -1, NULL, true, NULL)) {
        return false;
    }
    if (index == BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH) {
        return true;
    }

    if (!press_button(instance, P4_BUTTON_B)) {
        return false;
    }
    if (capture_retreat) {
        if (!render_motion_phase(
                writer, instance, surface, "retreat",
                PREVIEW_SIGNAL_RETREAT_REMAINING_MS,
                -1, NULL, true, NULL) ||
            !render_motion_frame(
                writer, instance, surface,
                "retreat-tracker", 0U, NULL)) {
            return false;
        }
    } else if (!advance_ms(
                   instance, PREVIEW_SIGNAL_RETREAT_REMAINING_MS)) {
        return false;
    }
    return settle_scene_transition(instance) &&
        press_button(instance, P4_BUTTON_B);
}

static bool render_until_signal_defeat(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    uint32_t *elapsed_out)
{
    uint32_t elapsed_ms = 0U;
    for (unsigned frame = 0U; frame < 345U; ++frame) {
        if (!render_motion_frame(
                writer, instance, surface,
                "comet-pressure", frame, NULL)) {
            return false;
        }
        if (surface_pixel(surface, 44U, 48U) == UINT16_C(0xf81f)) {
            if (elapsed_out != NULL) {
                *elapsed_out = elapsed_ms;
            }
            return true;
        }
        const uint32_t next_ms = preview_30hz_target_ms(frame);
        if (!advance_ms(instance, next_ms - elapsed_ms)) {
            return false;
        }
        elapsed_ms = next_ms;
    }
    fprintf(stderr, "Comet Crash did not produce an HP defeat\n");
    return false;
}

static bool render_pulse_victory(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface)
{
    if (!enter_signal_battle_index(instance, 0U, 0U) ||
        !advance_ms(
            instance,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS)) {
        return false;
    }
    bool victory = false;
    for (unsigned strike = 0U; strike < 32U; ++strike) {
        if (!tap(instance, 60U, 180U) ||
            !render_motion_frame(
                writer, instance, surface,
                "pulse-strike", strike, NULL)) {
            return false;
        }
        const uint16_t border = surface_pixel(surface, 44U, 48U);
        if (border == UINT16_C(0xffe0)) {
            victory = true;
            break;
        }
        if (border == UINT16_C(0xf81f) ||
            !render_motion_phase(
                writer, instance, surface, "pulse-cooldown",
                PREVIEW_SIGNAL_STRIKE_WAIT_MS,
                -1, NULL, false, NULL)) {
            return false;
        }
    }
    if (!victory ||
        !render_motion_phase(
            writer, instance, surface, "pulse-victory",
            PREVIEW_SIGNAL_VICTORY_MS - 16U,
            -1, NULL, true, NULL) ||
        !render_motion_frame(
            writer, instance, surface, "pulse-reward", 0U, NULL)) {
        fprintf(stderr, "Pulse Rush did not reach delayed victory\n");
        return false;
    }
    return true;
}

static bool render_resonance_victory(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface)
{
    const uint64_t token = preview_signal_token(0U, 1U);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(token, -43);
    const byte_buddy_signal_weave_rules_t rules =
        byte_buddy_signal_weave_rules(genome, profile.strength, 0U);
    if (!enter_signal_battle_index(instance, 0U, 1U) ||
        !advance_ms(
            instance,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS)) {
        return false;
    }
    for (uint8_t step = 0U; step < rules.required_locks; ++step) {
        const byte_buddy_signal_weave_node_t node =
            byte_buddy_signal_weave_node(genome, step);
        const p4_game_point_t touch = {.x = node.x, .y = node.y};
        char label[64];
        const int bytes = snprintf(
            label, sizeof(label), "resonance-lock-%u", step);
        if (bytes < 0 || (size_t)bytes >= sizeof(label) ||
            !render_motion_phase(
                writer, instance, surface, label, rules.hold_ms,
                -1, &touch, true, NULL)) {
            return false;
        }
        if ((uint8_t)(step + 1U) < rules.required_locks &&
            !render_motion_phase(
                writer, instance, surface, "resonance-settle",
                300U, -1, NULL, true, NULL)) {
            return false;
        }
    }
    if (!render_battle_result(instance, surface, UINT16_C(0xffe0)) ||
        !render_motion_phase(
            writer, instance, surface, "resonance-victory",
            PREVIEW_SIGNAL_VICTORY_MS,
            -1, NULL, true, NULL) ||
        !render_motion_frame(
            writer, instance, surface,
            "resonance-reward", 0U, NULL)) {
        fprintf(stderr, "Resonance Weave did not reach delayed victory\n");
        return false;
    }
    return true;
}

static bool restart_preview_game(
    p4_game_instance_t *instance, void *state,
    const p4_game_services_t *services,
    preview_signal_scan_t *scan, preview_scan_mode_t scan_mode)
{
    if (instance == NULL || state == NULL || services == NULL ||
        scan == NULL) {
        return false;
    }
    p4_game_instance_stop(instance);
    memset(state, 0, p4_byte_buddy_game.state_bytes);
    s_preview_care_index = 0U;
    *scan = (preview_signal_scan_t){.mode = (uint8_t)scan_mode};
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(
        instance, &p4_byte_buddy_game, services, state,
        p4_byte_buddy_game.state_bytes);
}

static const char *const s_runtime_action_names[
    PREVIEW_RUNTIME_ACTION_COUNT] = {
    "feed", "play", "clean", "rest", "pet", "grow", "signal",
};

static const char *const s_runtime_stage_names[
    PREVIEW_RUNTIME_STAGE_COUNT] = {
    "baby", "winged", "flying", "elemental",
};

static const char *const s_runtime_hatch_names[
    PREVIEW_RUNTIME_HATCH_MORPH_COUNT] = {
    "nebula", "sungold", "jade", "glacier",
};

static const char *const s_runtime_hatch_milestone_names[
    PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT] = {
    "first-crack", "peek", "emerge", "hatch",
};

static bool preview_profile_for_stage(
    unsigned stage_index, byte_buddy_save_profile_t *profile)
{
    if (profile == NULL || stage_index >= PREVIEW_RUNTIME_STAGE_COUNT) {
        return false;
    }
    const byte_buddy_stage_t wanted = (byte_buddy_stage_t)(
        BYTE_BUDDY_STAGE_BABY + stage_index);
    uint32_t growth = 0U;
    while (growth <= UINT16_MAX &&
           byte_buddy_stage_for_interactions((uint16_t)growth) != wanted) {
        ++growth;
    }
    if (growth > UINT16_MAX) {
        return false;
    }
    *profile = (byte_buddy_save_profile_t){
        .hunger = 100U,
        .joy = 100U,
        .hygiene = 100U,
        .energy = 100U,
        .coins = 200U,
        .care_actions = (uint16_t)growth,
    };
    return byte_buddy_save_profile_valid(profile);
}

static bool restart_preview_profile(
    p4_game_instance_t *instance, void *state,
    const p4_game_services_t *services, preview_signal_scan_t *scan,
    preview_save_host_t *save,
    const byte_buddy_save_profile_t *profile)
{
    if (save == NULL || profile == NULL) {
        return false;
    }
    memset(save, 0, sizeof(*save));
    if (byte_buddy_save_encode(
            profile, save->payload, sizeof(save->payload)) !=
        sizeof(save->payload)) {
        return false;
    }
    p4_game_services_t scenario = *services;
    scenario.available_capabilities |= P4_GAME_CAP_SAVE;
    scenario.save_context = save;
    scenario.save_data = save->payload;
    scenario.save_bytes = sizeof(save->payload);
    scenario.save_schema_version = BYTE_BUDDY_SAVE_SCHEMA_VERSION;
    scenario.save_sequence = 1U;
    scenario.queue_save = preview_queue_save;
    scenario.read_save_status = preview_read_save_status;
    return restart_preview_game(
        instance, state, &scenario, scan, PREVIEW_SCAN_READY);
}

static bool preview_home_header_hash(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    uint64_t *hash)
{
    const preview_roi_t home_header = {48U, 3U, 122U, 22U};
    return capture_region_hash(instance, surface, home_header, hash);
}

static bool preview_runtime_render_frame(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    const char *label, unsigned local_frame, preview_roi_t roi,
    uint64_t hashes[PREVIEW_RUNTIME_COMPLETION_FRAMES],
    uint64_t *hash_out)
{
    if (writer == NULL ||
        writer->frame_count >= PREVIEW_RUNTIME_COMPLETION_FRAMES) {
        return false;
    }
    const unsigned ordinal = writer->frame_count;
    uint64_t hash = 0U;
    if (!render_motion_frame_roi(
            writer, instance, surface, label, local_frame, roi, &hash)) {
        return false;
    }
    hashes[ordinal] = hash;
    if (hash_out != NULL) {
        *hash_out = hash;
    }
    return true;
}

static bool preview_runtime_render_phases(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    const char *label, preview_roi_t roi,
    uint64_t all_hashes[PREVIEW_RUNTIME_COMPLETION_FRAMES],
    uint64_t phase_hashes[PREVIEW_RUNTIME_PHASE_COUNT])
{
    /* The wide ROI retains large Elemental FX.  This second ROI excludes the
     * metadata panel and proves the composited dragon body itself moves. */
    const preview_roi_t body_roi = {125U, 42U, 198U, 123U};
    uint64_t body_hashes[PREVIEW_RUNTIME_PHASE_COUNT] = {0};
    for (unsigned phase = 0U; phase < PREVIEW_RUNTIME_PHASE_COUNT;
         ++phase) {
        if (!preview_runtime_render_frame(
                writer, instance, surface, label, phase, roi,
                all_hashes, &phase_hashes[phase])) {
            return false;
        }
        body_hashes[phase] = surface_region_hash(
            surface, body_roi.left, body_roi.top,
            body_roi.right, body_roi.bottom);
        if (body_hashes[phase] == 0U ||
            (phase + 1U < PREVIEW_RUNTIME_PHASE_COUNT &&
             !advance_ms(instance, PREVIEW_RUNTIME_PHASE_STEP_MS))) {
            return false;
        }
    }
    return hashes_are_pairwise_distinct(
               phase_hashes, PREVIEW_RUNTIME_PHASE_COUNT, label) &&
        hashes_are_pairwise_distinct(
            body_hashes, PREVIEW_RUNTIME_PHASE_COUNT, label);
}

static bool preview_runtime_sequences_distinct(
    const uint64_t *hashes, size_t sequence_count,
    size_t sequence_length, const char *label)
{
    if (hashes == NULL || label == NULL || sequence_length == 0U) {
        return false;
    }
    for (size_t sequence = 0U; sequence < sequence_count; ++sequence) {
        for (size_t prior = 0U; prior < sequence; ++prior) {
            if (memcmp(
                    hashes + sequence * sequence_length,
                    hashes + prior * sequence_length,
                    sequence_length * sizeof(*hashes)) == 0) {
                fprintf(stderr,
                        "%s sequences %zu and %zu render identically\n",
                        label, prior, sequence);
                return false;
            }
        }
    }
    return true;
}

static bool preview_runtime_digest_matches(
    const char *kind, const char *label, uint64_t actual,
    uint64_t expected, bool review)
{
    fprintf(stdout,
            "runtime-completion-%s label=%s digest=%016llx\n",
            kind, label, (unsigned long long)actual);
    if (actual == expected) {
        return true;
    }
    fprintf(stderr,
            "runtime completion %s %s digest %016llx, expected %016llx\n",
            kind, label, (unsigned long long)actual,
            (unsigned long long)expected);
    return review;
}

static bool preview_trigger_runtime_action(
    p4_game_instance_t *instance, unsigned action)
{
    static const uint16_t care_points[5][2] = {
        {20U, 145U}, {110U, 145U}, {180U, 145U},
        {280U, 145U}, {160U, 80U},
    };
    if (action < 5U) {
        return tap(instance, care_points[action][0], care_points[action][1]);
    }
    if (action != 5U) {
        return false;
    }
    /* Grow is production-reachable at every stage through a power upgrade.
     * The reaction pauses in the shop and begins only after Home returns. */
    return tap(instance, 180U, 180U) &&
        settle_scene_transition(instance) &&
        /* Magnet changes no visible dragon customization behind the clip. */
        tap(instance, 200U, 130U) &&
        tap(instance, 240U, 180U) &&
        settle_scene_transition(instance);
}

static bool preview_hatch_profile(
    unsigned morph, unsigned milestone,
    byte_buddy_save_profile_t *profile, uint16_t *resulting_care)
{
    if (profile == NULL || resulting_care == NULL ||
        morph >= PREVIEW_RUNTIME_HATCH_MORPH_COUNT ||
        milestone >= PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT) {
        return false;
    }
    byte_buddy_save_profile_t baby_profile;
    if (!preview_profile_for_stage(0U, &baby_profile) ||
        baby_profile.care_actions < PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT) {
        return false;
    }
    const uint16_t care_after = (uint16_t)(
        baby_profile.care_actions -
        (PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT - 1U - milestone));
    *profile = (byte_buddy_save_profile_t){
        .hunger = 60U,
        .joy = 60U,
        .hygiene = 60U,
        .energy = 60U,
        .coins = 200U,
        .care_actions = (uint16_t)(care_after - 1U),
    };
    switch ((byte_buddy_morph_t)morph) {
    case BYTE_BUDDY_MORPH_NEBULA:
        profile->pet_actions = profile->care_actions;
        break;
    case BYTE_BUDDY_MORPH_SUNGOLD:
        profile->action_counts[BYTE_BUDDY_CARE_FEED] =
            profile->care_actions;
        break;
    case BYTE_BUDDY_MORPH_JADE:
        profile->action_counts[BYTE_BUDDY_CARE_PLAY] =
            profile->care_actions;
        break;
    case BYTE_BUDDY_MORPH_GLACIER:
        profile->action_counts[BYTE_BUDDY_CARE_CLEAN] =
            profile->care_actions;
        break;
    default:
        return false;
    }
    *resulting_care = care_after;
    return byte_buddy_save_profile_valid(profile);
}

static bool preview_trigger_hatch_morph(
    p4_game_instance_t *instance, unsigned morph)
{
    static const uint16_t hatch_points[
        PREVIEW_RUNTIME_HATCH_MORPH_COUNT][2] = {
        {160U, 80U},  /* Nebula: Pet */
        {20U, 145U},  /* Sungold: Feed */
        {110U, 145U}, /* Jade: Play */
        {180U, 145U}, /* Glacier: Clean */
    };
    return morph < PREVIEW_RUNTIME_HATCH_MORPH_COUNT &&
        tap(instance, hatch_points[morph][0], hatch_points[morph][1]);
}

static bool render_runtime_completion_sequence(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services, const char *prefix,
    unsigned expected_frames, bool review)
{
    static const uint64_t expected_action_digests[
        PREVIEW_RUNTIME_ACTION_COUNT][PREVIEW_RUNTIME_STAGE_COUNT] = {
        {
            UINT64_C(0x933854eaf07d1538),
            UINT64_C(0x284a305b5fd4dcad),
            UINT64_C(0xd4389f568482d80b),
            UINT64_C(0xc6395026ad050a60),
        },
        {
            UINT64_C(0xa9fb69926e7da4a7),
            UINT64_C(0x02046dbd9e83c0dd),
            UINT64_C(0x92c18ccab2d2a937),
            UINT64_C(0xda32783cbe6a4b79),
        },
        {
            UINT64_C(0x94805879154f7eb3),
            UINT64_C(0xff864265515d6d05),
            UINT64_C(0xe9b044c7cf4f9143),
            UINT64_C(0xcd6ea1cc99625496),
        },
        {
            UINT64_C(0x8da9449a17304bc0),
            UINT64_C(0x5be0a7548b5c3db4),
            UINT64_C(0x7a9be90ac7d7e2ad),
            UINT64_C(0xea07d8ea0a0c7fdb),
        },
        {
            UINT64_C(0x4a4851223ffd131f),
            UINT64_C(0xf484167ea7935d55),
            UINT64_C(0x70546ca9aa45b14a),
            UINT64_C(0x71993aeefa85be55),
        },
        {
            UINT64_C(0xd1438f2285ab133d),
            UINT64_C(0x9821543d185eb90e),
            UINT64_C(0xc16d123fa98f62d6),
            UINT64_C(0x0abea6d03f4e7c6d),
        },
        {
            UINT64_C(0xab138d9e4d1c326a),
            UINT64_C(0x245a8dc640795042),
            UINT64_C(0x1a6fbe48b96338d6),
            UINT64_C(0xccaef9dcab8abb51),
        },
    };
    static const uint64_t expected_hatch_digests[
        PREVIEW_RUNTIME_HATCH_MORPH_COUNT]
        [PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT] = {
        {
            UINT64_C(0x462759f99baf41f2),
            UINT64_C(0x8552cb8d1510b3fa),
            UINT64_C(0x23180c8dc368c2f9),
            UINT64_C(0xa8dc8d147866e437),
        },
        {
            UINT64_C(0xde7023a752f62f67),
            UINT64_C(0x1650e6c2c8158bb5),
            UINT64_C(0x332d83a4a8a20d98),
            UINT64_C(0x300f122d997d8bb3),
        },
        {
            UINT64_C(0xd8964ba1cd1c4f03),
            UINT64_C(0x84e835c6e61e0550),
            UINT64_C(0x0f6cf6404ea83636),
            UINT64_C(0x15afea675250f0da),
        },
        {
            UINT64_C(0x2950bd1207926d35),
            UINT64_C(0xc1625677f89715c0),
            UINT64_C(0x7b9b720bb6f0776c),
            UINT64_C(0x2202131d16c62ddc),
        },
    };
    static const uint64_t expected_signal_list_hashes[
        PREVIEW_RUNTIME_STAGE_COUNT] = {
        UINT64_C(0x1808da7dec99a080),
        UINT64_C(0x5c3403699cfca6b9),
        UINT64_C(0xadf775cf1c2a9a50),
        UINT64_C(0x4f2d039d35d165ad),
    };
    static const uint64_t expected_all_digest =
        UINT64_C(0x97262b8282c5b682);
    const preview_roi_t dragon_roi = {118U, 40U, 210U, 130U};
    const preview_roi_t full_surface = {
        0U, 0U, P4_GAME_SURFACE_WIDTH, P4_GAME_SURFACE_HEIGHT,
    };
    preview_motion_writer_t writer = {
        .prefix = prefix,
        .require_semantic_digests = false,
        .print_semantic_digests = review,
    };
    uint64_t all_hashes[PREVIEW_RUNTIME_COMPLETION_FRAMES] = {0};
    uint64_t action_hashes[PREVIEW_RUNTIME_ACTION_COUNT]
        [PREVIEW_RUNTIME_STAGE_COUNT][PREVIEW_RUNTIME_PHASE_COUNT] = {{{0}}};
    uint64_t hatch_hashes[PREVIEW_RUNTIME_HATCH_MORPH_COUNT]
        [PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT]
        [PREVIEW_RUNTIME_PHASE_COUNT] = {{{0}}};
    uint64_t signal_list_hashes[PREVIEW_RUNTIME_STAGE_COUNT] = {0};
    preview_save_host_t save = {0};
    bool success = true;

    for (unsigned action = 0U;
         success && action < PREVIEW_RUNTIME_ACTION_COUNT; ++action) {
        for (unsigned stage = 0U;
             success && stage < PREVIEW_RUNTIME_STAGE_COUNT; ++stage) {
            byte_buddy_save_profile_t profile;
            uint64_t home_header_before = 0U;
            uint64_t home_header_after = 0U;
            char label[96];
            const int label_bytes = snprintf(
                label, sizeof(label), "runtime-action-%s-%s",
                s_runtime_action_names[action],
                s_runtime_stage_names[stage]);
            if (label_bytes < 0 || (size_t)label_bytes >= sizeof(label) ||
                !preview_profile_for_stage(stage, &profile) ||
                !restart_preview_profile(
                    instance, state, services, scan, &save, &profile) ||
                !preview_home_header_hash(
                    instance, surface, &home_header_before)) {
                success = false;
                break;
            }
            if (action == 6U) {
                char list_label[112];
                const int list_bytes = snprintf(
                    list_label, sizeof(list_label), "%s-list-hold", label);
                if (list_bytes < 0 ||
                    (size_t)list_bytes >= sizeof(list_label) ||
                    !defeat_signal_index(instance, surface, 0U, 0U) ||
                    !settle_scene_transition(instance) ||
                    !screen_signature_matches(
                        instance, surface, PREVIEW_SCREEN_SIGNAL_LIST) ||
                    !advance_ms(
                        instance, PREVIEW_RUNTIME_SIGNAL_LIST_HOLD_MS) ||
                    !screen_signature_matches(
                        instance, surface, PREVIEW_SCREEN_SIGNAL_LIST) ||
                    !preview_runtime_render_frame(
                        &writer, instance, surface, list_label, 0U,
                        full_surface, all_hashes,
                        &signal_list_hashes[stage]) ||
                    !tap(instance, 20U, 12U) ||
                    !settle_scene_transition(instance)) {
                    success = false;
                    break;
                }
                success = preview_runtime_digest_matches(
                    "signal-list", s_runtime_stage_names[stage],
                    signal_list_hashes[stage],
                    expected_signal_list_hashes[stage], review);
            } else {
                success = preview_trigger_runtime_action(instance, action);
            }
            if (!success ||
                !preview_home_header_hash(
                    instance, surface, &home_header_after) ||
                home_header_after != home_header_before ||
                !preview_runtime_render_phases(
                    &writer, instance, surface, label, dragon_roi,
                    all_hashes, action_hashes[action][stage])) {
                if (success && home_header_after != home_header_before) {
                    fprintf(stderr,
                            "%s did not render on Home (%016llx != "
                            "%016llx)\n",
                            label,
                            (unsigned long long)home_header_after,
                            (unsigned long long)home_header_before);
                }
                success = false;
                break;
            }
            const uint64_t digest = preview_ordered_hash_digest(
                action_hashes[action][stage], PREVIEW_RUNTIME_PHASE_COUNT);
            success = preview_runtime_digest_matches(
                "action", label, digest,
                expected_action_digests[action][stage], review);
        }
    }

    for (unsigned action = 0U;
         success && action < PREVIEW_RUNTIME_ACTION_COUNT; ++action) {
        success = preview_runtime_sequences_distinct(
            &action_hashes[action][0U][0U],
            PREVIEW_RUNTIME_STAGE_COUNT, PREVIEW_RUNTIME_PHASE_COUNT,
            s_runtime_action_names[action]);
    }
    for (unsigned stage = 0U;
         success && stage < PREVIEW_RUNTIME_STAGE_COUNT; ++stage) {
        uint64_t stage_sequences[PREVIEW_RUNTIME_ACTION_COUNT]
            [PREVIEW_RUNTIME_PHASE_COUNT] = {{0}};
        for (unsigned action = 0U;
             action < PREVIEW_RUNTIME_ACTION_COUNT; ++action) {
            memcpy(stage_sequences[action], action_hashes[action][stage],
                   sizeof(stage_sequences[action]));
        }
        success = preview_runtime_sequences_distinct(
            &stage_sequences[0U][0U], PREVIEW_RUNTIME_ACTION_COUNT,
            PREVIEW_RUNTIME_PHASE_COUNT, s_runtime_stage_names[stage]);
    }

    for (unsigned morph = 0U;
         success && morph < PREVIEW_RUNTIME_HATCH_MORPH_COUNT; ++morph) {
        for (unsigned milestone = 0U;
             success && milestone <
                 PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT; ++milestone) {
            byte_buddy_save_profile_t profile;
            uint16_t care_after = 0U;
            char label[96];
            const int label_bytes = snprintf(
                label, sizeof(label), "runtime-hatch-%s-%s",
                s_runtime_hatch_names[morph],
                s_runtime_hatch_milestone_names[milestone]);
            if (label_bytes < 0 || (size_t)label_bytes >= sizeof(label) ||
                !preview_hatch_profile(
                    morph, milestone, &profile, &care_after) ||
                !restart_preview_profile(
                    instance, state, services, scan, &save, &profile) ||
                !preview_trigger_hatch_morph(instance, morph)) {
                success = false;
                break;
            }
            uint16_t counts[5] = {
                profile.action_counts[0], profile.action_counts[1],
                profile.action_counts[2], profile.action_counts[3],
                profile.pet_actions,
            };
            ++counts[morph == BYTE_BUDDY_MORPH_NEBULA ? 4U :
                morph == BYTE_BUDDY_MORPH_SUNGOLD ? 0U :
                morph == BYTE_BUDDY_MORPH_JADE ? 1U : 2U];
            if (byte_buddy_morph_for_nurture(
                    counts[0], counts[1], counts[2], counts[3],
                    counts[4]) != (byte_buddy_morph_t)morph ||
                byte_buddy_stage_for_interactions(care_after) !=
                    (milestone + 1U ==
                            PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT
                        ? BYTE_BUDDY_STAGE_BABY
                        : BYTE_BUDDY_STAGE_EGG) ||
                !preview_runtime_render_phases(
                    &writer, instance, surface, label, dragon_roi,
                    all_hashes, hatch_hashes[morph][milestone])) {
                success = false;
                break;
            }
            const uint64_t digest = preview_ordered_hash_digest(
                hatch_hashes[morph][milestone],
                PREVIEW_RUNTIME_PHASE_COUNT);
            success = preview_runtime_digest_matches(
                "hatch", label, digest,
                expected_hatch_digests[morph][milestone], review);
        }
    }
    if (success) {
        success = preview_runtime_sequences_distinct(
            &hatch_hashes[0U][0U][0U],
            PREVIEW_RUNTIME_HATCH_MORPH_COUNT *
                PREVIEW_RUNTIME_HATCH_MILESTONE_COUNT,
            PREVIEW_RUNTIME_PHASE_COUNT, "runtime hatch");
    }

    const uint64_t all_digest = preview_ordered_hash_digest(
        all_hashes, writer.frame_count);
    if (success) {
        success = preview_runtime_digest_matches(
            "all", "ordered-180", all_digest, expected_all_digest, review);
    }
    fprintf(stdout, "runtime-completion-expected-frames=%u\n",
            PREVIEW_RUNTIME_COMPLETION_FRAMES);
    fprintf(stdout, "runtime-completion-frames=%u\n", writer.frame_count);
    if (expected_frames != PREVIEW_RUNTIME_COMPLETION_FRAMES ||
        writer.frame_count != PREVIEW_RUNTIME_COMPLETION_FRAMES) {
        fprintf(stderr,
                "expected runtime-completion argument/count %u/%u, "
                "got %u/%u\n",
                PREVIEW_RUNTIME_COMPLETION_FRAMES,
                PREVIEW_RUNTIME_COMPLETION_FRAMES,
                expected_frames, writer.frame_count);
        success = false;
    }
    return success;
}

static bool render_counter_coverage(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    static const char *const labels[BYTE_BUDDY_ABILITY_COUNT] = {
        "counter-nova", "counter-flare", "counter-glacier", "counter-jam",
    };
    uint64_t row_hashes[BYTE_BUDDY_ABILITY_COUNT] = {0};
    const preview_roi_t roi = {210U, 50U, 282U, 122U};
    for (unsigned ability = 0U; ability < BYTE_BUDDY_ABILITY_COUNT;
         ++ability) {
        if (!restart_preview_game(
                instance, state, services, scan, PREVIEW_SCAN_READY)) {
            return false;
        }
        if (ability == BYTE_BUDDY_ABILITY_FLARE_COUNTER &&
            !tap(instance, 20U, 145U)) {
            return false;
        }
        if (ability == BYTE_BUDDY_ABILITY_GLACIER_WARD &&
            !tap(instance, 180U, 145U)) {
            return false;
        }
        if (ability == BYTE_BUDDY_ABILITY_JAM_FIELD) {
            if (!tap(instance, 110U, 145U) ||
                !advance_ms(instance, PREVIEW_PLAY_ACTION_AFTER_TAP_MS) ||
                !press_button(instance, P4_BUTTON_B) ||
                !advance_ms(instance, 900U)) {
                return false;
            }
        }
        const byte_buddy_signal_encounter_t encounter =
            preview_signal_encounter(0U, 0U);
        if (!enter_signal_battle_index(instance, 0U, 0U) ||
            !advance_ms(
                instance,
                PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS) ||
            !advance_ms(instance, preview_signal_open_ms(encounter)) ||
            !press_button(instance, P4_BUTTON_START)) {
            return false;
        }
        uint64_t phases[4] = {0};
        if (!render_motion_frame_roi(
                writer, instance, surface, labels[ability], 0U, roi,
                &phases[0]) ||
            !advance_ms(instance, encounter.telegraph_ms / 2U + 16U) ||
            !render_motion_frame_roi(
                writer, instance, surface, labels[ability], 1U, roi,
                &phases[1]) ||
            !advance_ms(instance, encounter.telegraph_ms / 2U + 32U) ||
            !render_motion_frame_roi(
                writer, instance, surface, labels[ability], 2U, roi,
                &phases[2]) ||
            !advance_ms(
                instance,
                PREVIEW_SIGNAL_TRAVEL_MS + PREVIEW_SIGNAL_IMPACT_MS) ||
            !render_motion_frame_roi(
                writer, instance, surface, labels[ability], 3U, roi,
                &phases[3]) ||
            !hashes_are_pairwise_distinct(phases, 4U, labels[ability])) {
            return false;
        }
        row_hashes[ability] = phases[3];
    }
    return hashes_are_pairwise_distinct(
        row_hashes, BYTE_BUDDY_ABILITY_COUNT, "counter rows");
}

static bool render_passive_coverage(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    static const char *const labels[BYTE_BUDDY_SIGNAL_PASSIVE_COUNT] = {
        "passive-ward", "passive-echo", "passive-siphon",
        "passive-overclock",
    };
    uint64_t row_hashes[BYTE_BUDDY_SIGNAL_PASSIVE_COUNT] = {0};
    const preview_roi_t roi = {53U, 62U, 103U, 112U};
    for (unsigned passive = 0U;
         passive < BYTE_BUDDY_SIGNAL_PASSIVE_COUNT; ++passive) {
        const bool custom_siphon =
            passive == BYTE_BUDDY_SIGNAL_PASSIVE_SIPHON;
        unsigned signal_index = 0U;
        while (!custom_siphon && signal_index < 4U &&
               preview_signal_encounter(0U, signal_index).passive != passive) {
            ++signal_index;
        }
        if (signal_index == 4U ||
            !restart_preview_game(
                instance, state, services, scan, PREVIEW_SCAN_READY)) {
            return false;
        }
        uint64_t token = preview_signal_token(0U, signal_index);
        if (custom_siphon) {
            token = controlled_signal_token(0U, 0U, 0U, 2U, 0U, 0U);
            scan->first_token_override = token;
            scan->first_channel_override = 1U;
            scan->first_flags_override = 0U;
        }
        if (!enter_signal_battle_index(instance, 0U, signal_index) ||
            !settle_scene_transition(instance)) {
            return false;
        }
        const byte_buddy_signal_encounter_t encounter = custom_siphon
            ? byte_buddy_signal_encounter(
                token, -43, 1U, P4_GAME_SIGNAL_SIMULATED)
            : preview_signal_encounter(0U, signal_index);
        const uint32_t intro_after_transition =
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS -
            PREVIEW_SCENE_TRANSITION_MS;
        const uint32_t active_intro_ms = 120U;
        uint64_t phases[4] = {0};
        if (!render_motion_frame_roi(
                writer, instance, surface, labels[passive], 0U, roi,
                &phases[0]) ||
            !advance_ms(
                instance, intro_after_transition - active_intro_ms) ||
            !render_motion_frame_roi(
                writer, instance, surface, labels[passive], 1U, roi,
                &phases[1]) ||
            !advance_ms(instance, active_intro_ms)) {
            return false;
        }
        bool triggered = false;
        switch ((byte_buddy_signal_passive_t)passive) {
        case BYTE_BUDDY_SIGNAL_PASSIVE_WARD:
            triggered = advance_ms(
                    instance, preview_signal_open_ms(encounter)) &&
                press_button(instance, P4_BUTTON_START) &&
                advance_ms(instance,
                           (uint32_t)encounter.telegraph_ms +
                               PREVIEW_SIGNAL_TRAVEL_MS);
            break;
        case BYTE_BUDDY_SIGNAL_PASSIVE_ECHO:
            triggered = advance_ms(
                instance,
                (uint32_t)encounter.attack_period_ms * 2U +
                    preview_signal_open_ms(encounter));
            break;
        case BYTE_BUDDY_SIGNAL_PASSIVE_SIPHON:
        {
            const uint32_t open_ms = preview_signal_open_ms(encounter);
            const uint32_t settle_ms = open_ms / 2U;
            triggered = tap(instance, 60U, 180U) &&
                advance_ms(instance, settle_ms) &&
                p4_game_instance_render(instance, surface);
            const unsigned damaged_hp_pixels = triggered
                ? count_surface_color(
                    surface, 145U, 127U, 226U, 133U,
                    UINT16_C(0xf81f))
                : 0U;
            triggered = triggered &&
                advance_ms(
                    instance, open_ms - settle_ms +
                        (uint32_t)encounter.telegraph_ms +
                        PREVIEW_SIGNAL_TRAVEL_MS) &&
                p4_game_instance_render(instance, surface);
            const unsigned healed_hp_pixels = triggered
                ? count_surface_color(
                    surface, 145U, 127U, 226U, 133U,
                    UINT16_C(0xf81f))
                : 0U;
            if (healed_hp_pixels <= damaged_hp_pixels) {
                fprintf(stderr,
                        "Siphon passive did not restore visible enemy HP "
                        "(%u -> %u pixels)\n",
                        damaged_hp_pixels, healed_hp_pixels);
                triggered = false;
            }
            break;
        }
        case BYTE_BUDDY_SIGNAL_PASSIVE_OVERCLOCK:
            triggered = advance_ms(
                instance, preview_signal_open_ms(encounter));
            break;
        default:
            break;
        }
        if (!triggered ||
            !render_motion_frame_roi(
                writer, instance, surface, labels[passive], 2U, roi,
                &phases[2]) ||
            !advance_ms(instance, PREVIEW_SIGNAL_PASSIVE_FX_MS * 2U / 3U) ||
            !render_motion_frame_roi(
                writer, instance, surface, labels[passive], 3U, roi,
                &phases[3]) ||
            !hashes_are_pairwise_distinct(
                phases, 4U, labels[passive])) {
            return false;
        }
        row_hashes[passive] = phases[2];
    }
    return hashes_are_pairwise_distinct(
        row_hashes, BYTE_BUDDY_SIGNAL_PASSIVE_COUNT, "passive rows");
}

static uint64_t controlled_signal_token(
    uint8_t core, uint8_t halo, uint8_t sigil,
    uint8_t aura, uint8_t hue, uint8_t rarity)
{
    static const uint16_t rarity_rolls[4] = {
        UINT16_C(0x0200), UINT16_C(0x0080),
        UINT16_C(0x0010), UINT16_C(0x0001),
    };
    const uint8_t bounded_rarity = rarity < 4U ? rarity : 0U;
    return UINT64_C(0x5a00000000000000) |
        ((uint64_t)(core & 3U) << 21U) |
        ((uint64_t)(halo & 3U) << 23U) |
        ((uint64_t)(aura & 3U) << 25U) |
        ((uint64_t)(sigil & 3U) << 27U) |
        ((uint64_t)(hue & 7U) << 18U) |
        rarity_rolls[bounded_rarity];
}

static bool render_list_seed_hash(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services, uint64_t token,
    uint8_t channel, uint8_t flags, uint64_t *hash)
{
    const preview_roi_t roi = {6U, 32U, 29U, 55U};
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY)) {
        return false;
    }
    scan->first_token_override = token;
    scan->first_channel_override = channel;
    scan->first_flags_override = flags;
    return tap(instance, 100U, 180U) &&
        settle_scene_transition(instance) &&
        capture_region_hash(instance, surface, roi, hash);
}

static bool verify_signal_seed_visual_distinction(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    uint64_t hashes[20] = {0};
    for (uint8_t core = 0U; core < 4U; ++core) {
        if (!render_list_seed_hash(
                instance, surface, scan, state, services,
                controlled_signal_token(core, 0U, 0U, 0U, 0U, 0U),
                1U, 0U, &hashes[core])) {
            return false;
        }
    }
    if (!hashes_are_pairwise_distinct(hashes, 4U, "list seed cores")) {
        return false;
    }
    for (uint8_t halo = 0U; halo < 4U; ++halo) {
        if (!render_list_seed_hash(
                instance, surface, scan, state, services,
                controlled_signal_token(0U, halo, 0U, 0U, 0U, 0U),
                1U, 0U, &hashes[halo])) {
            return false;
        }
    }
    if (!hashes_are_pairwise_distinct(hashes, 4U, "list seed halos")) {
        return false;
    }
    for (uint8_t sigil = 0U; sigil < 4U; ++sigil) {
        if (!render_list_seed_hash(
                instance, surface, scan, state, services,
                controlled_signal_token(0U, 0U, sigil, 0U, 0U, 0U),
                1U, 0U, &hashes[sigil])) {
            return false;
        }
    }
    if (!hashes_are_pairwise_distinct(hashes, 4U, "list seed sigils")) {
        return false;
    }
    for (uint8_t aura = 0U; aura < 4U; ++aura) {
        if (!render_list_seed_hash(
                instance, surface, scan, state, services,
                controlled_signal_token(0U, 0U, 0U, aura, 0U, 0U),
                1U, 0U, &hashes[aura])) {
            return false;
        }
    }
    if (!hashes_are_pairwise_distinct(hashes, 4U, "list seed auras")) {
        return false;
    }
    for (uint8_t hue = 0U; hue < 8U; ++hue) {
        if (!render_list_seed_hash(
                instance, surface, scan, state, services,
                controlled_signal_token(0U, 0U, 0U, 0U, hue, 0U),
                1U, 0U, &hashes[hue])) {
            return false;
        }
    }
    if (!hashes_are_pairwise_distinct(hashes, 8U, "list seed hues")) {
        return false;
    }
    for (uint8_t rarity = 0U; rarity < 4U; ++rarity) {
        if (!render_list_seed_hash(
                instance, surface, scan, state, services,
                controlled_signal_token(0U, 0U, 0U, 0U, 0U, rarity),
                1U, 0U, &hashes[rarity])) {
            return false;
        }
    }
    if (!hashes_are_pairwise_distinct(hashes, 4U, "list seed rarities")) {
        return false;
    }
    static const uint8_t channels[5] = {1U, 6U, 11U, 36U, 0U};
    static const uint8_t flag_sets[4] = {
        0U,
        P4_GAME_SIGNAL_PROTECTED,
        P4_GAME_SIGNAL_HIDDEN,
        P4_GAME_SIGNAL_PROTECTED | P4_GAME_SIGNAL_HIDDEN,
    };
    unsigned context = 0U;
    const uint64_t token = controlled_signal_token(
        0U, 0U, 0U, 0U, 0U, 0U);
    for (size_t flags = 0U; flags < 4U; ++flags) {
        for (size_t channel = 0U; channel < 5U; ++channel) {
            if (!render_list_seed_hash(
                    instance, surface, scan, state, services, token,
                    channels[channel], flag_sets[flags],
                    &hashes[context])) {
                return false;
            }
            ++context;
        }
    }
    return hashes_are_pairwise_distinct(
        hashes, 20U, "list seed habitats");
}

static bool begin_pulse_victory(
    p4_game_instance_t *instance, p4_game_surface_t *surface)
{
    if (!enter_signal_battle_index(instance, 0U, 0U) ||
        !advance_ms(
            instance,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS)) {
        return false;
    }
    for (unsigned strike = 0U; strike < 32U; ++strike) {
        if (!tap(instance, 60U, 180U) ||
            !p4_game_instance_render(instance, surface)) {
            return false;
        }
        const uint16_t border = surface_pixel(surface, 44U, 48U);
        if (border == UINT16_C(0xffe0)) {
            return true;
        }
        if (border == UINT16_C(0xf81f) ||
            !advance_ms(instance, PREVIEW_SIGNAL_STRIKE_WAIT_MS)) {
            return false;
        }
    }
    return false;
}

static bool wait_for_defeat(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    bool guard_every_step, uint32_t maximum_ms)
{
    uint32_t elapsed_ms = 0U;
    while (elapsed_ms < maximum_ms) {
        if (guard_every_step &&
            !press_button(instance, P4_BUTTON_START)) {
            return false;
        }
        if (!advance_ms(instance, 68U) ||
            !p4_game_instance_render(instance, surface)) {
            return false;
        }
        if (surface_pixel(surface, 44U, 48U) == UINT16_C(0xf81f)) {
            return true;
        }
        elapsed_ms += 100U;
    }
    return false;
}

static bool render_outcome_coverage(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    uint64_t row_hashes[4] = {0};
    const preview_roi_t outcome_roi = {48U, 54U, 108U, 114U};

    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !begin_pulse_victory(instance, surface) ||
        !render_sampled_phase(
            writer, instance, surface, "outcome-victory", 800U, 4U,
            outcome_roi, 4U, &row_hashes[0]) ||
        !advance_ms(instance, 100U) ||
        !settle_scene_transition(instance) ||
        !render_sampled_phase(
            writer, instance, surface, "evolution-genome", 1100U, 4U,
            (preview_roi_t){70U, 76U, 118U, 124U}, 4U, NULL)) {
        return false;
    }

    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !enter_signal_battle_index(instance, 0U, 3U) ||
        !advance_ms(
            instance,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS) ||
        !wait_for_defeat(instance, surface, false, 12000U) ||
        !render_sampled_phase(
            writer, instance, surface, "outcome-hp", 650U, 4U,
            outcome_roi, 4U, &row_hashes[1])) {
        return false;
    }

    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !care_credits(instance, 104U) ||
        !tap(instance, 180U, 145U) ||
        !advance_ms(instance, 1400U) ||
        !enter_signal_battle_index(instance, 0U, 0U) ||
        !advance_ms(
            instance,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS) ||
        !wait_for_defeat(instance, surface, true, 14000U) ||
        !render_sampled_phase(
            writer, instance, surface, "outcome-timeout", 650U, 4U,
            outcome_roi, 4U, &row_hashes[2])) {
        return false;
    }

    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !enter_signal_battle_index(instance, 0U, 0U) ||
        !advance_ms(
            instance,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS) ||
        !press_button(instance, P4_BUTTON_B) ||
        !render_sampled_phase(
            writer, instance, surface, "outcome-retreat", 420U, 4U,
            outcome_roi, 4U, &row_hashes[3])) {
        return false;
    }
    return hashes_are_pairwise_distinct(row_hashes, 4U, "outcome rows");
}

static bool render_scan_coverage(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    static const preview_scan_mode_t modes[4] = {
        PREVIEW_SCAN_SCANNING, PREVIEW_SCAN_BUSY,
        PREVIEW_SCAN_OFFLINE, PREVIEW_SCAN_EMPTY,
    };
    static const char *const labels[4] = {
        "scan-scanning", "scan-busy", "scan-offline", "scan-empty",
    };
    uint64_t row_hashes[4] = {0};
    const preview_roi_t art_roi = {128U, 32U, 193U, 101U};
    for (size_t row = 0U; row < 4U; ++row) {
        if (!restart_preview_game(
                instance, state, services, scan, modes[row]) ||
            !tap(instance, 100U, 180U) ||
            !settle_scene_transition(instance) ||
            !render_sampled_phase(
                writer, instance, surface, labels[row], 600U, 4U,
                art_roi, 4U, &row_hashes[row])) {
            return false;
        }
    }
    if (!hashes_are_pairwise_distinct(row_hashes, 4U, "scan rows") ||
        !restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !tap(instance, 100U, 180U) ||
        !settle_scene_transition(instance) ||
        !tap(instance, 70U, 42U) ||
        !settle_scene_transition(instance) ||
        !p4_game_instance_render(instance, surface)) {
        return false;
    }
    const uint64_t tracked_hash = surface_region_hash(
        surface, 92U, 101U, 229U, 121U);
    scan->mode = PREVIEW_SCAN_MOVED;
    if (!tap(instance, 70U, 180U) ||
        !p4_game_instance_render(instance, surface)) {
        return false;
    }
    const uint64_t moved_hash = surface_region_hash(
        surface, 92U, 101U, 229U, 121U);
    if (tracked_hash == moved_hash) {
        fprintf(stderr, "moved signal tracker did not change state\n");
        return false;
    }
    return true;
}

static bool render_need_coverage(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    uint64_t row_hashes[4] = {0};
    const preview_roi_t roi = {108U, 84U, 144U, 120U};
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !advance_ms(instance, 70000U) ||
        !render_sampled_phase(
            writer, instance, surface, "need-joy", 600U, 4U,
            roi, 4U, &row_hashes[BYTE_BUDDY_NEED_FX_JOY]) ||
        !tap(instance, 110U, 145U) ||
        !advance_ms(instance, PREVIEW_PLAY_ACTION_AFTER_TAP_MS) ||
        !press_button(instance, P4_BUTTON_B) ||
        !advance_ms(instance, 900U) ||
        !settle_scene_transition(instance) ||
        !render_sampled_phase(
            writer, instance, surface, "need-energy", 600U, 4U,
            roi, 4U, &row_hashes[BYTE_BUDDY_NEED_FX_ENERGY]) ||
        !tap(instance, 280U, 145U) ||
        !advance_ms(instance, 1400U) ||
        !advance_ms(instance, 20000U) ||
        !render_sampled_phase(
            writer, instance, surface, "need-hunger", 600U, 4U,
            roi, 4U, &row_hashes[BYTE_BUDDY_NEED_FX_HUNGER]) ||
        !tap(instance, 20U, 145U) ||
        !advance_ms(instance, 1400U) ||
        !advance_ms(instance, 15000U) ||
        !render_sampled_phase(
            writer, instance, surface, "need-hygiene", 600U, 4U,
            roi, 4U, &row_hashes[BYTE_BUDDY_NEED_FX_HYGIENE])) {
        return false;
    }
    return hashes_are_pairwise_distinct(row_hashes, 4U, "need rows");
}

static bool render_evolution_coverage(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    static const unsigned preparation_counts[2] = {31U, 43U};
    static const char *const labels[2] = {
        "evolution-flying", "evolution-elemental",
    };
    uint64_t row_hashes[3] = {0};
    const preview_roi_t roi = {121U, 39U, 200U, 119U};
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY)) {
        return false;
    }
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(preview_signal_token(0U, 0U), -43);
    const uint16_t signal_growth = byte_buddy_signal_growth_reward(
        profile.rarity, 1U);
    if (signal_growth == 0U || signal_growth >= 28U ||
        !care_credits(instance, 28U - signal_growth) ||
        !begin_pulse_victory(instance, surface) ||
        !advance_ms(instance, PREVIEW_SIGNAL_VICTORY_MS) ||
        !settle_scene_transition(instance) ||
        !tap(instance, 20U, 12U) ||
        !settle_scene_transition(instance) ||
        !render_sampled_phase(
            writer, instance, surface, "evolution-winged-signal",
            1000U, 4U, roi, 4U, &row_hashes[0]) ||
        !advance_ms(instance, 200U)) {
        return false;
    }
    for (size_t row = 0U; row < 2U; ++row) {
        if (!care_credits(instance, preparation_counts[row]) ||
            !trigger_care_credit(instance) ||
            !render_sampled_phase(
                writer, instance, surface, labels[row], 1000U, 4U,
                roi, 4U, &row_hashes[row + 1U]) ||
            !advance_ms(instance, 200U)) {
            return false;
        }
    }
    return hashes_are_pairwise_distinct(row_hashes, 3U, "evolution rows");
}

static bool verify_stacked_evolution_chronology(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    const preview_roi_t roi = {121U, 39U, 200U, 119U};
    uint64_t winged_hashes[4] = {0};
    uint64_t flying_hashes[4] = {0};
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !care_credits(instance, 27U) ||
        !defeat_signal_range(instance, surface, scan, 0U, 0U, 8U) ||
        !defeat_signal_range(instance, surface, scan, 1U, 0U, 8U) ||
        !defeat_signal_range(instance, surface, scan, 2U, 0U, 8U) ||
        !defeat_signal_range(instance, surface, scan, 3U, 0U, 3U) ||
        !settle_scene_transition(instance) ||
        !tap(instance, 20U, 12U) ||
        !settle_scene_transition(instance) ||
        !sample_region_hashes(
            instance, surface, roi, 1000U, winged_hashes) ||
        !advance_ms(instance, 201U) ||
        !sample_region_hashes(
            instance, surface, roi, 1000U, flying_hashes) ||
        !hashes_are_pairwise_distinct(
            winged_hashes, 4U, "stacked Winged evolution") ||
        !hashes_are_pairwise_distinct(
            flying_hashes, 4U, "stacked Flying evolution")) {
        fprintf(stderr,
                "stacked signal evolution chronology did not complete\n");
        return false;
    }
    for (size_t phase = 0U; phase < 4U; ++phase) {
        if (winged_hashes[phase] == flying_hashes[phase]) {
            fprintf(stderr,
                    "stacked evolution rows collapsed at phase %zu\n",
                    phase);
            return false;
        }
    }
    return true;
}

static bool verify_play_evolution_chronology(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    const preview_roi_t evolution_roi = {121U, 39U, 200U, 119U};
    const preview_roi_t transition_roi = {139U, 79U, 182U, 122U};
    const preview_roi_t intro_roi = {128U, 48U, 193U, 113U};
    uint64_t evolution_hashes[4] = {0};
    uint64_t transition_hash = 0U;
    uint64_t intro_hash = 0U;
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !care_credits(instance, 27U) ||
        !advance_ms(instance, PREVIEW_CARE_NEED_REFRESH_MS) ||
        !tap(instance, 110U, 145U) ||
        /* Evolution finishes first; Play's full action clip follows it. */
        !sample_region_hashes(
            instance, surface, evolution_roi, 1000U,
            evolution_hashes) ||
        !hashes_are_pairwise_distinct(
            evolution_hashes, 4U, "Play-triggered evolution") ||
        !advance_ms(instance, PREVIEW_PLAY_EVOLUTION_TAIL_MS) ||
        !capture_region_hash(
            instance, surface, transition_roi, &transition_hash) ||
        !settle_scene_transition(instance) ||
        !capture_region_hash(instance, surface, intro_roi, &intro_hash)) {
        fprintf(stderr, "Play-triggered evolution chronology failed\n");
        return false;
    }
    if (transition_hash != UINT64_C(0x5a9b36281e7b9aa0) ||
        intro_hash != UINT64_C(0xa1ffd5865272be4a)) {
        fprintf(stderr,
                "Play did not wait for evolution and restart the full "
                "Ready chronology (%016llx, %016llx)\n",
                (unsigned long long)transition_hash,
                (unsigned long long)intro_hash);
        return false;
    }
    return true;
}

static bool verify_evolution_completion_input_scrub(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    const preview_roi_t full_surface = {
        0U, 0U, P4_GAME_SURFACE_WIDTH, P4_GAME_SURFACE_HEIGHT,
    };
    uint64_t edge_hash = 0U;
    uint64_t neutral_hash = 0U;
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !care_credits(instance, 27U) ||
        !trigger_care_credit(instance) ||
        !advance_ms(instance, PREVIEW_EVOLUTION_AFTER_TAP_MS) ||
        !tap(instance, 280U, 180U) ||
        !capture_region_hash(
            instance, surface, full_surface, &edge_hash) ||
        !restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !care_credits(instance, 27U) ||
        !trigger_care_credit(instance) ||
        !advance_ms(instance, PREVIEW_EVOLUTION_AFTER_TAP_MS) ||
        /* Match tap's 16 ms down/up partition; only the edge may differ. */
        !advance_ms(instance, 16U) ||
        !advance_ms(instance, 16U) ||
        !capture_region_hash(
            instance, surface, full_surface, &neutral_hash)) {
        fprintf(stderr, "evolution completion input-scrub route failed\n");
        return false;
    }
    if (edge_hash != neutral_hash) {
        fprintf(stderr,
                "evolution completion leaked a touch edge into Home "
                "(%016llx != %016llx)\n",
                (unsigned long long)edge_hash,
                (unsigned long long)neutral_hash);
        return false;
    }
    return true;
}

static bool render_activity_coverage(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services)
{
    const preview_roi_t intro_roi = {128U, 48U, 193U, 113U};
    /*
     * The post-action deterministic seed places the first missed star at the
     * right edge.  Keep this ROI on the authored four-phase miss burst so
     * moving background streaks cannot satisfy the distinctness assertion.
     */
    const preview_roi_t miss_roi = {232U, 104U, 280U, 153U};
    const preview_roi_t summary_roi = {91U, 51U, 152U, 112U};
    const preview_roi_t summary_count_roi = {197U, 70U, 211U, 87U};
    const preview_roi_t transition_roi = {139U, 79U, 182U, 122U};
    uint64_t star_wipe_hash = 0U;
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !tap(instance, 110U, 145U) ||
        !advance_ms(instance, PREVIEW_PLAY_ACTION_AFTER_TAP_MS) ||
        !capture_region_hash(
            instance, surface, transition_roi, &star_wipe_hash) ||
        !settle_scene_transition(instance) ||
        !render_sampled_phase(
            writer, instance, surface, "activity-star-intro", 900U, 4U,
            intro_roi, 4U, NULL)) {
        return false;
    }

    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !tap(instance, 110U, 145U) ||
        !advance_ms(instance, PREVIEW_PLAY_ACTION_AFTER_TAP_MS) ||
        !settle_scene_transition(instance) ||
        !advance_ms(instance, 944U) ||
        !advance_ms(instance, 1520U) ||
        !render_sampled_phase(
            writer, instance, surface, "activity-star-miss", 450U, 4U,
            miss_roi, 4U, NULL)) {
        return false;
    }

    uint64_t summary_one_hash = 0U;
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !tap(instance, 110U, 145U) ||
        !advance_ms(instance, PREVIEW_PLAY_ACTION_AFTER_TAP_MS) ||
        !settle_scene_transition(instance) ||
        !advance_ms(instance, 944U) ||
        /* Follow the first deterministic post-action spawn for one catch. */
        !advance_touch_ms(instance, 253U, 80U, 1600U) ||
        !tap(instance, 280U, 12U) ||
        !capture_region_hash(
            instance, surface, summary_count_roi, &summary_one_hash) ||
        !render_sampled_phase(
            writer, instance, surface, "activity-star-summary", 850U, 4U,
            summary_roi, 4U, NULL) ||
        !advance_ms(instance, 35U) ||
        !settle_scene_transition(instance)) {
        return false;
    }

    uint64_t summary_zero_b_hash = 0U;
    if (!tap(instance, 110U, 145U) ||
        !advance_ms(instance, PREVIEW_PLAY_ACTION_AFTER_TAP_MS) ||
        !press_button(instance, P4_BUTTON_B) ||
        !settle_scene_transition(instance) ||
        !capture_region_hash(
            instance, surface, summary_count_roi,
            &summary_zero_b_hash)) {
        fprintf(stderr,
                "Star B could not finish during the opening transition\n");
        return false;
    }
    if (summary_one_hash == summary_zero_b_hash) {
        fprintf(stderr, "Star B did not reset per-run catches\n");
        return false;
    }
    if (!advance_ms(instance, 900U) ||
        !settle_scene_transition(instance)) {
        fprintf(stderr, "Star B summary did not return home\n");
        return false;
    }

    uint64_t summary_zero_done_hash = 0U;
    if (!tap(instance, 110U, 145U) ||
        !advance_ms(instance, PREVIEW_PLAY_ACTION_AFTER_TAP_MS) ||
        !tap(instance, 280U, 12U) ||
        !settle_scene_transition(instance) ||
        !capture_region_hash(
            instance, surface, summary_count_roi,
            &summary_zero_done_hash)) {
        fprintf(stderr,
                "Star Done could not finish during the opening transition\n");
        return false;
    }
    if (summary_zero_done_hash != summary_zero_b_hash) {
        fprintf(stderr, "Star Done did not match the zero-catch run\n");
        return false;
    }

    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !tap(instance, 180U, 180U)) {
        return false;
    }
    uint64_t shop_wipe_hash = 0U;
    if (!capture_region_hash(
            instance, surface, transition_roi, &shop_wipe_hash) ||
        star_wipe_hash == shop_wipe_hash ||
        !settle_scene_transition(instance)) {
        return false;
    }

    const preview_roi_t shop_roi = {39U, 0U, 74U, 31U};
    uint64_t power_hashes[4] = {0};
    if (!tap(instance, 50U, 60U) ||
        !sample_region_hashes(
            instance, surface, shop_roi, 481U, power_hashes) ||
        !hashes_are_pairwise_distinct(
            power_hashes, 4U, "power growth feedback") ||
        !advance_ms(instance, 160U) ||
        !tap(instance, 220U, 32U) ||
        !settle_scene_transition(instance)) {
        return false;
    }

    uint64_t shop_hashes[4] = {0};
    if (!tap(instance, 130U, 60U) ||
        !render_motion_frame_roi(
            writer, instance, surface, "activity-shop-onset", 0U,
            shop_roi, &shop_hashes[0]) ||
        !advance_ms(instance, 310U) ||
        !render_motion_frame_roi(
            writer, instance, surface, "activity-shop-unlocked", 0U,
            shop_roi, &shop_hashes[1]) ||
        !tap(instance, 50U, 60U) ||
        !render_motion_frame_roi(
            writer, instance, surface, "activity-shop-equipped", 0U,
            shop_roi, &shop_hashes[2]) ||
        !tap(instance, 130U, 60U) ||
        !render_motion_frame_roi(
            writer, instance, surface, "activity-shop-unavailable", 0U,
            shop_roi, &shop_hashes[3])) {
        return false;
    }
    static const uint64_t expected_power_hashes[4] = {
        UINT64_C(0x4f8029e505006cd8),
        UINT64_C(0xdf3a8d6e598d64aa),
        UINT64_C(0x09296ddad594a85b),
        UINT64_C(0xe7e237d7006a503f),
    };
    if (star_wipe_hash != UINT64_C(0x5a9b36281e7b9aa0) ||
        shop_wipe_hash != UINT64_C(0x050239ecae6f57f4) ||
        summary_one_hash != UINT64_C(0xe99f14f7257c8383) ||
        summary_zero_b_hash != UINT64_C(0xe39a2dbad8773bd5) ||
        summary_zero_done_hash != UINT64_C(0xe39a2dbad8773bd5)) {
        fprintf(stderr,
                "neutral wipe or per-run Star summary digest changed "
                "(%016llx, %016llx, %016llx, %016llx, %016llx)\n",
                (unsigned long long)star_wipe_hash,
                (unsigned long long)shop_wipe_hash,
                (unsigned long long)summary_one_hash,
                (unsigned long long)summary_zero_b_hash,
                (unsigned long long)summary_zero_done_hash);
        if (!writer->print_semantic_digests) {
            return false;
        }
    }
    for (size_t phase = 0U; phase < 4U; ++phase) {
        if (power_hashes[phase] != expected_power_hashes[phase]) {
            fprintf(stderr,
                    "Power growth phase %zu semantic digest changed "
                    "(%016llx)\n",
                    phase, (unsigned long long)power_hashes[phase]);
            if (!writer->print_semantic_digests) {
                return false;
            }
        }
    }
    return hashes_are_pairwise_distinct(shop_hashes, 4U, "shop feedback");
}

static bool render_authored_coverage_sequence(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services, const char *prefix,
    unsigned expected_frames, bool review)
{
    preview_motion_writer_t writer = {
        .prefix = prefix,
        .require_semantic_digests = !review,
        .print_semantic_digests = review,
    };
    const bool success =
        render_counter_coverage(
            &writer, instance, surface, scan, state, services) &&
        render_outcome_coverage(
            &writer, instance, surface, scan, state, services) &&
        render_passive_coverage(
            &writer, instance, surface, scan, state, services) &&
        verify_signal_seed_visual_distinction(
            instance, surface, scan, state, services) &&
        render_scan_coverage(
            &writer, instance, surface, scan, state, services) &&
        render_need_coverage(
            &writer, instance, surface, scan, state, services) &&
        render_evolution_coverage(
            &writer, instance, surface, scan, state, services) &&
        verify_stacked_evolution_chronology(
            instance, surface, scan, state, services) &&
        verify_play_evolution_chronology(
            instance, surface, scan, state, services) &&
        verify_evolution_completion_input_scrub(
            instance, surface, scan, state, services) &&
        render_activity_coverage(
            &writer, instance, surface, scan, state, services);
    fprintf(stdout, "authored-coverage-frames=%u\n", writer.frame_count);
    if (!success) {
        return false;
    }
    if (expected_frames != 0U && writer.frame_count != expected_frames) {
        fprintf(stderr,
                "expected %u authored coverage frames, rendered %u\n",
                expected_frames, writer.frame_count);
        return false;
    }
    return true;
}

static bool render_signal_motion_sequence(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, const char *prefix,
    unsigned expected_frames)
{
    preview_motion_writer_t writer = {.prefix = prefix};
    if (scan == NULL || prefix == NULL ||
        !care_credits(instance, 104U) ||
        !tap(instance, 100U, 180U)) {
        return false;
    }
    scan->batch = 0U;
    for (unsigned attack = 0U;
         attack < BYTE_BUDDY_SIGNAL_ATTACK_COUNT;
         ++attack) {
        if (!render_signal_attack_cycle(
                &writer, instance, surface, attack,
                attack == BYTE_BUDDY_SIGNAL_ATTACK_THORN_SNARE)) {
            return false;
        }
    }

    uint32_t defeat_elapsed_ms = 0U;
    const byte_buddy_signal_encounter_t comet =
        preview_signal_encounter(0U, 3U);
    const byte_buddy_signal_genome_t comet_genome =
        byte_buddy_signal_genome(preview_signal_token(0U, 3U));
    if (byte_buddy_signal_battle_pattern(comet_genome) !=
            BYTE_BUDDY_BATTLE_RESONANCE_WEAVE ||
        !render_until_signal_defeat(
            &writer, instance, surface, &defeat_elapsed_ms) ||
        (uint32_t)comet.attack_period_ms + defeat_elapsed_ms >=
            PREVIEW_SIGNAL_HP_DEFEAT_CUTOFF_MS ||
        !render_motion_phase(
            &writer, instance, surface, "hp-defeat",
            PREVIEW_SIGNAL_DEFEAT_MS,
            -1, NULL, true, NULL)) {
        fprintf(stderr, "defeat was not an early HP loss\n");
        return false;
    }

    uint64_t tracker_hash = 0U;
    uint64_t rematch_hash = 0U;
    if (!render_motion_frame(
            &writer, instance, surface,
            "defeat-tracker", 0U, &tracker_hash) ||
        !settle_scene_transition(instance) ||
        !press_button(instance, P4_BUTTON_A) ||
        !render_motion_frame(
            &writer, instance, surface,
            "defeat-rematch-intro", 0U, &rematch_hash) ||
        tracker_hash == rematch_hash ||
        !settle_scene_transition(instance) ||
        !press_button(instance, P4_BUTTON_B) ||
        !advance_ms(instance, PREVIEW_SIGNAL_RETREAT_REMAINING_MS) ||
        !settle_scene_transition(instance) ||
        !press_button(instance, P4_BUTTON_B) ||
        !render_pulse_victory(&writer, instance, surface) ||
        !render_resonance_victory(&writer, instance, surface)) {
        fprintf(stderr, "defeated signal was not available for rematch\n");
        return false;
    }

    for (unsigned attack = 0U;
         attack < BYTE_BUDDY_SIGNAL_ATTACK_COUNT;
         ++attack) {
        if (!writer.attack_hash_valid[attack]) {
            return false;
        }
    }
    fprintf(stdout, "signal-motion-frames=%u\n", writer.frame_count);
    if (expected_frames != 0U && writer.frame_count != expected_frames) {
        fprintf(stderr, "expected %u signal-motion frames, rendered %u\n",
                expected_frames, writer.frame_count);
        return false;
    }
    return true;
}

static bool render_animation_sequence(p4_game_instance_t *instance,
                                      p4_game_surface_t *surface,
                                      preview_signal_scan_t *scan,
                                      const char *prefix)
{
    if (scan == NULL) {
        return false;
    }
    scan->focused_rssi_dbm =
        (int8_t)PREVIEW_LEGACY_FOCUS_RSSI_DBM;
    bool success = render_clip(
        instance, surface, prefix, "egg-idle", 8U, 9U);
    for (unsigned pet = 0U; success && pet < 5U; ++pet) {
        success = care_credit(instance);
    }
    success = success && render_clip(
        instance, surface, prefix, "egg-crack", 6U, 4U) &&
        care_credit(instance) && render_clip(
        instance, surface, prefix, "egg-wobble", 6U, 4U) &&
        care_credit(instance) && render_clip(
        instance, surface, prefix, "egg-peek", 6U, 4U) &&
        care_credit(instance) && render_clip(
        instance, surface, prefix, "hatch", 18U, 4U) &&
        animate(instance, 10U) && care_credit(instance) && render_clip(
        instance, surface, prefix, "baby-care", 8U, 10U) &&
        animate(instance, 10U) && render_clip(
            instance, surface, prefix, "baby-idle", 8U, 9U) &&
        care_credits(instance, 19U) && render_clip(
            instance, surface, prefix, "winged-care", 8U, 10U) &&
        animate(instance, 10U) && render_clip(
            instance, surface, prefix, "winged-idle", 16U, 9U) &&
        care_credits(instance, 32U) && render_clip(
            instance, surface, prefix, "flying-care", 8U, 10U) &&
        animate(instance, 10U) && render_clip(
            instance, surface, prefix, "flying-idle", 16U, 9U) &&
        care_credits(instance, 44U) && render_clip(
        instance, surface, prefix, "elemental-care", 8U, 10U) &&
        animate(instance, 10U) && render_clip(
        instance, surface, prefix, "elemental-idle", 8U, 9U) &&
        tap(instance, 280U, 180U) && settle_scene_transition(instance) &&
        render_clip(
        instance, surface, prefix, "genome-dormant", 1U, 1U) &&
        settle_scene_transition(instance) &&
        tap(instance, 160U, 180U) &&
        settle_scene_transition(instance) &&
        tap(instance, 70U, 180U) && animate(instance, 2U) &&
        settle_scene_transition(instance) &&
        press_button(instance, P4_BUTTON_LEFT) &&
        screen_signature_matches(
            instance, surface, PREVIEW_SCREEN_SIGNAL_LIST) && render_clip(
        instance, surface, prefix, "signal-list-page-1", 1U, 1U) &&
        press_button(instance, P4_BUTTON_RIGHT) &&
        screen_signature_matches(
            instance, surface, PREVIEW_SCREEN_SIGNAL_LIST) && render_clip(
        instance, surface, prefix, "signal-list-page-2", 1U, 1U) &&
        tap(instance, 30U, 180U) &&
        tap(instance, 70U, 42U) && settle_scene_transition(instance) &&
        tap(instance, 70U, 180U) &&
        press_button(instance, P4_BUTTON_A) &&
        settle_signal_battle_active(instance) &&
        prime_signal_battle_sample(
            instance, preview_signal_token(0U, 0U)) &&
        screen_signature_matches(
            instance, surface, PREVIEW_SCREEN_PULSE_ACTIVE) && render_clip(
            instance, surface, prefix, "pulse-rush", 1U, 1U);
    success = success && finish_active_signal_battle_index(
        instance, surface, 0U, 0U);
    success = success && settle_scene_transition(instance) &&
        screen_signature_matches(
            instance, surface, PREVIEW_SCREEN_SIGNAL_REWARD) && render_clip(
        instance, surface, prefix, "signal-reward", 1U, 1U) &&
        settle_scene_transition(instance) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-spark", 15U, 5U) &&
        tap(instance, 70U, 180U) &&
        enter_signal_battle_index(instance, 0U, 1U) &&
        settle_signal_battle_active(instance) &&
        prime_signal_battle_sample(
            instance, preview_signal_token(0U, 1U)) &&
        screen_signature_matches(
            instance, surface, PREVIEW_SCREEN_WEAVE_ACTIVE) && render_clip(
        instance, surface, prefix, "resonance-weave", 1U, 1U) &&
        finish_active_signal_battle_index(instance, surface, 0U, 1U);
    for (unsigned index = 2U; success && index < 5U; ++index) {
        success = defeat_signal_index(instance, surface, 0U, index);
    }
    success = success && settle_scene_transition(instance) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-aurora", 16U, 5U) &&
        tap(instance, 70U, 180U) && animate(instance, 2U);
    for (unsigned index = 5U; success && index < 8U; ++index) {
        success = defeat_signal_index(instance, surface, 0U, index);
    }
    success = success && settle_scene_transition(instance) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-ascended", 4U, 5U) &&
        defeat_signal_range(instance, surface, scan, 1U, 0U, 4U) &&
        settle_scene_transition(instance) && tap(instance, 20U, 12U) &&
        render_clip(
        instance, surface, prefix, "signal-lineage-mythic", 3U, 5U) &&
        defeat_signal_range(instance, surface, scan, 1U, 4U, 8U) &&
        settle_scene_transition(instance) && tap(instance, 20U, 12U) &&
        render_clip(
        instance, surface, prefix, "signal-lineage-nova", 3U, 5U) &&
        defeat_signal_range(instance, surface, scan, 2U, 0U, 8U) &&
        settle_scene_transition(instance) && tap(instance, 20U, 12U) &&
        render_clip(
        instance, surface, prefix, "signal-lineage-galaxy", 3U, 5U) &&
        defeat_signal_range(instance, surface, scan, 3U, 0U, 8U) &&
        settle_scene_transition(instance) && tap(instance, 20U, 12U) &&
        render_clip(
        instance, surface, prefix, "signal-lineage-eternal", 3U, 5U) &&
        settle_scene_transition(instance) &&
        tap(instance, 280U, 180U) && settle_scene_transition(instance) &&
        render_clip(
        instance, surface, prefix, "genome-eternal", 1U, 1U);
    return success;
}

int main(int argc, char **argv)
{
    const bool signal_motion = argc == 5 &&
        strcmp(argv[2], "--signal-motion") == 0;
    const bool authored_motion = argc == 5 &&
        strcmp(argv[2], "--authored-motion") == 0;
    const bool authored_review = argc == 5 &&
        strcmp(argv[2], "--review-authored-motion") == 0;
    const bool exact_animation = argc == 5 &&
        strcmp(argv[2], "--animation") == 0;
    const bool completion_motion = argc == 5 &&
        strcmp(argv[2], "--completion-motion") == 0;
    const bool runtime_completion = argc == 5 &&
        strcmp(argv[2], "--runtime-completion-motion") == 0;
    const bool runtime_completion_review = argc == 5 &&
        strcmp(argv[2], "--review-runtime-completion-motion") == 0;
    unsigned expected_motion_frames = 0U;
    char trailing = '\0';
    if ((argc != 13 && argc != 3 && !signal_motion && !authored_motion &&
         !authored_review && !exact_animation && !completion_motion &&
         !runtime_completion && !runtime_completion_review) ||
        ((signal_motion || authored_motion || authored_review ||
          exact_animation || completion_motion || runtime_completion ||
          runtime_completion_review) &&
         sscanf(argv[4], "%u%c", &expected_motion_frames, &trailing) != 1)) {
        fprintf(stderr, "usage: %s ART.bin EGG.ppm HATCH.ppm POWER.ppm "
                        "STYLE.ppm CUSTOM.ppm FLYING.ppm ELEMENTAL.ppm "
                        "PLAY.ppm SIGNALS.ppm TRACK.ppm BATTLE.ppm\n"
                        "   or: %s ART.bin ANIMATION_PREFIX\n"
                        "   or: %s ART.bin --animation PREFIX "
                        "EXPECTED_FRAMES\n"
                        "   or: %s ART.bin --signal-motion PREFIX "
                        "EXPECTED_FRAMES\n"
                        "   or: %s ART.bin --authored-motion PREFIX "
                        "EXPECTED_FRAMES\n"
                        "   or: %s ART.bin --review-authored-motion PREFIX "
                        "EXPECTED_FRAMES\n"
                        "   or: %s ART.bin --completion-motion PREFIX "
                        "EXPECTED_FRAMES\n"
                        "   or: %s ART.bin --runtime-completion-motion "
                        "PREFIX EXPECTED_FRAMES\n"
                        "   or: %s ART.bin "
                        "--review-runtime-completion-motion PREFIX "
                        "EXPECTED_FRAMES\n"
                        "       use PREFIX - to render/hash without files\n",
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0],
                argv[0]);
        return EXIT_FAILURE;
    }
    size_t art_bytes = 0U;
    uint8_t *const art = read_resource(argv[1], &art_bytes);
    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    if (pixels == NULL || state == NULL || art == NULL) {
        free(art);
        free(state);
        free(pixels);
        return EXIT_FAILURE;
    }
    preview_signal_scan_t signal_scan = {0};
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_STORAGE |
                                  P4_GAME_CAP_SIGNAL_SCAN,
        .resource_data = art,
        .resource_bytes = art_bytes,
        .resource_format_version = 1U,
        .signal_scan_context = &signal_scan,
        .request_signal_scan = preview_request_signal,
        .read_signal_scan = preview_read_signal,
    };
    p4_game_instance_t instance = {0};
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    if (completion_motion) {
        const bool success = render_completion_motion_sequence(
            art, art_bytes, pixels, argv[3], expected_motion_frames);
        free(art);
        free(state);
        free(pixels);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    bool success = p4_game_instance_start(
        &instance, &p4_byte_buddy_game, &services, state,
        p4_byte_buddy_game.state_bytes);
    if (success && (runtime_completion || runtime_completion_review)) {
        success = render_runtime_completion_sequence(
            &instance, &surface, &signal_scan, state, &services,
            argv[3], expected_motion_frames, runtime_completion_review);
        p4_game_instance_stop(&instance);
        free(art);
        free(state);
        free(pixels);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (success && signal_motion) {
        success = render_signal_motion_sequence(
            &instance, &surface, &signal_scan, argv[3],
            expected_motion_frames);
        p4_game_instance_stop(&instance);
        free(art);
        free(state);
        free(pixels);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (success && (authored_motion || authored_review)) {
        success = render_authored_coverage_sequence(
            &instance, &surface, &signal_scan, state, &services,
            argv[3], expected_motion_frames, authored_review);
        p4_game_instance_stop(&instance);
        free(art);
        free(state);
        free(pixels);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (success && exact_animation) {
        s_clip_frame_count = 0U;
        success = render_animation_sequence(
            &instance, &surface, &signal_scan, argv[3]);
        fprintf(stdout, "animation-frames=%u\n", s_clip_frame_count);
        if (success && s_clip_frame_count != expected_motion_frames) {
            fprintf(stderr, "expected %u animation frames, rendered %u\n",
                    expected_motion_frames, s_clip_frame_count);
            success = false;
        }
        p4_game_instance_stop(&instance);
        free(art);
        free(state);
        free(pixels);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (success && argc == 3) {
        s_clip_frame_count = 0U;
        success = render_animation_sequence(
            &instance, &surface, &signal_scan, argv[2]);
        p4_game_instance_stop(&instance);
        free(art);
        free(state);
        free(pixels);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    success = success && animate(&instance, 18U) &&
        render_to(&instance, &surface, argv[2]);
    for (unsigned pet = 0U; success && pet < 7U; ++pet) {
        success = care_credit(&instance);
    }
    success = success && animate(&instance, 8U) &&
        render_to(&instance, &surface, argv[3]) &&
        tap(&instance, 180U, 180U) &&
        render_to(&instance, &surface, argv[4]) &&
        tap(&instance, 220U, 32U) &&
        tap(&instance, 130U, 60U) && tap(&instance, 300U, 60U) &&
        tap(&instance, 80U, 180U) &&
        render_to(&instance, &surface, argv[5]) &&
        tap(&instance, 240U, 180U) && care_credits(&instance, 1U) &&
        animate(&instance, 18U) && render_to(&instance, &surface, argv[6]);
    success = success && care_credits(&instance, 52U) &&
        animate(&instance, 18U) &&
        render_to(&instance, &surface, argv[7]) &&
        care_credits(&instance, 44U) && tap(&instance, 20U, 145U) &&
        animate(&instance, 18U) &&
        render_to(&instance, &surface, argv[8]) &&
        tap(&instance, 110U, 145U) && tap(&instance, 280U, 80U) &&
        animate(&instance, 8U) &&
        render_to(&instance, &surface, argv[9]) &&
        advance_ms(&instance, PREVIEW_PLAY_ACTION_AFTER_TAP_MS) &&
        press_button(&instance, P4_BUTTON_B) &&
        advance_ms(&instance, 900U) && tap(&instance, 70U, 180U) &&
        settle_scene_transition(&instance) &&
        screen_signature_matches(
            &instance, &surface, PREVIEW_SCREEN_SIGNAL_LIST) &&
        render_to(&instance, &surface, argv[10]) &&
        tap(&instance, 70U, 67U) &&
        settle_scene_transition(&instance) &&
        tap(&instance, 70U, 180U) && animate(&instance, 2U) &&
        screen_signature_matches(
            &instance, &surface, PREVIEW_SCREEN_SIGNAL_TRACKER) &&
        render_to(&instance, &surface, argv[11]) &&
        tap(&instance, 250U, 180U) &&
        settle_signal_battle_active(&instance) &&
        prime_signal_battle_sample(
            &instance, preview_signal_token(0U, 1U)) &&
        screen_signature_matches(
            &instance, &surface, PREVIEW_SCREEN_WEAVE_ACTIVE) &&
        render_to(&instance, &surface, argv[12]);
    p4_game_instance_stop(&instance);
    free(art);
    free(state);
    free(pixels);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
