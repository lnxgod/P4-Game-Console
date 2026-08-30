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
    uint8_t mode;
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
} preview_motion_writer_t;

typedef struct {
    uint16_t left;
    uint16_t top;
    uint16_t right;
    uint16_t bottom;
} preview_roi_t;

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
    PREVIEW_SCENE_TRANSITION_MS = 240,
};

static unsigned s_clip_frame_count;

static uint64_t preview_signal_token(unsigned batch, unsigned index);

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

static bool care_credit(p4_game_instance_t *instance)
{
    if (!tap(instance, 160U, 80U)) {
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
        {"counter-nova", 0U, UINT64_C(0x312cdc7584a07a30)},
        {"counter-nova", 1U, UINT64_C(0x6a805625fd0a137e)},
        {"counter-nova", 2U, UINT64_C(0x829b210914c9334d)},
        {"counter-nova", 3U, UINT64_C(0x65a8d3c22df47444)},
        {"counter-flare", 0U, UINT64_C(0x54fb8ff9ccc3fa80)},
        {"counter-flare", 1U, UINT64_C(0x25afa1c67c508646)},
        {"counter-flare", 2U, UINT64_C(0x932483a41694ed44)},
        {"counter-flare", 3U, UINT64_C(0x984cf3a3852c583e)},
        {"counter-glacier", 0U, UINT64_C(0x387b4b74204918c9)},
        {"counter-glacier", 1U, UINT64_C(0x3e82b2ed4c7191b7)},
        {"counter-glacier", 2U, UINT64_C(0xf73a912f5408ecb7)},
        {"counter-glacier", 3U, UINT64_C(0xfa6f719e22edc981)},
        {"counter-jam", 0U, UINT64_C(0x3186199ea91150ae)},
        {"counter-jam", 1U, UINT64_C(0xf603064603500fb2)},
        {"counter-jam", 2U, UINT64_C(0xb19dc3e54b791804)},
        {"counter-jam", 3U, UINT64_C(0x4566a4d65fcccc9b)},
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
        {"evolution-genome", 0U, UINT64_C(0x2bf42edf89cc90e6)},
        {"evolution-genome", 1U, UINT64_C(0x1226d12be371d112)},
        {"evolution-genome", 2U, UINT64_C(0x62c2aaf8f143d792)},
        {"evolution-genome", 3U, UINT64_C(0xcbbe6ec1e0d2be8c)},
        {"passive-ward", 0U, UINT64_C(0x0fbe85311f4cfb8f)},
        {"passive-ward", 1U, UINT64_C(0x9041ab5d35df5857)},
        {"passive-ward", 2U, UINT64_C(0xbd461bcc2c295ff4)},
        {"passive-ward", 3U, UINT64_C(0xd056a8c81f18ffee)},
        {"passive-echo", 0U, UINT64_C(0x977f1b47db472c27)},
        {"passive-echo", 1U, UINT64_C(0x5dbcb7a51aeeabe5)},
        {"passive-echo", 2U, UINT64_C(0xe08180530092d5b2)},
        {"passive-echo", 3U, UINT64_C(0x4485951c878be4c6)},
        {"passive-siphon", 0U, UINT64_C(0xa024295847cf3888)},
        {"passive-siphon", 1U, UINT64_C(0x7f05a94d69e6a219)},
        {"passive-siphon", 2U, UINT64_C(0x505a043a4051dd14)},
        {"passive-siphon", 3U, UINT64_C(0x0068b6b08307d1d2)},
        {"passive-overclock", 0U, UINT64_C(0xfe7644eeaa23f93d)},
        {"passive-overclock", 1U, UINT64_C(0x58f5d01999a01997)},
        {"passive-overclock", 2U, UINT64_C(0x92d2acf11bded96b)},
        {"passive-overclock", 3U, UINT64_C(0x3999cd2100266f84)},
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
        {"need-joy", 0U, UINT64_C(0xa0e05110828804e1)},
        {"need-joy", 1U, UINT64_C(0x8752764196a71f28)},
        {"need-joy", 2U, UINT64_C(0xa2295ff11b2093fa)},
        {"need-joy", 3U, UINT64_C(0x3938ffa5ff640d79)},
        {"need-energy", 0U, UINT64_C(0xf17fc145842d6b81)},
        {"need-energy", 1U, UINT64_C(0x233b426569f864ad)},
        {"need-energy", 2U, UINT64_C(0x37934af6a153b3f1)},
        {"need-energy", 3U, UINT64_C(0x2f71117e05ef0141)},
        {"need-hunger", 0U, UINT64_C(0xcbef06682b9020b2)},
        {"need-hunger", 1U, UINT64_C(0xafefb81645e98159)},
        {"need-hunger", 2U, UINT64_C(0x85c126ff1c8fbc96)},
        {"need-hunger", 3U, UINT64_C(0xb049e507b97a48c3)},
        {"need-hygiene", 0U, UINT64_C(0xcc7013062d8314b2)},
        {"need-hygiene", 1U, UINT64_C(0x0615ff26a1a69e4b)},
        {"need-hygiene", 2U, UINT64_C(0x644e46c235bc7d45)},
        {"need-hygiene", 3U, UINT64_C(0x019ee1960926f647)},
        {"evolution-winged-signal", 0U,
         UINT64_C(0x0e4319ec249f6407)},
        {"evolution-winged-signal", 1U,
         UINT64_C(0x19793ac107c224f4)},
        {"evolution-winged-signal", 2U,
         UINT64_C(0x6c01b3fa62585ba4)},
        {"evolution-winged-signal", 3U,
         UINT64_C(0xccaa697afad8da96)},
        {"evolution-flying", 0U, UINT64_C(0xfbee4998b66a4310)},
        {"evolution-flying", 1U, UINT64_C(0x0740c665353d8d4c)},
        {"evolution-flying", 2U, UINT64_C(0x25920d6b4ab43634)},
        {"evolution-flying", 3U, UINT64_C(0x5a9b2381d668ea52)},
        {"evolution-elemental", 0U, UINT64_C(0xc395a22fc0e0f2ef)},
        {"evolution-elemental", 1U, UINT64_C(0x7895eb2220c00ebc)},
        {"evolution-elemental", 2U, UINT64_C(0x6d0258a58bdddd7e)},
        {"evolution-elemental", 3U, UINT64_C(0xc5985dabd78f4e61)},
        {"activity-star-intro", 0U, UINT64_C(0xa1ffd5865272be4a)},
        {"activity-star-intro", 1U, UINT64_C(0x26d9c3f6c255bf3c)},
        {"activity-star-intro", 2U, UINT64_C(0x2088678244447cf7)},
        {"activity-star-intro", 3U, UINT64_C(0x2220aee4c222b5da)},
        {"activity-star-miss", 0U, UINT64_C(0x7c9d69334a4153c7)},
        {"activity-star-miss", 1U, UINT64_C(0x807d702f58d99a3a)},
        {"activity-star-miss", 2U, UINT64_C(0xc47ab7b75832deab)},
        {"activity-star-miss", 3U, UINT64_C(0x1a5f3a4136a4bb30)},
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
    *scan = (preview_signal_scan_t){.mode = (uint8_t)scan_mode};
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(
        instance, &p4_byte_buddy_game, services, state,
        p4_byte_buddy_game.state_bytes);
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
                !advance_ms(instance, 720U) ||
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
        unsigned signal_index = 0U;
        while (signal_index < 4U &&
               preview_signal_encounter(0U, signal_index).passive != passive) {
            ++signal_index;
        }
        if (signal_index == 4U ||
            !restart_preview_game(
                instance, state, services, scan, PREVIEW_SCAN_READY) ||
            !enter_signal_battle_index(instance, 0U, signal_index) ||
            !settle_scene_transition(instance) ||
            !render_sampled_phase(
                writer, instance, surface, labels[passive],
                PREVIEW_SIGNAL_INTRO_MS + 1U, 4U, roi, 4U,
                &row_hashes[passive])) {
            return false;
        }
    }
    return hashes_are_pairwise_distinct(
        row_hashes, BYTE_BUDDY_SIGNAL_PASSIVE_COUNT, "passive rows");
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
        !advance_ms(instance, 720U) ||
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
            !tap(instance, 160U, 80U) ||
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
        !tap(instance, 110U, 145U) ||
        !sample_region_hashes(
            instance, surface, evolution_roi, 1000U,
            evolution_hashes) ||
        !hashes_are_pairwise_distinct(
            evolution_hashes, 4U, "Play-triggered evolution") ||
        !advance_ms(instance, 200U) ||
        !capture_region_hash(
            instance, surface, transition_roi, &transition_hash) ||
        !settle_scene_transition(instance) ||
        !capture_region_hash(instance, surface, intro_roi, &intro_hash)) {
        fprintf(stderr, "Play-triggered evolution chronology failed\n");
        return false;
    }
    if (transition_hash != UINT64_C(0x9480518cf1decbe4) ||
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
        !tap(instance, 160U, 80U) ||
        !advance_ms(instance, 1168U) ||
        !tap(instance, 280U, 180U) ||
        !capture_region_hash(
            instance, surface, full_surface, &edge_hash) ||
        !restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !care_credits(instance, 27U) ||
        !tap(instance, 160U, 80U) ||
        !advance_ms(instance, 1200U) ||
        !capture_region_hash(
            instance, surface, full_surface, &neutral_hash)) {
        fprintf(stderr, "evolution completion input-scrub route failed\n");
        return false;
    }
    if (edge_hash != neutral_hash) {
        fprintf(stderr,
                "evolution completion leaked a touch edge into Home\n");
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
    const preview_roi_t miss_roi = {188U, 109U, 228U, 150U};
    const preview_roi_t summary_roi = {91U, 51U, 152U, 112U};
    const preview_roi_t summary_count_roi = {197U, 70U, 211U, 87U};
    const preview_roi_t transition_roi = {139U, 79U, 182U, 122U};
    uint64_t star_wipe_hash = 0U;
    if (!restart_preview_game(
            instance, state, services, scan, PREVIEW_SCAN_READY) ||
        !tap(instance, 110U, 145U) ||
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
        !settle_scene_transition(instance) ||
        !advance_ms(instance, 944U) ||
        !advance_touch_ms(instance, 207U, 80U, 1600U) ||
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
    if (star_wipe_hash != UINT64_C(0x47d3a1410a6ba068) ||
        shop_wipe_hash != UINT64_C(0xcd8907d5ac710875) ||
        summary_one_hash != UINT64_C(0xe99f14f7257c8383) ||
        summary_zero_b_hash != UINT64_C(0xe39a2dbad8773bd5) ||
        summary_zero_done_hash != UINT64_C(0xe39a2dbad8773bd5)) {
        fprintf(stderr,
                "neutral wipe or per-run Star summary digest changed\n");
        return false;
    }
    for (size_t phase = 0U; phase < 4U; ++phase) {
        if (power_hashes[phase] != expected_power_hashes[phase]) {
            fprintf(stderr,
                    "Power growth phase %zu semantic digest changed\n",
                    phase);
            return false;
        }
    }
    return hashes_are_pairwise_distinct(shop_hashes, 4U, "shop feedback");
}

static bool render_authored_coverage_sequence(
    p4_game_instance_t *instance, p4_game_surface_t *surface,
    preview_signal_scan_t *scan, void *state,
    const p4_game_services_t *services, const char *prefix,
    unsigned expected_frames)
{
    preview_motion_writer_t writer = {
        .prefix = prefix,
        .require_semantic_digests = true,
    };
    const bool success =
        render_counter_coverage(
            &writer, instance, surface, scan, state, services) &&
        render_outcome_coverage(
            &writer, instance, surface, scan, state, services) &&
        render_passive_coverage(
            &writer, instance, surface, scan, state, services) &&
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
        tap(instance, 280U, 180U) && render_clip(
        instance, surface, prefix, "genome-dormant", 1U, 1U) &&
        settle_scene_transition(instance) &&
        tap(instance, 160U, 180U) &&
        settle_scene_transition(instance) &&
        tap(instance, 70U, 180U) && animate(instance, 2U) &&
        settle_scene_transition(instance) &&
        press_button(instance, P4_BUTTON_LEFT) && render_clip(
        instance, surface, prefix, "signal-list-page-1", 1U, 1U) &&
        press_button(instance, P4_BUTTON_RIGHT) && render_clip(
        instance, surface, prefix, "signal-list-page-2", 1U, 1U) &&
        tap(instance, 30U, 180U) &&
        tap(instance, 70U, 42U) && settle_scene_transition(instance) &&
        tap(instance, 70U, 180U) &&
        press_button(instance, P4_BUTTON_A) && render_clip(
            instance, surface, prefix, "pulse-rush", 1U, 1U);
    success = success && finish_signal_battle_index(
        instance, surface, 0U, 0U);
    success = success && render_clip(
        instance, surface, prefix, "signal-reward", 1U, 1U) &&
        settle_scene_transition(instance) &&
        tap(instance, 20U, 12U) && render_clip(
        instance, surface, prefix, "signal-lineage-spark", 15U, 5U) &&
        tap(instance, 70U, 180U) &&
        enter_signal_battle_index(instance, 0U, 1U) && render_clip(
        instance, surface, prefix, "resonance-weave", 1U, 1U) &&
        finish_signal_battle_index(instance, surface, 0U, 1U);
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
        tap(instance, 280U, 180U) && render_clip(
        instance, surface, prefix, "genome-eternal", 1U, 1U);
    return success;
}

int main(int argc, char **argv)
{
    const bool signal_motion = argc == 5 &&
        strcmp(argv[2], "--signal-motion") == 0;
    const bool authored_motion = argc == 5 &&
        strcmp(argv[2], "--authored-motion") == 0;
    const bool exact_animation = argc == 5 &&
        strcmp(argv[2], "--animation") == 0;
    unsigned expected_motion_frames = 0U;
    char trailing = '\0';
    if ((argc != 13 && argc != 3 && !signal_motion && !authored_motion &&
         !exact_animation) ||
        ((signal_motion || authored_motion || exact_animation) &&
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
                        "       use PREFIX - to render/hash without files\n",
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
    if (success && authored_motion) {
        success = render_authored_coverage_sequence(
            &instance, &surface, &signal_scan, state, &services,
            argv[3], expected_motion_frames);
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
        advance_ms(&instance, 720U) &&
        press_button(&instance, P4_BUTTON_B) &&
        advance_ms(&instance, 900U) && tap(&instance, 70U, 180U) &&
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
