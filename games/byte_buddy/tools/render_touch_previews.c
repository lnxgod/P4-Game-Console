// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "p4/game.h"

#include "byte_buddy_internal.h"

extern const p4_game_descriptor_t p4_byte_buddy_game;

typedef struct {
    p4_game_signal_snapshot_t snapshot;
    uint32_t requests;
    uint8_t batch;
} preview_signal_scan_t;

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
            scan->snapshot.results[0].rssi_dbm = -43;
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

static bool complete_resonance_weave(
    p4_game_instance_t *instance, uint64_t token)
{
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(token, -43);
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
        const unsigned frames =
            (unsigned)(rules.hold_ms + 49U) / 50U + 1U;
        for (unsigned frame = 0U; frame < frames; ++frame) {
            if (!update_elapsed(instance, &held, 50U)) {
                return false;
            }
        }
        const p4_game_input_t released = {.touch_valid = true};
        for (unsigned settle = 0U; settle < 4U; ++settle) {
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
    p4_game_instance_t *instance, unsigned batch, unsigned index)
{
    const uint64_t token = preview_signal_token(batch, index);
    if (token == 0U) {
        return false;
    }
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    if (byte_buddy_signal_battle_pattern(genome) ==
        BYTE_BUDDY_BATTLE_RESONANCE_WEAVE) {
        return complete_resonance_weave(instance, token);
    }
    for (unsigned strike = 0U; strike < 32U; ++strike) {
        if (!tap(instance, 60U, 180U)) {
            return false;
        }
    }
    return true;
}

static bool defeat_signal_index(
    p4_game_instance_t *instance, unsigned batch, unsigned index)
{
    return enter_signal_battle_index(instance, batch, index) &&
        finish_signal_battle_index(instance, batch, index);
}

static bool defeat_signal_range(
    p4_game_instance_t *instance, preview_signal_scan_t *scan,
    unsigned batch, unsigned first, unsigned limit)
{
    if (scan == NULL || batch >= 4U || first > limit || limit > 8U) {
        return false;
    }
    scan->batch = (uint8_t)batch;
    for (unsigned index = first; index < limit; ++index) {
        if (!defeat_signal_index(instance, batch, index)) {
            return false;
        }
    }
    return true;
}

static bool render_animation_sequence(p4_game_instance_t *instance,
                                      p4_game_surface_t *surface,
                                      preview_signal_scan_t *scan,
                                      const char *prefix)
{
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
    for (unsigned strike = 0U; success && strike < 32U; ++strike) {
        success = tap(instance, 60U, 180U);
    }
    success = success && render_clip(
        instance, surface, prefix, "signal-reward", 1U, 1U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-spark", 15U, 5U) &&
        tap(instance, 70U, 180U) &&
        enter_signal_battle_index(instance, 0U, 1U) && render_clip(
        instance, surface, prefix, "resonance-weave", 1U, 1U) &&
        finish_signal_battle_index(instance, 0U, 1U);
    for (unsigned index = 2U; success && index < 5U; ++index) {
        success = defeat_signal_index(instance, 0U, index);
    }
    success = success && tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-aurora", 16U, 5U) &&
        tap(instance, 70U, 180U) && animate(instance, 2U);
    for (unsigned index = 5U; success && index < 8U; ++index) {
        success = defeat_signal_index(instance, 0U, index);
    }
    success = success && tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-ascended", 4U, 5U) &&
        defeat_signal_range(instance, scan, 1U, 0U, 4U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-mythic", 3U, 5U) &&
        defeat_signal_range(instance, scan, 1U, 4U, 8U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-nova", 3U, 5U) &&
        defeat_signal_range(instance, scan, 2U, 0U, 8U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-galaxy", 3U, 5U) &&
        defeat_signal_range(instance, scan, 3U, 0U, 8U) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-eternal", 3U, 5U) &&
        tap(instance, 280U, 180U) && render_clip(
        instance, surface, prefix, "genome-eternal", 1U, 1U);
    return success;
}

int main(int argc, char **argv)
{
    if (argc != 13 && argc != 3) {
        fprintf(stderr, "usage: %s ART.bin EGG.ppm HATCH.ppm POWER.ppm "
                        "STYLE.ppm CUSTOM.ppm FLYING.ppm ELEMENTAL.ppm "
                        "PLAY.ppm SIGNALS.ppm TRACK.ppm BATTLE.ppm\n"
                        "   or: %s ART.bin ANIMATION_PREFIX\n",
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
