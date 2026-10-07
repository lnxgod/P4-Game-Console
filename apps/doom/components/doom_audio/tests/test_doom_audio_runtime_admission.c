// SPDX-License-Identifier: GPL-2.0-or-later
/* Deterministic worker/producer schedules; no wall-clock timing or device I/O. */
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>

static void test_critical_enter(void *lock);
static void test_critical_exit(void *lock);
#define DOOM_AUDIO_TEST_CRITICAL_HOOKS 1
#define portENTER_CRITICAL(lock_) test_critical_enter(lock_)
#define portEXIT_CRITICAL(lock_) test_critical_exit(lock_)
#define s_worker_stack_hwm_bytes s_mock_worker_stack_hwm_bytes
#define main existing_runtime_tests
#define platform_audio_write_frames legacy_mock_write_frames
#include "test_doom_audio_runtime.c"
#undef platform_audio_write_frames
#undef main
#undef s_worker_stack_hwm_bytes

static void (*s_write_hook)(void);
static bool s_fail_before_publication;
static bool s_fail_after_publication;
static unsigned s_critical_depth;
static size_t s_allocations;
static size_t s_frees;
static void (*s_player_start_hook)(void);
static bool s_in_failure;
static bool s_fail_player_allocation;
static bool s_fail_next_allocation;
static bool s_fail_all_player_allocations;

void *test_music_malloc(size_t bytes)
{
    assert(s_critical_depth == 0U);
    if (s_fail_next_allocation) {
        s_fail_next_allocation = false;
        return NULL;
    }
    void *p = malloc(bytes);
    if (p != NULL) ++s_allocations;
    return p;
}

void test_music_free(void *p)
{
    assert(s_critical_depth == 0U);
    if (p != NULL) ++s_frees;
    free(p);
}

static void run_worker_failure(void)
{
    assert(!s_in_failure);
    s_in_failure = true;
    s_fail_write_after = s_write_calls + 1U;
    TaskFunction_t task = s_task;
    assert(task != NULL);
    s_task = NULL;
    task(s_task_argument);
    s_in_failure = false;
}

static void test_critical_enter(void *lock)
{
    (void)lock;
    if (s_fail_before_publication) {
        s_fail_before_publication = false;
        run_worker_failure();
    }
    assert(s_critical_depth == 0U);
    ++s_critical_depth;
}

static void test_critical_exit(void *lock)
{
    (void)lock;
    assert(s_critical_depth == 1U);
    --s_critical_depth;
    if (s_fail_after_publication) {
        s_fail_after_publication = false;
        run_worker_failure();
    }
}

esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *pcm, size_t frames)
{
    const esp_err_t result = legacy_mock_write_frames(audio, pcm, frames);
    if (s_write_hook != NULL) s_write_hook();
    return result;
}

static bool intercepted_ring_push(doom_audio_command_ring_t *ring,
                                  const doom_audio_command_t *command)
{
    if (s_fail_before_publication && command->type == DOOM_AUDIO_COMMAND_MUSIC_PLAY) {
        s_fail_before_publication = false;
        run_worker_failure();
    }
    const bool result = doom_audio_command_ring_push(ring, command);
    if (s_fail_after_publication && command->type == DOOM_AUDIO_COMMAND_MUSIC_PLAY) {
        s_fail_after_publication = false;
        run_worker_failure();
    }
    return result;
}

static bool intercepted_player_start(doom_music_player_t *player,
                                     doom_music_song_t *song, bool loop)
{
    assert(s_critical_depth == 0U);
    if (s_player_start_hook != NULL) {
        void (*hook)(void) = s_player_start_hook;
        s_player_start_hook = NULL;
        hook();
    }
    if (s_fail_player_allocation || s_fail_all_player_allocations) {
        s_fail_player_allocation = false;
        s_fail_next_allocation = true;
    }
    return doom_music_player_start(player, song, loop);
}

#define doom_audio_command_ring_push intercepted_ring_push
#define doom_music_player_start intercepted_player_start
#ifndef DOOM_RUNTIME_UNDER_TEST
#define DOOM_RUNTIME_UNDER_TEST "../src/doom_audio_runtime.c"
#endif
#include DOOM_RUNTIME_UNDER_TEST
#undef doom_audio_command_ring_push
#undef doom_music_player_start

static const uint8_t midi_song[] = {
    'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
    'M','T','r','k',0,0,0,8,0,0x90,60,100,96,0xff,0x2f,0
};
static const uint8_t mus_song[] = {
    'M','U','S',0x1a,5,0,16,0,1,0,0,0,0,0,0,0,
    0x90,0xbc,100,100,0x60
};
static platform_audio_t test_audio;
static unsigned s_schedule;
static unsigned s_case;

static doom_music_song_t *new_song(bool midi)
{
    doom_music_song_t *song = doom_music_song_create(midi ? midi_song : mus_song,
                          midi ? sizeof(midi_song) : sizeof(mus_song));
    assert(song != NULL);
    return song;
}

static void begin_case(void)
{
    reset_mocks();
    s_write_hook = NULL;
    s_player_start_hook = NULL;
    s_fail_before_publication = false;
    s_fail_after_publication = false;
    s_schedule = 0U;
    s_fail_player_allocation = false;
    s_fail_next_allocation = false;
    s_fail_all_player_allocations = false;
    test_audio.state = PLATFORM_AUDIO_STATE_READY_MUTED;
    EXPECT_EQ(ESP_OK, bind_fixture(&test_audio));
    EXPECT_EQ(ESP_OK, doom_audio_runtime_start());
}

static void end_case(void)
{
    s_write_hook = NULL;
    EXPECT_EQ(ESP_OK, doom_audio_runtime_stop());
    EXPECT_EQ(ESP_OK, doom_audio_runtime_unbind());
    EXPECT_EQ(0, s_critical_depth);
}

static void fill_sfx_ring(void)
{
    static const uint8_t bytes[] = {128U};
    static const doom_audio_sample_t sample = {
        .samples = bytes, .sample_count = sizeof(bytes),
        .sample_rate_hz = DOOM_AUDIO_OUTPUT_RATE_HZ,
    };
    for (unsigned i = 0U; i < DOOM_AUDIO_COMMAND_RING_CAPACITY; ++i)
        EXPECT_EQ(true, doom_audio_runtime_start_voice(0U, &sample, 127U, 127U));
}

static void saturation_hook(void)
{
    doom_audio_runtime_stats_t stats;
    EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
    ++s_schedule;
    if (s_schedule == 1U) {
        EXPECT_EQ(true, stats.music_playing);
        if (s_case == 2U) {
            EXPECT_EQ(true, doom_audio_runtime_music_pause());
        } else {
            fill_sfx_ring();
            EXPECT_EQ(true, s_case == 0U ? doom_audio_runtime_music_stop()
                                        : doom_audio_runtime_music_pause());
        }
    } else if (s_schedule == 2U) {
        if (s_case == 0U) EXPECT_EQ(false, stats.music_playing);
        else EXPECT_EQ(true, stats.music_paused);
        if (s_case == 2U) {
            fill_sfx_ring();
            EXPECT_EQ(true, doom_audio_runtime_music_resume());
        }
    } else if (s_case == 2U && s_schedule == 3U) {
        EXPECT_EQ(false, stats.music_paused);
    }
}

static void test_saturation(unsigned operation, bool midi)
{
    begin_case();
    s_case = operation;
    doom_music_song_t *song = new_song(midi);
    EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
    doom_music_song_release(song);
    s_write_hook = saturation_hook;
    s_fail_write_after = operation == 2U ? 4U : 3U;
    TaskFunction_t task = s_task; s_task = NULL; task(s_task_argument);
    end_case();
    EXPECT_EQ(s_allocations, s_frees);
}

static void test_final_drain_race(bool midi, bool before)
{
    begin_case();
    doom_music_song_t *song = new_song(midi);
    s_fail_before_publication = before;
    s_fail_after_publication = !before;
    EXPECT_EQ(!before, doom_audio_runtime_music_play(song, true));
    doom_music_song_release(song);
    end_case();
    EXPECT_EQ(s_allocations, s_frees);
    /* Keep a test-only rescue pointer, then actually reset the old queue.
     * Baseline loses ownership without freeing; rescue only after proving it. */
    doom_music_song_t *orphan = NULL;
    const uint32_t read_index = atomic_load(&s_command_ring.read_index);
    if (read_index != atomic_load(&s_command_ring.write_index)) {
        const doom_audio_command_t left = s_command_ring.commands[
            read_index & (DOOM_AUDIO_COMMAND_RING_CAPACITY - 1U)];
        if (left.type == DOOM_AUDIO_COMMAND_MUSIC_PLAY) orphan = left.music_song;
    }
    begin_case();
    end_case();
    EXPECT_EQ(s_allocations, s_frees);
    doom_music_song_release(orphan);
    EXPECT_EQ(s_allocations, s_frees);
}

static void ordering_hook(void)
{
    doom_audio_runtime_stats_t stats;
    EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
    EXPECT_EQ(s_case != 0U && s_case != 4U && s_case != 5U, stats.music_playing);
    EXPECT_EQ(s_case == 1U, stats.music_paused);
    s_write_hook = NULL;
}

static void test_pending_order(unsigned order, bool midi)
{
    begin_case();
    s_case = order;
    doom_music_song_t *song = new_song(midi);
    EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
    switch (order) {
    case 0U: EXPECT_EQ(true, doom_audio_runtime_music_stop()); break;
    case 1U: EXPECT_EQ(true, doom_audio_runtime_music_pause()); break;
    case 2U:
        EXPECT_EQ(true, doom_audio_runtime_music_pause());
        EXPECT_EQ(true, doom_audio_runtime_music_play(song, true)); break;
    case 3U:
        EXPECT_EQ(true, doom_audio_runtime_music_pause());
        EXPECT_EQ(true, doom_audio_runtime_music_resume()); break;
    case 4U:
        EXPECT_EQ(true, doom_audio_runtime_music_stop());
        EXPECT_EQ(true, doom_audio_runtime_music_resume()); break;
    case 5U:
        EXPECT_EQ(true, doom_audio_runtime_music_stop());
        EXPECT_EQ(true, doom_audio_runtime_music_pause()); break;
    default:
        EXPECT_EQ(true, doom_audio_runtime_music_stop());
        EXPECT_EQ(true, doom_audio_runtime_music_play(song, true)); break;
    }
    doom_music_song_release(song);
    s_write_hook = ordering_hook;
    run_worker_failure();
    end_case();
    EXPECT_EQ(s_allocations, s_frees);
}

static void queue_replacement_from_start(void)
{
    doom_music_song_t *song = new_song(true);
    if (s_case == 0U) EXPECT_EQ(true, doom_audio_runtime_music_stop());
    else EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
    doom_music_song_release(song);
}

static void detached_hook(void)
{
    ++s_schedule;
    if (s_schedule == 2U) {
        doom_audio_runtime_stats_t stats;
        EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
        EXPECT_EQ(s_case != 0U, stats.music_playing);
        EXPECT_EQ(s_case == 0U ? 1U : 2U, stats.music_songs_started);
    }
}

static void test_detached_order(unsigned action)
{
    begin_case(); s_case = action;
    doom_music_song_t *song = new_song(false);
    EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
    doom_music_song_release(song);
    s_player_start_hook = queue_replacement_from_start;
    s_write_hook = detached_hook;
    s_fail_write_after = 3U;
    TaskFunction_t task = s_task; s_task = NULL; task(s_task_argument);
    end_case();
    EXPECT_EQ(s_allocations, s_frees);
}

static void failed_replacement_hook(void)
{
    ++s_schedule;
    doom_audio_runtime_stats_t stats;
    EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
    if (s_schedule == 1U) {
        EXPECT_EQ(true, doom_audio_runtime_music_pause());
    } else if (s_schedule == 2U) {
        EXPECT_EQ(true, stats.music_paused);
        doom_music_song_t *song = new_song(true);
        EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
        doom_music_song_release(song);
        EXPECT_EQ(true, doom_audio_runtime_music_resume());
        s_fail_player_allocation = true;
    } else if (s_schedule == 3U) {
        EXPECT_EQ(false, s_fail_next_allocation);
        EXPECT_EQ(true, stats.music_playing);
        EXPECT_EQ(false, stats.music_paused);
        EXPECT_EQ(1, stats.music_songs_started);
    }
}

static void test_failed_replacement_resume(void)
{
    begin_case();
    doom_music_song_t *song = new_song(true);
    EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
    doom_music_song_release(song);
    s_write_hook = failed_replacement_hook;
    s_fail_write_after = 4U;
    TaskFunction_t task = s_task; s_task = NULL; task(s_task_argument);
    end_case();
    EXPECT_EQ(s_allocations, s_frees);
}

static void enqueue_failed_replacement(unsigned order)
{
    if (order == 0U || order == 4U)
        EXPECT_EQ(true, doom_audio_runtime_music_stop());
    else if (order == 1U)
        EXPECT_EQ(true, doom_audio_runtime_music_pause());
    else if (order == 2U)
        EXPECT_EQ(true, doom_audio_runtime_music_resume());
    doom_music_song_t *song = new_song(true);
    EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
    doom_music_song_release(song);
    if (order == 3U || order == 4U) {
        if (order == 3U) EXPECT_EQ(true, doom_audio_runtime_music_pause());
        song = new_song(true);
        EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
        doom_music_song_release(song);
    }
    s_fail_all_player_allocations = true;
}

static void prior_control_hook(void)
{
    ++s_schedule;
    if (s_schedule == 1U) {
        if (s_case == 2U) EXPECT_EQ(true, doom_audio_runtime_music_pause());
        else enqueue_failed_replacement(s_case);
    } else if (s_schedule == 2U && s_case == 2U) {
        enqueue_failed_replacement(s_case);
    } else if (s_schedule == (s_case == 2U ? 3U : 2U)) {
        doom_audio_runtime_stats_t stats;
        EXPECT_EQ(ESP_OK, doom_audio_runtime_get_stats(&stats));
        EXPECT_EQ(s_case != 0U && s_case != 4U, stats.music_playing);
        EXPECT_EQ(s_case == 1U || s_case == 3U, stats.music_paused);
        EXPECT_EQ(1U, stats.music_songs_started);
        s_fail_all_player_allocations = false;
    }
}

static void test_prior_control_survives_failed_replacement(unsigned order)
{
    begin_case(); s_case = order;
    doom_music_song_t *song = new_song(true);
    EXPECT_EQ(true, doom_audio_runtime_music_play(song, true));
    doom_music_song_release(song);
    s_write_hook = prior_control_hook;
    s_fail_write_after = order == 2U ? 4U : 3U;
    TaskFunction_t task = s_task; s_task = NULL; task(s_task_argument);
    end_case();
    EXPECT_EQ(s_allocations, s_frees);
}

int main(void)
{
    (void)existing_runtime_tests();
    for (unsigned midi = 0U; midi < 2U; ++midi) {
        for (unsigned control = 0U; control < 3U; ++control)
            test_saturation(control, midi != 0U);
        test_final_drain_race(midi != 0U, true);
        test_final_drain_race(midi != 0U, false);
        for (unsigned order = 0U; order < 7U; ++order)
            test_pending_order(order, midi != 0U);
    }
    test_detached_order(0U);
    test_detached_order(1U);
    test_failed_replacement_resume();
    for (unsigned order = 0U; order < 5U; ++order)
        test_prior_control_survives_failed_replacement(order);
    printf("RUNTIME_ADMISSION schedules=32 legacy=10 failures=%u allocations=%zu frees=%zu\n",
           s_failures, s_allocations, s_frees);
    return s_failures != 0U ? 1 : 0;
}
