// SPDX-License-Identifier: GPL-2.0-or-later
/* Synthetic SMF fixtures are authored here; pack music stays in its WAD. */
#include "doom/music_synth.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t midi[] = {
    'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
    'M','T','r','k',0,0,0,29,
    0,0xc0,40, 0,0xb0,64,127, 0,0x90,60,100,
    96,60,0, 96,0xb0,64,0,
    0,0xff,0x51,3,0x0f,0x42,0x40, 96,0xff,0x2f,0
};
static const uint8_t mus[] = {
    'M','U','S',0x1a,5,0,16,0,1,0,0,0,0,0,0,0,
    0x90,0xbc,100,14,0x60
};

static void mix_frames(doom_music_player_t *p, size_t frames)
{
    int16_t pcm[512];
    while (frames != 0U) {
        const size_t n = frames > 256U ? 256U : frames;
        memset(pcm, 0, sizeof(pcm));
        assert(doom_music_player_mix(p, pcm, n));
        frames -= n;
    }
}

static void test_lifecycle(void)
{
    uint8_t bytes[sizeof(midi)];
    memcpy(bytes, midi, sizeof(bytes));
    doom_music_song_t *song = doom_music_song_create(bytes, sizeof(bytes));
    assert(song != NULL); /* Reproduces the 0.64 Arena registration failure. */
    assert(doom_music_song_length(song) == sizeof(bytes));
    memset(bytes, 0, sizeof(bytes)); /* Caller storage may go away immediately. */
    doom_music_player_t p;
    doom_music_player_init(&p);
    assert(doom_music_player_start(&p, song, true));
    doom_music_song_release(song); /* Active worker retains its own reference. */
    mix_frames(&p, 8000U);
    doom_music_stats_t first;
    doom_music_player_get_stats(&p, &first);
    assert(first.songs_started == 1U && first.notes_started == 1U);
    assert(first.mixed_frames == 8000U && first.maximum_absolute_mix > 0U);
    assert(first.parse_failures == 0U && first.playing);

    doom_music_player_pause(&p);
    int16_t pcm[1026];
    for (size_t i = 0U; i < 1026U; ++i) pcm[i] = 1234;
    assert(doom_music_player_mix(&p, pcm, 513U));
    for (size_t i = 0U; i < 1026U; ++i) assert(pcm[i] == 1234);
    doom_music_stats_t paused;
    doom_music_player_get_stats(&p, &paused);
    assert(paused.paused && paused.mixed_frames == first.mixed_frames);
    doom_music_player_resume(&p);
    assert(doom_music_player_set_volume(&p, 0U));
    assert(doom_music_player_mix(&p, pcm, 513U));
    for (size_t i = 0U; i < 1026U; ++i) assert(pcm[i] == 1234);
    assert(doom_music_player_set_volume(&p, 127U));
    mix_frames(&p, 24000U);
    doom_music_player_get_stats(&p, &first);
    assert(first.loops_completed == 1U && first.notes_started == 2U);
    assert(first.mixed_frames == 32513U && first.parse_failures == 0U);

    song = doom_music_song_create(mus, sizeof(mus));
    assert(song != NULL && doom_music_player_start(&p, song, false));
    doom_music_song_release(song);
    mix_frames(&p, 2048U);
    doom_music_player_get_stats(&p, &paused);
    assert(!paused.playing && paused.songs_started == 2U);
    assert(paused.notes_started == 3U && paused.mixed_frames > first.mixed_frames);
    song = doom_music_song_create(midi, sizeof(midi));
    assert(song != NULL && doom_music_player_start(&p, song, false));
    doom_music_song_release(song);
    mix_frames(&p, 32768U);
    doom_music_player_get_stats(&p, &first);
    assert(first.songs_started == 3U && first.notes_started == 4U);
    assert(!first.playing && first.parse_failures == 0U);
    doom_music_player_stop(&p);
    doom_music_player_stop(&p);
}

static void test_bounds(void)
{
    for (size_t n = 0U; n < sizeof(midi); ++n)
        assert(doom_music_song_create(midi, n) == NULL);
    uint8_t bad[sizeof(midi)];
    memcpy(bad, midi, sizeof(bad));
    bad[9] = 2U; /* SMF type 2 is deliberately unsupported. */
    assert(doom_music_song_create(bad, sizeof(bad)) == NULL);
    memcpy(bad, midi, sizeof(bad)); bad[12] = 0x80U;
    assert(doom_music_song_create(bad, sizeof(bad)) == NULL);
    memcpy(bad, midi, sizeof(bad)); bad[28] = 0xffU; /* Invalid controller value. */
    doom_music_song_t *song = doom_music_song_create(bad, sizeof(bad));
    assert(song != NULL); /* Events are bounded and validated while sequencing. */
    doom_music_player_t p;
    doom_music_player_init(&p);
    assert(doom_music_player_start(&p, song, true));
    doom_music_song_release(song);
    int16_t pcm[512] = {0};
    assert(!doom_music_player_mix(&p, pcm, 256U));
    doom_music_stats_t stats;
    doom_music_player_get_stats(&p, &stats);
    assert(stats.parse_failures == 1U && !stats.playing);
    for (size_t i = 0U; i < 512U; ++i) assert(pcm[i] == 0);
    doom_music_player_stop(&p);
}

static void test_multitrack_tempo_sustain(void)
{
    static const uint8_t smf1[] = {
        'M','T','h','d',0,0,0,6,0,1,0,2,0,96,
        'M','T','r','k',0,0,0,18,
        0,0xff,0x51,3,7,0xa1,0x20, /* 500000 us per quarter */
        96,0xff,0x51,3,0x0f,0x42,0x40, /* 1000000 us per quarter */
        96,0xff,0x2f,0,
        'M','T','r','k',0,0,0,34,
        0,0xc0,40, 0,0xb0,10,0, 0,0xb0,7,127, 0,0xb0,11,127,
        0,0xb0,64,127, 0,0x90,60,100, 96,60,0,
        96,0xb0,64,0, 0,0xff,0x2f,0
    };
    doom_music_song_t *song = doom_music_song_create(smf1, sizeof(smf1));
    assert(song != NULL);
    doom_music_player_t p;
    doom_music_player_init(&p);
    assert(doom_music_player_start(&p, song, false));
    doom_music_song_release(song);
    mix_frames(&p, 16000U); /* Note-off at 8000; sustain keeps the voice alive. */
    int16_t pcm[512] = {0};
    assert(doom_music_player_mix(&p, pcm, 256U));
    bool audible = false;
    for (size_t i = 0U; i < 256U; ++i) {
        audible |= pcm[2U * i] != 0;
        assert(pcm[2U * i + 1U] == 0); /* Controller pan is fully left. */
    }
    assert(audible);
    mix_frames(&p, 24000U - 16256U);
    assert(doom_music_player_is_playing(&p));
    mix_frames(&p, 1U); /* Tempo change gives precisely 8000 + 16000 frames. */
    doom_music_stats_t stats;
    doom_music_player_get_stats(&p, &stats);
    assert(!stats.playing && stats.mixed_frames == 24000U);
    assert(stats.notes_started == 1U && stats.parse_failures == 0U);
    doom_music_player_stop(&p);
}

static void test_malformed_events(void)
{
    static const uint8_t bad_events[][8] = {
        {0,0xf0,0x7f,0,0,0,0,0}, /* Sysex extends beyond track. */
        {0,0xff,0x51,3,0,0,0,0}, /* Zero tempo. */
        {0,0xff,0x2f,1,0,0,0,0}, /* End event must have zero length. */
        {0,60,100,0,0,0,0,0}, /* Running status without prior status. */
        {0,0xc0,40,0x80,0x80,0x80,0x80,0} /* Unterminated delta. */
    };
    uint8_t data[30] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
                        'M','T','r','k',0,0,0,8};
    for (size_t i = 0U; i < sizeof(bad_events) / sizeof(bad_events[0]); ++i) {
        memcpy(data + 22, bad_events[i], 8U);
        doom_music_song_t *song = doom_music_song_create(data, sizeof(data));
        assert(song != NULL);
        doom_music_player_t p;
        doom_music_player_init(&p);
        assert(doom_music_player_start(&p, song, true));
        doom_music_song_release(song);
        int16_t pcm[512] = {0};
        assert(!doom_music_player_mix(&p, pcm, 256U));
        doom_music_stats_t stats;
        doom_music_player_get_stats(&p, &stats);
        assert(!stats.playing && stats.parse_failures == 1U);
        doom_music_player_stop(&p);
    }
}

static void test_event_budget(void)
{
    /* A valid-sized hostile track cannot spend unlimited time on zero-delay
     * events before the audio worker returns to its output deadline. */
    const size_t events = 4097U, track_bytes = events * 3U + 4U;
    const size_t bytes = 22U + track_bytes;
    uint8_t *data = calloc(1U, bytes);
    assert(data != NULL);
    memcpy(data, midi, 22U);
    for (size_t i = 0U; i < 4U; ++i)
        data[18U + i] = (uint8_t)(track_bytes >> ((3U - i) * 8U));
    for (size_t i = 0U; i < events; ++i) data[22U + 3U * i + 1U] = 0xc0U;
    data[bytes - 3U] = 0xffU;
    data[bytes - 2U] = 0x2fU;
    doom_music_song_t *song = doom_music_song_create(data, bytes);
    free(data);
    assert(song != NULL);
    doom_music_player_t p;
    doom_music_player_init(&p);
    assert(doom_music_player_start(&p, song, true));
    doom_music_song_release(song);
    int16_t pcm[512] = {0};
    assert(!doom_music_player_mix(&p, pcm, 256U));
    doom_music_stats_t stats;
    doom_music_player_get_stats(&p, &stats);
    assert(!stats.playing && stats.parse_failures == 1U);
    assert(stats.events_processed <= 4096U && stats.mixed_frames == 0U);
    doom_music_player_stop(&p);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U |
           (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}

static void test_wad(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f != NULL && fseek(f, 0L, SEEK_END) == 0);
    const long size = ftell(f);
    assert(size >= 12L && fseek(f, 0L, SEEK_SET) == 0);
    const size_t length = (size_t)size;
    uint8_t *wad = malloc(length);
    assert(wad != NULL && fread(wad, 1U, length, f) == length);
    assert(fclose(f) == 0);
    assert(memcmp(wad, "PWAD", 4U) == 0 || memcmp(wad, "IWAD", 4U) == 0);
    const size_t count = le32(wad + 4), directory = le32(wad + 8);
    assert(directory <= length && count <= (length - directory) / 16U);
    size_t songs = 0U;
    for (size_t i = 0U; i < count; ++i) {
        const uint8_t *entry = wad + directory + i * 16U;
        if (entry[8] != 'D' || entry[9] != '_') continue;
        const size_t offset = le32(entry), bytes = le32(entry + 4);
        assert(offset <= length && bytes <= length - offset);
        doom_music_song_t *song = doom_music_song_create(wad + offset, bytes);
        assert(song != NULL);
        doom_music_player_t p;
        doom_music_player_init(&p);
        assert(doom_music_player_start(&p, song, true));
        doom_music_song_release(song);
        mix_frames(&p, 16000U * 10U);
        doom_music_stats_t stats;
        doom_music_player_get_stats(&p, &stats);
        assert(stats.playing && stats.songs_started == 1U);
        assert(stats.notes_started > 0U && stats.maximum_absolute_mix > 0U);
        assert(stats.mixed_frames == 160000U && stats.parse_failures == 0U);
        printf("%.8s: notes=%u events=%u peak=%u\n", (const char *)(entry + 8),
               stats.notes_started, stats.events_processed, stats.maximum_absolute_mix);
        doom_music_player_stop(&p);
        ++songs;
    }
    assert(songs != 0U);
    printf("PASS: %zu original WAD music tracks produced PCM\n", songs);
    free(wad);
}

int main(int argc, char **argv)
{
    test_lifecycle();
    test_bounds();
    test_multitrack_tempo_sustain();
    test_malformed_events();
    test_event_budget();
    for (int i = 1; i < argc; ++i) test_wad(argv[i]);
    puts("PASS: Doom MIDI registration, immutable ownership, pause, volume, loop, MUS switch and bounds");
    return 0;
}
