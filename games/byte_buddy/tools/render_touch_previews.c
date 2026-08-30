// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/game.h"

#include "byte_buddy_internal.h"

extern const p4_game_descriptor_t p4_byte_buddy_game;

typedef struct {
    p4_game_signal_snapshot_t snapshot;
    uint32_t requests;
    uint8_t batch;
    int8_t focused_rssi_dbm;
} preview_signal_scan_t;

typedef struct {
    const char *prefix;
    unsigned frame_count;
    uint64_t attack_hashes[BYTE_BUDDY_SIGNAL_ATTACK_COUNT];
    bool attack_hash_valid[BYTE_BUDDY_SIGNAL_ATTACK_COUNT];
} preview_motion_writer_t;

enum {
    PREVIEW_SIGNAL_INTRO_MS = 450,
    PREVIEW_SIGNAL_TRAVEL_MS = 220,
    PREVIEW_SIGNAL_IMPACT_MS = 280,
    PREVIEW_SIGNAL_RECOVERY_MS = 420,
    PREVIEW_SIGNAL_VICTORY_MS = 900,
    PREVIEW_SIGNAL_DEFEAT_MS = 760,
    PREVIEW_SIGNAL_RETREAT_REMAINING_MS = 504,
    PREVIEW_SIGNAL_STRIKE_WAIT_MS = 400,
    PREVIEW_SIGNAL_HP_DEFEAT_CUTOFF_MS = 11000,
    PREVIEW_SIGNAL_MOTION_MAX_FRAMES = 1200,
    PREVIEW_LEGACY_FOCUS_RSSI_DBM = -65,
    PREVIEW_BUTTON_RELEASE_MS = 16,
};

static uint64_t preview_signal_token(unsigned batch, unsigned index);

static bool preview_request_signal(void *context, uint64_t focus_token)
{
    preview_signal_scan_t *const scan = context;
    if (scan == NULL) {
        return false;
    }
    ++scan->requests;
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

static bool care_credit(p4_game_instance_t *instance)
{
    if (!tap(instance, 160U, 80U)) {
        return false;
    }
    const p4_game_input_t released = {.touch_valid = true};
    for (unsigned wait = 0U; wait < 7U; ++wait) {
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

static bool render_motion_frame(
    preview_motion_writer_t *writer,
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    const char *label, unsigned local_frame, uint64_t *region_hash)
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
        surface, 44U, 38U, 294U, 122U);
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
    if (token == 0U || !tap(instance, 100U, 180U) ||
        !tap(instance, 30U, 180U) ||
        (index >= 5U && !tap(instance, 280U, 180U)) ||
        !tap(instance, 70U,
             (uint16_t)(42U + (index % 5U) * 25U)) ||
        !tap(instance, 70U, 180U) ||
        !press_button(instance, P4_BUTTON_A)) {
        return false;
    }
    return true;
}

static bool finish_signal_battle_index(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    unsigned batch, unsigned index)
{
    const uint64_t token = preview_signal_token(batch, index);
    if (token == 0U || surface == NULL ||
        !advance_ms(
            instance,
            PREVIEW_SIGNAL_INTRO_MS - PREVIEW_BUTTON_RELEASE_MS)) {
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
    return press_button(instance, P4_BUTTON_B);
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
        !press_button(instance, P4_BUTTON_A) ||
        !render_motion_frame(
            &writer, instance, surface,
            "defeat-rematch-intro", 0U, &rematch_hash) ||
        tracker_hash == rematch_hash ||
        !press_button(instance, P4_BUTTON_B) ||
        !advance_ms(instance, PREVIEW_SIGNAL_RETREAT_REMAINING_MS) ||
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
        tap(instance, 280U, 180U) && render_clip(
        instance, surface, prefix, "genome-dormant", 1U, 1U) &&
        tap(instance, 160U, 180U) &&
        tap(instance, 70U, 180U) && animate(instance, 2U) &&
        press_button(instance, P4_BUTTON_LEFT) && render_clip(
        instance, surface, prefix, "signal-list-page-1", 1U, 1U) &&
        press_button(instance, P4_BUTTON_RIGHT) && render_clip(
        instance, surface, prefix, "signal-list-page-2", 1U, 1U) &&
        tap(instance, 30U, 180U) &&
        tap(instance, 70U, 42U) && tap(instance, 70U, 180U) &&
        press_button(instance, P4_BUTTON_A) && render_clip(
            instance, surface, prefix, "pulse-rush", 1U, 1U);
    success = success && finish_signal_battle_index(
        instance, surface, 0U, 0U);
    success = success && render_clip(
        instance, surface, prefix, "signal-reward", 1U, 1U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-spark", 15U, 5U) &&
        tap(instance, 70U, 180U) &&
        enter_signal_battle_index(instance, 0U, 1U) && render_clip(
        instance, surface, prefix, "resonance-weave", 1U, 1U) &&
        finish_signal_battle_index(instance, surface, 0U, 1U);
    for (unsigned index = 2U; success && index < 5U; ++index) {
        success = defeat_signal_index(instance, surface, 0U, index);
    }
    success = success && tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-aurora", 16U, 5U) &&
        tap(instance, 70U, 180U) && animate(instance, 2U);
    for (unsigned index = 5U; success && index < 8U; ++index) {
        success = defeat_signal_index(instance, surface, 0U, index);
    }
    success = success && tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-ascended", 4U, 5U) &&
        defeat_signal_range(instance, surface, scan, 1U, 0U, 4U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-mythic", 3U, 5U) &&
        defeat_signal_range(instance, surface, scan, 1U, 4U, 8U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-nova", 3U, 5U) &&
        defeat_signal_range(instance, surface, scan, 2U, 0U, 8U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-galaxy", 3U, 5U) &&
        defeat_signal_range(instance, surface, scan, 3U, 0U, 8U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-eternal", 3U, 5U) &&
        tap(instance, 280U, 180U) && render_clip(
        instance, surface, prefix, "genome-eternal", 1U, 1U);
    return success;
}

int main(int argc, char **argv)
{
    const bool signal_motion = argc == 5 &&
        strcmp(argv[2], "--signal-motion") == 0;
    unsigned expected_motion_frames = 0U;
    char trailing = '\0';
    if ((argc != 13 && argc != 3 && !signal_motion) ||
        (signal_motion &&
         sscanf(argv[4], "%u%c", &expected_motion_frames, &trailing) != 1)) {
        fprintf(stderr, "usage: %s ART.bin EGG.ppm HATCH.ppm POWER.ppm "
                        "STYLE.ppm CUSTOM.ppm FLYING.ppm ELEMENTAL.ppm "
                        "PLAY.ppm SIGNALS.ppm TRACK.ppm BATTLE.ppm\n"
                        "   or: %s ART.bin ANIMATION_PREFIX\n"
                        "   or: %s ART.bin --signal-motion PREFIX "
                        "EXPECTED_FRAMES\n"
                        "       use PREFIX - to render/hash without files\n",
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
    bool success = p4_game_instance_start(
        &instance, &p4_byte_buddy_game, &services, state,
        p4_byte_buddy_game.state_bytes);
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
    if (success && argc == 3) {
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
        tap(&instance, 280U, 12U) && tap(&instance, 70U, 180U) &&
        animate(&instance, 2U) &&
        render_to(&instance, &surface, argv[10]) &&
        tap(&instance, 70U, 67U) && tap(&instance, 70U, 180U) &&
        animate(&instance, 2U) &&
        render_to(&instance, &surface, argv[11]) &&
        tap(&instance, 250U, 180U) && animate(&instance, 2U) &&
        render_to(&instance, &surface, argv[12]);
    p4_game_instance_stop(&instance);
    free(art);
    free(state);
    free(pixels);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
