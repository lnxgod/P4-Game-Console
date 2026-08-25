// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "p4/game.h"

extern const p4_game_descriptor_t p4_byte_buddy_game;

typedef struct {
    p4_game_signal_snapshot_t snapshot;
    uint32_t requests;
} preview_signal_scan_t;

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
        .count = 5U,
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
        },
    };
    for (size_t index = 0U; index < scan->snapshot.count; ++index) {
        scan->snapshot.results[index].flags |= P4_GAME_SIGNAL_SIMULATED;
        if (focus_token != 0U &&
            scan->snapshot.results[index].token == focus_token) {
            scan->snapshot.results[index].rssi_dbm = -43;
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

static bool defeat_signal_row(p4_game_instance_t *instance, unsigned row)
{
    if (row >= 5U || !tap(
            instance, 70U, (uint16_t)(42U + row * 25U)) ||
        !tap(instance, 70U, 180U) ||
        !tap(instance, 250U, 180U)) {
        return false;
    }
    for (unsigned strike = 0U; strike < 16U; ++strike) {
        if (!tap(instance, 60U, 180U)) {
            return false;
        }
    }
    return true;
}

static bool render_animation_sequence(p4_game_instance_t *instance,
                                      p4_game_surface_t *surface,
                                      const char *prefix)
{
    bool success = render_clip(
        instance, surface, prefix, "egg-idle", 8U, 9U);
    for (unsigned pet = 0U; success && pet < 5U; ++pet) {
        success = tap(instance, 160U, 80U);
    }
    success = success && render_clip(
        instance, surface, prefix, "egg-crack", 6U, 4U) &&
        tap(instance, 160U, 80U) && render_clip(
        instance, surface, prefix, "egg-wobble", 6U, 4U) &&
        tap(instance, 160U, 80U) && render_clip(
        instance, surface, prefix, "egg-peek", 6U, 4U) &&
        tap(instance, 160U, 80U) && render_clip(
        instance, surface, prefix, "hatch", 18U, 4U) &&
        animate(instance, 10U) && tap(instance, 160U, 80U) && render_clip(
        instance, surface, prefix, "baby-care", 8U, 10U) &&
        animate(instance, 10U) && render_clip(
            instance, surface, prefix, "baby-idle", 8U, 9U) &&
        tap(instance, 260U, 180U) && render_clip(
            instance, surface, prefix, "winged-care", 8U, 10U) &&
        animate(instance, 10U) && render_clip(
            instance, surface, prefix, "winged-idle", 16U, 9U) &&
        tap(instance, 260U, 180U) && render_clip(
            instance, surface, prefix, "flying-care", 8U, 10U) &&
        animate(instance, 10U) && render_clip(
            instance, surface, prefix, "flying-idle", 16U, 9U) &&
        tap(instance, 260U, 180U) && render_clip(
        instance, surface, prefix, "elemental-care", 8U, 10U) &&
        animate(instance, 10U) && render_clip(
        instance, surface, prefix, "elemental-idle", 8U, 9U) &&
        tap(instance, 70U, 180U) && animate(instance, 2U) &&
        tap(instance, 70U, 42U) && tap(instance, 70U, 180U) &&
        tap(instance, 250U, 180U);
    for (unsigned strike = 0U; success && strike < 32U; ++strike) {
        success = tap(instance, 60U, 180U);
    }
    success = success && tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-spark", 16U, 5U) &&
        tap(instance, 70U, 180U);
    for (unsigned row = 1U; success && row < 5U; ++row) {
        success = defeat_signal_row(instance, row);
    }
    success = success && tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-aurora", 16U, 5U);
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
        success = render_animation_sequence(&instance, &surface, argv[2]);
        p4_game_instance_stop(&instance);
        free(art);
        free(state);
        free(pixels);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    success = success && animate(&instance, 18U) &&
        render_to(&instance, &surface, argv[2]);
    for (unsigned pet = 0U; success && pet < 7U; ++pet) {
        success = tap(&instance, 160U, 80U);
    }
    success = success && animate(&instance, 8U) &&
        render_to(&instance, &surface, argv[3]) &&
        tap(&instance, 180U, 180U) &&
        render_to(&instance, &surface, argv[4]) &&
        tap(&instance, 220U, 32U) &&
        tap(&instance, 130U, 60U) && tap(&instance, 300U, 60U) &&
        tap(&instance, 80U, 180U) &&
        render_to(&instance, &surface, argv[5]) &&
        tap(&instance, 240U, 180U) && tap(&instance, 260U, 180U) &&
        animate(&instance, 18U) && render_to(&instance, &surface, argv[6]);
    for (unsigned stage = 0U; success && stage < 2U; ++stage) {
        success = tap(&instance, 260U, 180U);
    }
    success = success && animate(&instance, 18U) &&
        render_to(&instance, &surface, argv[7]) &&
        tap(&instance, 260U, 180U) && tap(&instance, 20U, 145U) &&
        animate(&instance, 18U) &&
        render_to(&instance, &surface, argv[8]) &&
        tap(&instance, 110U, 145U) && tap(&instance, 280U, 80U) &&
        animate(&instance, 8U) &&
        render_to(&instance, &surface, argv[9]) &&
        tap(&instance, 280U, 12U) && tap(&instance, 70U, 180U) &&
        animate(&instance, 2U) &&
        render_to(&instance, &surface, argv[10]) &&
        tap(&instance, 70U, 42U) && tap(&instance, 70U, 180U) &&
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
