// SPDX-License-Identifier: MIT
#include "worker_runtime.h"
#include "p4/audio_worker.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    p4_game_audio_worker_t *worker;
    atomic_bool done;
    atomic_uint reads;
} observer_t;

static void *observe(void *argument)
{
    observer_t *observer = argument;
    uint32_t previous = 0;
    while (!atomic_load(&observer->done)) {
        p4_game_audio_worker_stats_t stats = {0};
        p4_game_audio_worker_stats(observer->worker, &stats);
        assert(stats.frames_written >= previous);
        assert(stats.write_failures <= 1);
        previous = stats.frames_written;
        atomic_fetch_add(&observer->reads, 1);
        mock_sleep_ms(1);
    }
    return NULL;
}

int main(void)
{
    p4_audio_mixer_t *mixer = calloc(1, sizeof(*mixer));
    assert(mixer);
    for (unsigned cycle = 0; cycle < 12; ++cycle) {
        p4_game_platform_audio_t audio = {.running = true};
        p4_game_audio_worker_t *worker = NULL;
        p4_audio_mixer_init(mixer);
        mock_audio_fail(false);
        assert(p4_game_audio_worker_open(&worker, &audio, mixer) == ESP_OK);
        observer_t observer = {.worker = worker};
        atomic_init(&observer.done, false);
        atomic_init(&observer.reads, 0);
        pthread_t reader;
        assert(pthread_create(&reader, NULL, observe, &observer) == 0);
        int16_t pcm[512];
        unsigned rejected = 0;
        for (unsigned burst = 0; burst < 100; ++burst) {
            if (burst % 7 == 0) p4_game_audio_worker_stop(worker);
            for (unsigned i = 0; i < 512; ++i) pcm[i] = burst % 2 ? 1234 : -1234;
            if (!p4_game_audio_worker_pcm(worker, pcm, 256)) ++rejected;
            /* The queue owns its copy after submission. */
            for (unsigned i = 0; i < 512; ++i) pcm[i] = 777;
        }
        assert(rejected > 0);
        mock_audio_fail(true);
        p4_game_audio_worker_stats_t stats = {0};
        /* Wait for a state transition, without asserting real-time throughput
         * under sanitizer instrumentation or on a heavily loaded host. */
        for (unsigned wait = 0; wait < 2000; ++wait) {
            p4_game_audio_worker_stats(worker, &stats);
            if (stats.write_failures == 1 && atomic_load(&observer.reads) > 0) break;
            mock_sleep_ms(1);
        }
        assert(stats.write_failures == 1 && !stats.running);
        assert(stats.rejected_commands == rejected);
        assert(!p4_game_audio_worker_pcm(worker, pcm, 1));
        atomic_store(&observer.done, true);
        assert(pthread_join(reader, NULL) == 0);
        assert(atomic_load(&observer.reads) > 0);
        /* Readers/producers stop before joined teardown; reopening reuses
         * exactly the same borrowed audio/mixer lifetime contract. */
        assert(p4_game_audio_worker_close(&worker) == ESP_OK && !worker);
    }
    free(mixer);
    puts("PASS: concurrent statistics, copied bounded submissions, rapid generations, failed writes, joined teardown/reopen");
}
