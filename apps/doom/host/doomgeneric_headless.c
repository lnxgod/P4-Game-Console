// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 ESP32-P4 badge platform contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif
#include "doomgeneric.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define P4_DOOM_DEFAULT_FRAMES 8U
#define P4_DOOM_MAX_ALLOWED_FRAMES 100000U
#define P4_DOOM_FNV_OFFSET UINT64_C(14695981039346656037)
#define P4_DOOM_FNV_PRIME UINT64_C(1099511628211)

static struct timespec p4_doom_started_at;
static uint32_t p4_doom_frame_limit = P4_DOOM_DEFAULT_FRAMES;
static uint32_t p4_doom_frame_count;
static uint64_t p4_doom_frame_hash = P4_DOOM_FNV_OFFSET;

static uint64_t p4_doom_elapsed_ms(void)
{
    struct timespec now;
    uint64_t seconds;
    int64_t nanoseconds;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    seconds = (uint64_t)(now.tv_sec - p4_doom_started_at.tv_sec);
    nanoseconds = (int64_t)now.tv_nsec - (int64_t)p4_doom_started_at.tv_nsec;
    if (nanoseconds < 0) {
        --seconds;
        nanoseconds += INT64_C(1000000000);
    }

    return seconds * UINT64_C(1000) + (uint64_t)nanoseconds / UINT64_C(1000000);
}

static uint32_t p4_doom_read_frame_limit(void)
{
    const char *value = getenv("P4_DOOM_MAX_FRAMES");
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0') {
        return P4_DOOM_DEFAULT_FRAMES;
    }

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0 ||
        parsed > P4_DOOM_MAX_ALLOWED_FRAMES) {
        fprintf(stderr,
                "P4_DOOM_D0 FAIL invalid P4_DOOM_MAX_FRAMES=%s (expected 1..%u)\n",
                value, P4_DOOM_MAX_ALLOWED_FRAMES);
        exit(EXIT_FAILURE);
    }

    return (uint32_t)parsed;
}

void DG_Init(void)
{
    if (clock_gettime(CLOCK_MONOTONIC, &p4_doom_started_at) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    p4_doom_frame_limit = p4_doom_read_frame_limit();
    printf("P4_DOOM_D0 INIT resolution=%ux%u frames=%u\n",
           (unsigned)DOOMGENERIC_RESX, (unsigned)DOOMGENERIC_RESY,
           p4_doom_frame_limit);
    fflush(stdout);
}

void DG_DrawFrame(void)
{
    size_t pixel_count = (size_t)DOOMGENERIC_RESX * (size_t)DOOMGENERIC_RESY;
    size_t index;

    if (DG_ScreenBuffer == NULL) {
        fputs("P4_DOOM_D0 FAIL null framebuffer\n", stderr);
        exit(EXIT_FAILURE);
    }

    for (index = 0; index < pixel_count; ++index) {
        uint32_t pixel = (uint32_t)DG_ScreenBuffer[index];
        unsigned int byte_index;

        /* Hash an explicit little-endian serialization, independent of host. */
        for (byte_index = 0; byte_index < 4U; ++byte_index) {
            p4_doom_frame_hash ^= (pixel >> (byte_index * 8U)) & UINT32_C(0xff);
            p4_doom_frame_hash *= P4_DOOM_FNV_PRIME;
        }
    }

#if P4_DOOM_ARENA_HOST_TEST
    extern void p4_doom_arena_capture(const uint32_t *pixels);
    p4_doom_arena_capture(DG_ScreenBuffer);
#endif
    ++p4_doom_frame_count;
    if (p4_doom_frame_count >= p4_doom_frame_limit) {
        printf("P4_DOOM_D0 PASS frames=%u checksum=%016" PRIx64 " elapsed_ms=%" PRIu64 "\n",
               p4_doom_frame_count, p4_doom_frame_hash, p4_doom_elapsed_ms());
        fflush(stdout);
        exit(EXIT_SUCCESS);
    }
}

void DG_SleepMs(uint32_t milliseconds)
{
    struct timespec requested;
    struct timespec remaining;

    requested.tv_sec = (time_t)(milliseconds / 1000U);
    requested.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;

    while (nanosleep(&requested, &remaining) != 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            exit(EXIT_FAILURE);
        }
        requested = remaining;
    }
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)p4_doom_elapsed_ms();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    (void)pressed;
    (void)key;
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    for (;;) {
        doomgeneric_Tick();
    }
}
