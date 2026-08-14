// SPDX-License-Identifier: GPL-2.0-or-later
/* Reads a user-local WAD at test time; no game-data bytes enter the source. */

#include "doom/audio_mixer.h"
#include "doom/music_synth.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    EXPECTED_SHAREWARE_SFX_LUMPS = 55,
    EXPECTED_SHAREWARE_11025_HZ_LUMPS = 54,
    EXPECTED_SHAREWARE_22050_HZ_LUMPS = 1,
    EXPECTED_SHAREWARE_PLAYABLE_SAMPLES = 532927,
    EXPECTED_SHAREWARE_MUSIC_LUMPS = 13,
    EXPECTED_SHAREWARE_MUSIC_BYTES = 245179,
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
    uint32_t music_count = 0U;
    uint64_t music_bytes = 0U;
    doom_music_song_t *e1m1_song = NULL;
    for (uint32_t index = 0U; index < lump_count; ++index) {
        const uint8_t *const entry =
            &wad[(size_t)directory_offset + ((size_t)index * 16U)];
        const uint32_t lump_offset = read_u32_le(entry);
        const uint32_t lump_bytes = read_u32_le(&entry[4]);
        if ((uint64_t)lump_offset > (uint64_t)wad_bytes ||
            (uint64_t)lump_bytes >
                (uint64_t)wad_bytes - (uint64_t)lump_offset) {
            doom_music_song_release(e1m1_song);
            free(wad);
            return fail("candidate audio lump is out of bounds");
        }
        if (entry[8] == (uint8_t)'D' && entry[9] == (uint8_t)'_' &&
            lump_bytes >= 4U &&
            memcmp(&wad[lump_offset], "MUS\x1a", 4U) == 0) {
            if (!doom_music_validate_mus(
                    &wad[lump_offset], (size_t)lump_bytes)) {
                doom_music_song_release(e1m1_song);
                free(wad);
                return fail("shareware music lump was rejected");
            }
            ++music_count;
            music_bytes += (uint64_t)lump_bytes;
            if (memcmp(&entry[8], "D_E1M1", 6U) == 0) {
                e1m1_song = doom_music_song_create(
                    &wad[lump_offset], (size_t)lump_bytes);
                if (e1m1_song == NULL) {
                    free(wad);
                    return fail("cannot copy D_E1M1");
                }
            }
            continue;
        }
        if (entry[8] != (uint8_t)'D' || entry[9] != (uint8_t)'S') {
            continue;
        }
        doom_audio_sample_t sample;
        if (!doom_audio_parse_dmx_lump(
                &wad[lump_offset], (size_t)lump_bytes, &sample)) {
            doom_music_song_release(e1m1_song);
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
            doom_music_song_release(e1m1_song);
            free(wad);
            return fail("unexpected sound sample rate");
        }
    }
    free(wad);

    if (sfx_count != EXPECTED_SHAREWARE_SFX_LUMPS ||
        rate_11025_count != EXPECTED_SHAREWARE_11025_HZ_LUMPS ||
        rate_22050_count != EXPECTED_SHAREWARE_22050_HZ_LUMPS ||
        playable_samples != EXPECTED_SHAREWARE_PLAYABLE_SAMPLES ||
        music_count != EXPECTED_SHAREWARE_MUSIC_LUMPS ||
        music_bytes != EXPECTED_SHAREWARE_MUSIC_BYTES ||
        e1m1_song == NULL) {
        doom_music_song_release(e1m1_song);
        return fail("shareware sound inventory changed");
    }

    doom_music_player_t player;
    doom_music_player_init(&player);
    if (!doom_music_player_start(&player, e1m1_song, true)) {
        doom_music_song_release(e1m1_song);
        return fail("cannot start D_E1M1");
    }
    doom_music_song_release(e1m1_song);
    int16_t music_output[512];
    uint32_t observed_peak = 0U;
    for (unsigned chunk = 0U; chunk < 32U; ++chunk) {
        memset(music_output, 0, sizeof(music_output));
        if (!doom_music_player_mix(&player, music_output, 256U)) {
            doom_music_player_stop(&player);
            return fail("D_E1M1 render failed");
        }
        for (size_t sample_index = 0U; sample_index < 512U; ++sample_index) {
            const int32_t sample = music_output[sample_index];
            const uint32_t magnitude = sample == INT16_MIN
                ? UINT32_C(32768)
                : (uint32_t)(sample < 0 ? -sample : sample);
            if (magnitude > observed_peak) {
                observed_peak = magnitude;
            }
        }
    }
    doom_music_stats_t music_stats = {0};
    doom_music_player_get_stats(&player, &music_stats);
    doom_music_player_stop(&player);
    if (observed_peak == 0U || music_stats.notes_started == 0U ||
        music_stats.events_processed == 0U ||
        music_stats.parse_failures != 0U) {
        return fail("D_E1M1 produced no bounded music");
    }
    printf("P4_DOOM_AUDIO_WAD HOST PASS lumps=%u rate11025=%u "
           "rate22050=%u playable_samples=%llu music_lumps=%u "
           "music_bytes=%llu e1m1_peak=%u e1m1_notes=%u\n",
           (unsigned)sfx_count, (unsigned)rate_11025_count,
           (unsigned)rate_22050_count,
           (unsigned long long)playable_samples, (unsigned)music_count,
           (unsigned long long)music_bytes, (unsigned)observed_peak,
           (unsigned)music_stats.notes_started);
    return 0;
}
