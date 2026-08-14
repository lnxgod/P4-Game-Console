// SPDX-License-Identifier: MIT

#include "p4/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/audio.h"

static int s_failures;
static char s_calls[64];
static size_t s_call_count;
static platform_audio_t s_audio;
static esp_err_t s_force_result;
static esp_err_t s_recover_result;
static esp_err_t s_create_result;
static esp_err_t s_start_result;
static esp_err_t s_write_result;
static esp_err_t s_stop_result;
static esp_err_t s_destroy_result;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void record(char call)
{
    if (s_call_count + 1U < sizeof(s_calls)) {
        s_calls[s_call_count++] = call;
        s_calls[s_call_count] = '\0';
    }
}

static void reset_fixture(void)
{
    memset(s_calls, 0, sizeof(s_calls));
    s_call_count = 0U;
    s_force_result = ESP_OK;
    s_recover_result = ESP_OK;
    s_create_result = ESP_OK;
    s_start_result = ESP_OK;
    s_write_result = ESP_OK;
    s_stop_result = ESP_OK;
    s_destroy_result = ESP_OK;
}

esp_err_t platform_audio_force_safe_shutdown(void)
{
    record('H');
    return s_force_result;
}

esp_err_t platform_audio_recover(void)
{
    record('R');
    return s_recover_result;
}

esp_err_t platform_audio_create(const platform_audio_config_t *config,
                                platform_audio_t **out_audio)
{
    record('C');
    CHECK(config != NULL);
    CHECK(config->control_bus == NULL);
    CHECK(config->sample_rate_hz == 16000U);
    CHECK(config->volume_percent == 6U);
    CHECK(out_audio != NULL);
    if (s_create_result == ESP_OK) {
        *out_audio = &s_audio;
    }
    return s_create_result;
}

esp_err_t platform_audio_start(platform_audio_t *audio)
{
    record('S');
    CHECK(audio == &s_audio);
    return s_start_result;
}

esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *interleaved_pcm,
                                      size_t frame_count)
{
    record('W');
    CHECK(audio == &s_audio);
    CHECK(interleaved_pcm != NULL);
    CHECK(frame_count == 128U);
    return s_write_result;
}

esp_err_t platform_audio_stop(platform_audio_t *audio)
{
    record('T');
    CHECK(audio == &s_audio);
    return s_stop_result;
}

esp_err_t platform_audio_destroy(platform_audio_t **audio)
{
    record('D');
    CHECK(audio != NULL && *audio == &s_audio);
    if (s_destroy_result == ESP_OK) {
        *audio = NULL;
    }
    return s_destroy_result;
}

static void test_gate_and_success(void)
{
    reset_fixture();
    p4_game_platform_audio_t session;
    p4_game_platform_audio_init(&session);
    CHECK(p4_game_platform_audio_open(&session, false, 6U) ==
          ESP_ERR_NOT_ALLOWED);
    CHECK(s_call_count == 0U);
    CHECK(!session.hardware_touched);
    CHECK(p4_game_platform_audio_open(&session, true, 6U) == ESP_OK);
    CHECK(strcmp(s_calls, "HCS") == 0);
    CHECK(p4_game_platform_audio_running(&session));
    CHECK(!session.safe_high_proven);

    int16_t pcm[128U * 2U] = {0};
    CHECK(p4_game_platform_audio_write(&session, pcm, 128U) == ESP_OK);
    CHECK(strcmp(s_calls, "HCSW") == 0);
    CHECK(session.writes == 1U && session.frames_written == 128U);
    CHECK(p4_game_platform_audio_close(&session) == ESP_OK);
    CHECK(strcmp(s_calls, "HCSWTDRH") == 0);
    CHECK(!p4_game_platform_audio_running(&session));
    CHECK(session.safe_high_proven);
    CHECK(session.closes == 1U);
}

static void test_open_and_write_fail_safe(void)
{
    reset_fixture();
    p4_game_platform_audio_t session;
    p4_game_platform_audio_init(&session);
    s_create_result = ESP_FAIL;
    CHECK(p4_game_platform_audio_open(&session, true, 6U) == ESP_FAIL);
    CHECK(strcmp(s_calls, "HCRH") == 0);
    CHECK(session.safe_high_proven);

    reset_fixture();
    p4_game_platform_audio_init(&session);
    s_start_result = ESP_FAIL;
    CHECK(p4_game_platform_audio_open(&session, true, 6U) == ESP_FAIL);
    CHECK(strcmp(s_calls, "HCSDRH") == 0);
    CHECK(session.safe_high_proven);

    reset_fixture();
    p4_game_platform_audio_init(&session);
    CHECK(p4_game_platform_audio_open(&session, true, 6U) == ESP_OK);
    s_write_result = ESP_FAIL;
    int16_t pcm[128U * 2U] = {0};
    CHECK(p4_game_platform_audio_write(&session, pcm, 128U) == ESP_FAIL);
    CHECK(strcmp(s_calls, "HCSWTDRH") == 0);
    CHECK(session.write_failures == 1U);
    CHECK(session.safe_high_proven);
    CHECK(!p4_game_platform_audio_running(&session));
}

static void test_cleanup_failure_is_retained(void)
{
    reset_fixture();
    p4_game_platform_audio_t session;
    p4_game_platform_audio_init(&session);
    CHECK(p4_game_platform_audio_open(&session, true, 6U) == ESP_OK);
    s_destroy_result = ESP_FAIL;
    CHECK(p4_game_platform_audio_close(&session) == ESP_FAIL);
    CHECK(session.backend == &s_audio);
    CHECK(!session.safe_high_proven || s_stop_result == ESP_OK);
    CHECK(strcmp(s_calls, "HCSTD") == 0);
}

int main(void)
{
    test_gate_and_success();
    test_open_and_write_fail_safe();
    test_cleanup_failure_is_retained();
    if (s_failures != 0) {
        fprintf(stderr, "%d P4 game platform test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 game platform tests passed");
    return EXIT_SUCCESS;
}
