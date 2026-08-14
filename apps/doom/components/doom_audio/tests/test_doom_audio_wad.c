// SPDX-License-Identifier: GPL-2.0-or-later
/* Reads a user-local WAD at test time; no game-data bytes enter the source. */

#include "doom/audio_mixer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    EXPECTED_SHAREWARE_SFX_LUMPS = 55,
    EXPECTED_SHAREWARE_11025_HZ_LUMPS = 54,
    EXPECTED_SHAREWARE_22050_HZ_LUMPS = 1,
    EXPECTED_SHAREWARE_PLAYABLE_SAMPLES = 532927,
};

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static int fail(const char *message)
{
    fprintf(stderr, "Doom audio WAD test failed: %s\n", message);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2 || argv[1] == NULL) {
        return fail("expected one WAD path");
    }
    FILE *const file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return fail("cannot open WAD");
    }
    const long file_length = ftell(file);
    if (file_length < 12L || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return fail("invalid WAD length");
    }
    const size_t wad_bytes = (size_t)file_length;
    uint8_t *const wad = malloc(wad_bytes);
    if (wad == NULL || fread(wad, 1U, wad_bytes, file) != wad_bytes ||
        fclose(file) != 0) {
        free(wad);
        return fail("cannot read WAD");
    }
    if (memcmp(wad, "IWAD", 4U) != 0) {
        free(wad);
        return fail("not an IWAD");
    }

    const uint32_t lump_count = read_u32_le(&wad[4]);
    const uint32_t directory_offset = read_u32_le(&wad[8]);
    const uint64_t directory_bytes = (uint64_t)lump_count * UINT64_C(16);
    if (lump_count == 0U || (uint64_t)directory_offset > (uint64_t)wad_bytes ||
        directory_bytes > (uint64_t)wad_bytes - (uint64_t)directory_offset) {
        free(wad);
        return fail("directory is out of bounds");
    }

    uint32_t sfx_count = 0U;
    uint32_t rate_11025_count = 0U;
    uint32_t rate_22050_count = 0U;
    uint64_t playable_samples = 0U;
    for (uint32_t index = 0U; index < lump_count; ++index) {
        const uint8_t *const entry =
            &wad[(size_t)directory_offset + ((size_t)index * 16U)];
        if (entry[8] != (uint8_t)'D' || entry[9] != (uint8_t)'S') {
            continue;
        }
        const uint32_t lump_offset = read_u32_le(entry);
        const uint32_t lump_bytes = read_u32_le(&entry[4]);
        if ((uint64_t)lump_offset > (uint64_t)wad_bytes ||
            (uint64_t)lump_bytes >
                (uint64_t)wad_bytes - (uint64_t)lump_offset) {
            free(wad);
            return fail("sound lump is out of bounds");
        }
        doom_audio_sample_t sample;
        if (!doom_audio_parse_dmx_lump(
                &wad[lump_offset], (size_t)lump_bytes, &sample)) {
            free(wad);
            return fail("shareware sound lump was rejected");
        }
        ++sfx_count;
        playable_samples += (uint64_t)sample.sample_count;
        if (sample.sample_rate_hz == UINT32_C(11025)) {
            ++rate_11025_count;
        } else if (sample.sample_rate_hz == UINT32_C(22050)) {
            ++rate_22050_count;
        } else {
            free(wad);
            return fail("unexpected sound sample rate");
        }
    }
    free(wad);

    if (sfx_count != EXPECTED_SHAREWARE_SFX_LUMPS ||
        rate_11025_count != EXPECTED_SHAREWARE_11025_HZ_LUMPS ||
        rate_22050_count != EXPECTED_SHAREWARE_22050_HZ_LUMPS ||
        playable_samples != EXPECTED_SHAREWARE_PLAYABLE_SAMPLES) {
        return fail("shareware sound inventory changed");
    }
    printf("P4_DOOM_AUDIO_WAD HOST PASS lumps=%u rate11025=%u "
           "rate22050=%u playable_samples=%llu\n",
           (unsigned)sfx_count, (unsigned)rate_11025_count,
           (unsigned)rate_22050_count,
           (unsigned long long)playable_samples);
    return 0;
}
