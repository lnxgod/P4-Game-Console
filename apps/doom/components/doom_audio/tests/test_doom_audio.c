// SPDX-License-Identifier: GPL-2.0-or-later

#include "doom/audio_mixer.h"
#include "doom/audio_ring.h"
#include "doom/music_synth.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

static unsigned failures;

#define EXPECT_TRUE(expression_)                                                \
    do {                                                                        \
        if (!(expression_)) {                                                   \
            fprintf(stderr, "%s:%d: expected true: %s\n", __FILE__,        \
                    __LINE__, #expression_);                                    \
            failures++;                                                        \
        }                                                                       \
    } while (0)

#define EXPECT_FALSE(expression_) EXPECT_TRUE(!(expression_))

#define EXPECT_EQ(expected_, actual_)                                           \
    do {                                                                        \
        const int64_t expected_value_ = (int64_t)(expected_);                   \
        const int64_t actual_value_ = (int64_t)(actual_);                       \
        if (expected_value_ != actual_value_) {                                 \
            fprintf(stderr, "%s:%d: expected %lld, got %lld: %s\n",        \
                    __FILE__, __LINE__, (long long)expected_value_,             \
                    (long long)actual_value_, #actual_);                        \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void build_test_mus(uint8_t song[27])
{
    memset(song, 0, 27U);
    memcpy(song, "MUS\x1a", 4U);
    write_u16_le(&song[4], UINT16_C(11));
    write_u16_le(&song[6], UINT16_C(16));
    write_u16_le(&song[8], UINT16_C(1));

    size_t cursor = 16U;
    song[cursor++] = UINT8_C(0x40); /* Program change, same event group. */
    song[cursor++] = UINT8_C(0x00);
    song[cursor++] = UINT8_C(24);
    song[cursor++] = UINT8_C(0x90); /* Note on, end group. */
    song[cursor++] = UINT8_C(0xc5); /* Note 69 plus explicit velocity. */
    song[cursor++] = UINT8_C(127);
    song[cursor++] = UINT8_C(1);    /* One 140 Hz tick. */
    song[cursor++] = UINT8_C(0x80); /* Note off, end group. */
    song[cursor++] = UINT8_C(69);
    song[cursor++] = UINT8_C(1);
    song[cursor++] = UINT8_C(0x60); /* Score end. */
    EXPECT_EQ(27, cursor);
}

static void test_dmx_parser(void)
{
    uint8_t lump[72] = {0};
    lump[0] = UINT8_C(0x03);
    lump[1] = UINT8_C(0x00);
    lump[2] = UINT8_C(0x11);
    lump[3] = UINT8_C(0x2b); /* 11025 Hz. */
    write_u32_le(&lump[4], UINT32_C(64));
    for (size_t index = 0U; index < 64U; ++index) {
        lump[8U + index] = (uint8_t)index;
    }

    doom_audio_sample_t sample = {0};
    EXPECT_TRUE(doom_audio_parse_dmx_lump(lump, sizeof(lump), &sample));
    EXPECT_EQ(11025, sample.sample_rate_hz);
    EXPECT_EQ(32, sample.sample_count);
    EXPECT_TRUE(sample.samples == &lump[24]);
    EXPECT_EQ(16, sample.samples[0]);
    EXPECT_EQ(47, sample.samples[31]);

    uint8_t invalid[sizeof(lump)];
    memcpy(invalid, lump, sizeof(invalid));
    invalid[0] = 0U;
    EXPECT_FALSE(doom_audio_parse_dmx_lump(invalid, sizeof(invalid), &sample));
    memcpy(invalid, lump, sizeof(invalid));
    write_u32_le(&invalid[4], UINT32_C(73));
    EXPECT_FALSE(doom_audio_parse_dmx_lump(invalid, sizeof(invalid), &sample));
    memcpy(invalid, lump, sizeof(invalid));
    write_u32_le(&invalid[4], UINT32_C(48));
    EXPECT_FALSE(doom_audio_parse_dmx_lump(invalid, sizeof(invalid), &sample));
    memcpy(invalid, lump, sizeof(invalid));
    invalid[2] = UINT8_C(0xff);
    invalid[3] = UINT8_C(0x00);
    EXPECT_FALSE(doom_audio_parse_dmx_lump(invalid, sizeof(invalid), &sample));
    EXPECT_FALSE(doom_audio_parse_dmx_lump(NULL, sizeof(lump), &sample));
    EXPECT_FALSE(doom_audio_parse_dmx_lump(lump, 7U, &sample));
    EXPECT_FALSE(doom_audio_parse_dmx_lump(lump, sizeof(lump), NULL));
}

static void test_mixer_pan_resample_and_end(void)
{
    const uint8_t center_data[] = {UINT8_C(255), UINT8_C(128)};
    const doom_audio_sample_t center = {
        .samples = center_data,
        .sample_count = 2U,
        .sample_rate_hz = DOOM_AUDIO_OUTPUT_RATE_HZ,
    };
    doom_audio_mixer_t mixer;
    doom_audio_mixer_init(&mixer);
    EXPECT_TRUE(doom_audio_mixer_start(&mixer, 0U, &center, 127U, 127U));
    int16_t output[6] = {0};
    EXPECT_TRUE(doom_audio_mixer_render(&mixer, output, 3U));
    EXPECT_EQ(16256, output[0]);
    EXPECT_EQ(16256, output[1]);
    EXPECT_EQ(0, output[2]);
    EXPECT_EQ(0, output[3]);
    EXPECT_EQ(0, output[4]);
    EXPECT_EQ(0, output[5]);
    EXPECT_FALSE(doom_audio_mixer_voice_active(&mixer, 0U));

    const uint8_t resample_data[] = {UINT8_C(255), UINT8_C(0)};
    const doom_audio_sample_t resample = {
        .samples = resample_data,
        .sample_count = 2U,
        .sample_rate_hz = 8000U,
    };
    doom_audio_mixer_init(&mixer);
    EXPECT_TRUE(doom_audio_mixer_start(&mixer, 0U, &resample, 127U, 0U));
    memset(output, 0, sizeof(output));
    EXPECT_TRUE(doom_audio_mixer_render(&mixer, output, 3U));
    EXPECT_EQ(32512, output[0]);
    EXPECT_EQ(0, output[1]);
    EXPECT_EQ(32512, output[2]);
    EXPECT_EQ(0, output[3]);
    EXPECT_EQ(-32768, output[4]);
    EXPECT_EQ(0, output[5]);
    EXPECT_TRUE(doom_audio_mixer_voice_active(&mixer, 0U));

    int16_t tail[4] = {0};
    EXPECT_TRUE(doom_audio_mixer_render(&mixer, tail, 2U));
    EXPECT_EQ(-32768, tail[0]);
    EXPECT_EQ(0, tail[1]);
    EXPECT_EQ(0, tail[2]);
    EXPECT_EQ(0, tail[3]);
    EXPECT_FALSE(doom_audio_mixer_voice_active(&mixer, 0U));
}

static void test_mixer_update_saturation_and_bounds(void)
{
    const uint8_t loud_data[] = {UINT8_C(255), UINT8_C(255)};
    const doom_audio_sample_t loud = {
        .samples = loud_data,
        .sample_count = 2U,
        .sample_rate_hz = DOOM_AUDIO_OUTPUT_RATE_HZ,
    };
    doom_audio_mixer_t mixer;
    doom_audio_mixer_init(&mixer);
    for (size_t voice = 0U; voice < (size_t)DOOM_AUDIO_MAX_VOICES; ++voice) {
        EXPECT_TRUE(doom_audio_mixer_start(
            &mixer, voice, &loud, 127U, 0U));
    }
    int16_t frame[2] = {0};
    EXPECT_TRUE(doom_audio_mixer_render(&mixer, frame, 1U));
    EXPECT_EQ(INT16_MAX, frame[0]);
    EXPECT_EQ(0, frame[1]);
    EXPECT_TRUE(doom_audio_mixer_update(&mixer, 0U, 0U, 254U));
    EXPECT_TRUE(doom_audio_mixer_stop(&mixer, 0U));
    EXPECT_FALSE(doom_audio_mixer_update(&mixer, 0U, 1U, 127U));
    EXPECT_FALSE(doom_audio_mixer_start(
        &mixer, DOOM_AUDIO_MAX_VOICES, &loud, 1U, 127U));
    EXPECT_FALSE(doom_audio_mixer_start(&mixer, 0U, &loud, 128U, 127U));
    EXPECT_FALSE(doom_audio_mixer_render(&mixer, NULL, 1U));
    EXPECT_FALSE(doom_audio_mixer_render(&mixer, frame, 0U));
}

static void test_mus_validation_synthesis_loop_and_bounds(void)
{
    uint8_t song_bytes[27];
    build_test_mus(song_bytes);
    EXPECT_TRUE(doom_music_validate_mus(song_bytes, sizeof(song_bytes)));
    doom_music_song_t *song =
        doom_music_song_create(song_bytes, sizeof(song_bytes));
    EXPECT_TRUE(song != NULL);
    EXPECT_EQ(27, doom_music_song_length(song));

    doom_music_player_t player;
    doom_music_player_init(&player);
    EXPECT_TRUE(doom_music_player_start(&player, song, false));
    doom_music_song_release(song);
    int16_t output[512] = {0};
    EXPECT_TRUE(doom_music_player_mix(&player, output, 256U));
    int32_t peak = 0;
    for (size_t index = 0U; index < 512U; ++index) {
        const int32_t value = output[index];
        const int32_t magnitude = value < 0 ? -value : value;
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    EXPECT_TRUE(peak > 0);
    EXPECT_FALSE(doom_music_player_is_playing(&player));
    doom_music_stats_t stats = {0};
    doom_music_player_get_stats(&player, &stats);
    EXPECT_EQ(1, stats.songs_started);
    EXPECT_EQ(1, stats.notes_started);
    EXPECT_EQ(0, stats.parse_failures);
    EXPECT_TRUE(stats.maximum_absolute_mix > 0U);

    song = doom_music_song_create(song_bytes, sizeof(song_bytes));
    EXPECT_TRUE(song != NULL);
    EXPECT_TRUE(doom_music_player_start(&player, song, true));
    doom_music_song_release(song);
    doom_music_player_pause(&player);
    memset(output, 0, sizeof(output));
    EXPECT_TRUE(doom_music_player_mix(&player, output, 64U));
    EXPECT_EQ(0, output[0]);
    doom_music_player_resume(&player);
    EXPECT_TRUE(doom_music_player_set_volume(&player, 0U));
    EXPECT_TRUE(doom_music_player_mix(&player, output, 224U));
    EXPECT_TRUE(doom_music_player_mix(&player, output, 224U));
    EXPECT_TRUE(doom_music_player_is_playing(&player));
    doom_music_player_get_stats(&player, &stats);
    EXPECT_TRUE(stats.loops_completed >= 1U);
    doom_music_player_stop(&player);
    EXPECT_FALSE(doom_music_player_is_playing(&player));

    uint8_t invalid[sizeof(song_bytes)];
    memcpy(invalid, song_bytes, sizeof(invalid));
    invalid[0] = 0U;
    EXPECT_FALSE(doom_music_validate_mus(invalid, sizeof(invalid)));
    memcpy(invalid, song_bytes, sizeof(invalid));
    write_u16_le(&invalid[6], UINT16_C(28));
    EXPECT_FALSE(doom_music_validate_mus(invalid, sizeof(invalid)));
    memcpy(invalid, song_bytes, sizeof(invalid));
    invalid[16] = UINT8_C(0x50); /* Undefined event type. */
    EXPECT_FALSE(doom_music_validate_mus(invalid, sizeof(invalid)));
    memcpy(invalid, song_bytes, sizeof(invalid));
    invalid[26] = UINT8_C(0x00); /* No score-end marker. */
    EXPECT_FALSE(doom_music_validate_mus(invalid, sizeof(invalid)));
    EXPECT_FALSE(doom_music_validate_mus(NULL, sizeof(song_bytes)));
    EXPECT_TRUE(doom_music_song_create(invalid, sizeof(invalid)) == NULL);
    EXPECT_FALSE(doom_music_player_mix(NULL, output, 1U));
    EXPECT_FALSE(doom_music_player_mix(&player, NULL, 1U));
    EXPECT_FALSE(doom_music_player_mix(&player, output, 0U));
    EXPECT_FALSE(doom_music_player_set_volume(&player, UINT8_C(128)));
}

static doom_audio_command_t command_for(uint32_t sequence)
{
    const doom_audio_command_t command = {
        .type = (sequence & UINT32_C(1)) != 0U
                    ? DOOM_AUDIO_COMMAND_UPDATE
                    : DOOM_AUDIO_COMMAND_START,
        .voice_index = (uint8_t)(sequence % DOOM_AUDIO_MAX_VOICES),
        .volume = (uint8_t)(sequence % UINT32_C(128)),
        .separation = (uint8_t)(sequence % UINT32_C(255)),
        .desired_state = sequence,
        .sample = NULL,
    };
    return command;
}

static void test_command_ring_full_empty_and_wrap(void)
{
    doom_audio_command_ring_t ring;
    doom_audio_command_ring_init(&ring);
    doom_audio_command_t actual = {0};
    EXPECT_FALSE(doom_audio_command_ring_pop(&ring, &actual));
    for (uint32_t sequence = 0U;
         sequence < (uint32_t)DOOM_AUDIO_COMMAND_RING_CAPACITY; ++sequence) {
        const doom_audio_command_t command = command_for(sequence);
        EXPECT_TRUE(doom_audio_command_ring_push(&ring, &command));
    }
    const doom_audio_command_t overflow = command_for(UINT32_C(999));
    EXPECT_FALSE(doom_audio_command_ring_push(&ring, &overflow));
    for (uint32_t sequence = 0U;
         sequence < (uint32_t)DOOM_AUDIO_COMMAND_RING_CAPACITY; ++sequence) {
        EXPECT_TRUE(doom_audio_command_ring_pop(&ring, &actual));
        EXPECT_EQ(sequence, actual.desired_state);
    }
    EXPECT_FALSE(doom_audio_command_ring_pop(&ring, &actual));

    for (uint32_t sequence = 0U; sequence < UINT32_C(4096); ++sequence) {
        const doom_audio_command_t command = command_for(sequence);
        EXPECT_TRUE(doom_audio_command_ring_push(&ring, &command));
        EXPECT_TRUE(doom_audio_command_ring_pop(&ring, &actual));
        EXPECT_EQ(sequence, actual.desired_state);
    }
    EXPECT_FALSE(doom_audio_command_ring_push(NULL, &overflow));
    EXPECT_FALSE(doom_audio_command_ring_push(&ring, NULL));
    EXPECT_FALSE(doom_audio_command_ring_pop(NULL, &actual));
    EXPECT_FALSE(doom_audio_command_ring_pop(&ring, NULL));
}

enum {
    THREADED_COMMAND_COUNT = 100000,
};

typedef struct {
    doom_audio_command_ring_t ring;
    atomic_bool producer_done;
    unsigned consumer_failures;
} threaded_ring_fixture_t;

static void *ring_producer(void *argument)
{
    threaded_ring_fixture_t *const fixture = argument;
    for (uint32_t sequence = 0U;
         sequence < (uint32_t)THREADED_COMMAND_COUNT; ++sequence) {
        const doom_audio_command_t command = command_for(sequence);
        while (!doom_audio_command_ring_push(&fixture->ring, &command)) {
            (void)sched_yield();
        }
    }
    atomic_store_explicit(
        &fixture->producer_done, true, memory_order_release);
    return NULL;
}

static void *ring_consumer(void *argument)
{
    threaded_ring_fixture_t *const fixture = argument;
    uint32_t expected = 0U;
    while (expected < (uint32_t)THREADED_COMMAND_COUNT) {
        doom_audio_command_t command;
        if (!doom_audio_command_ring_pop(&fixture->ring, &command)) {
            if (atomic_load_explicit(
                    &fixture->producer_done, memory_order_acquire)) {
                fixture->consumer_failures++;
                break;
            }
            (void)sched_yield();
            continue;
        }
        if (command.desired_state != expected ||
            command.voice_index !=
                (uint8_t)(expected % (uint32_t)DOOM_AUDIO_MAX_VOICES)) {
            fixture->consumer_failures++;
        }
        ++expected;
    }
    return NULL;
}

static void test_command_ring_threaded_spsc(void)
{
    threaded_ring_fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    doom_audio_command_ring_init(&fixture.ring);
    atomic_init(&fixture.producer_done, false);
    pthread_t producer;
    pthread_t consumer;
    const int producer_result = pthread_create(
        &producer, NULL, ring_producer, &fixture);
    const int consumer_result = pthread_create(
        &consumer, NULL, ring_consumer, &fixture);
    EXPECT_EQ(0, producer_result);
    EXPECT_EQ(0, consumer_result);
    if (producer_result == 0) {
        EXPECT_EQ(0, pthread_join(producer, NULL));
    }
    if (consumer_result == 0) {
        EXPECT_EQ(0, pthread_join(consumer, NULL));
    }
    EXPECT_EQ(0, fixture.consumer_failures);
}

int main(void)
{
    test_dmx_parser();
    test_mixer_pan_resample_and_end();
    test_mixer_update_saturation_and_bounds();
    test_mus_validation_synthesis_loop_and_bounds();
    test_command_ring_full_empty_and_wrap();
    test_command_ring_threaded_spsc();
    if (failures != 0U) {
        fprintf(stderr, "Doom audio core tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_DOOM_AUDIO HOST PASS dmx=bounded mixer=pcm16-stereo-16khz "
         "music=mus-140hz-16voice ring=spsc-nonblocking sanitizers=enabled");
    return 0;
}
