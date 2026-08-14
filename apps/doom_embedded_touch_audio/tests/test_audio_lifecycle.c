// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_lifecycle.h"

#include <assert.h>
#include <stdint.h>

#include "doom/audio_runtime.h"

struct platform_audio_factory {
    unsigned identity;
};

static struct platform_audio_factory s_audio = {.identity = 1U};
static esp_err_t s_stop_result;
static esp_err_t s_unbind_result;
static esp_err_t s_destroy_result;
static esp_err_t s_recover_result;
static unsigned s_stop_calls;
static unsigned s_unbind_calls;
static unsigned s_destroy_calls;
static unsigned s_recover_calls;

static void reset_mocks(void)
{
    s_stop_result = ESP_OK;
    s_unbind_result = ESP_OK;
    s_destroy_result = ESP_OK;
    s_recover_result = ESP_OK;
    s_stop_calls = 0U;
    s_unbind_calls = 0U;
    s_destroy_calls = 0U;
    s_recover_calls = 0U;
}

esp_err_t doom_audio_runtime_stop(void)
{
    ++s_stop_calls;
    return s_stop_result;
}

esp_err_t doom_audio_runtime_unbind(void)
{
    ++s_unbind_calls;
    return s_unbind_result;
}

esp_err_t platform_audio_destroy(platform_audio_t **audio)
{
    ++s_destroy_calls;
    if (s_destroy_result == ESP_OK) {
        *audio = NULL;
    }
    return s_destroy_result;
}

esp_err_t platform_audio_recover(void)
{
    ++s_recover_calls;
    return s_recover_result;
}

static doom_e6_audio_lifecycle_t bound_lifecycle(void)
{
    const doom_e6_audio_lifecycle_t lifecycle = {
        .audio = &s_audio,
        .hardware_touched = true,
        .safe_high_proven = true,
        .runtime_bound = true,
        .released = false,
    };
    return lifecycle;
}

static void test_untouched_fast_path(void)
{
    reset_mocks();
    doom_e6_audio_lifecycle_t lifecycle = {0};
    doom_e6_audio_release_result_t result;
    assert(doom_e6_audio_release(&lifecycle, &result) == ESP_OK);
    assert(result.complete);
    assert(lifecycle.released);
    assert(s_stop_calls + s_unbind_calls + s_destroy_calls +
               s_recover_calls == 0U);
}

static void test_failed_engine_init_bound_cleanup(void)
{
    reset_mocks();
    s_stop_result = ESP_ERR_INVALID_STATE;
    doom_e6_audio_lifecycle_t lifecycle = bound_lifecycle();
    doom_e6_audio_release_result_t result;
    assert(doom_e6_audio_release(&lifecycle, &result) == ESP_OK);
    assert(result.complete);
    assert(!lifecycle.runtime_bound);
    assert(lifecycle.audio == NULL);
    assert(lifecycle.safe_high_proven);
    assert(s_stop_calls == 1U && s_unbind_calls == 1U &&
           s_destroy_calls == 1U && s_recover_calls == 1U);
}

static void test_timeout_retains_single_owner(void)
{
    reset_mocks();
    s_stop_result = ESP_ERR_TIMEOUT;
    doom_e6_audio_lifecycle_t lifecycle = bound_lifecycle();
    lifecycle.safe_high_proven = false;
    doom_e6_audio_release_result_t result;
    assert(doom_e6_audio_release(&lifecycle, &result) == ESP_ERR_TIMEOUT);
    assert(!result.complete && lifecycle.runtime_bound);
    assert(lifecycle.audio == &s_audio);
    assert(!lifecycle.safe_high_proven);
    assert(s_unbind_calls == 0U && s_destroy_calls == 0U &&
           s_recover_calls == 0U);
}

static void test_unbind_failure_retains_single_owner(void)
{
    reset_mocks();
    s_stop_result = ESP_ERR_INVALID_STATE;
    s_unbind_result = ESP_FAIL;
    doom_e6_audio_lifecycle_t lifecycle = bound_lifecycle();
    doom_e6_audio_release_result_t result;
    assert(doom_e6_audio_release(&lifecycle, &result) == ESP_FAIL);
    assert(!result.complete && lifecycle.runtime_bound);
    assert(lifecycle.audio == &s_audio);
    assert(s_destroy_calls == 0U && s_recover_calls == 0U);
}

static void test_recover_failure_never_claims_safe_release(void)
{
    reset_mocks();
    s_recover_result = ESP_ERR_INVALID_RESPONSE;
    doom_e6_audio_lifecycle_t lifecycle = bound_lifecycle();
    lifecycle.safe_high_proven = false;
    doom_e6_audio_release_result_t result;
    assert(doom_e6_audio_release(&lifecycle, &result) ==
           ESP_ERR_INVALID_RESPONSE);
    assert(!result.complete && !lifecycle.released);
    assert(!lifecycle.runtime_bound && lifecycle.audio == NULL);
    assert(!lifecycle.safe_high_proven);
}

static void test_destroy_failure_retains_then_second_pass_recovers(void)
{
    reset_mocks();
    s_destroy_result = ESP_FAIL;
    doom_e6_audio_lifecycle_t lifecycle = bound_lifecycle();
    doom_e6_audio_release_result_t result;
    assert(doom_e6_audio_release(&lifecycle, &result) == ESP_FAIL);
    assert(!result.complete && !lifecycle.runtime_bound);
    assert(lifecycle.audio == &s_audio);
    assert(s_recover_calls == 0U);

    s_destroy_result = ESP_OK;
    assert(doom_e6_audio_release(&lifecycle, &result) == ESP_OK);
    assert(result.complete && lifecycle.audio == NULL);
    assert(s_stop_calls == 1U && s_unbind_calls == 1U &&
           s_destroy_calls == 2U && s_recover_calls == 1U);
}

static void test_initial_safe_failure_recover_only(void)
{
    reset_mocks();
    doom_e6_audio_lifecycle_t lifecycle = {
        .audio = NULL,
        .hardware_touched = true,
        .safe_high_proven = false,
        .runtime_bound = false,
        .released = false,
    };
    doom_e6_audio_release_result_t result;
    assert(doom_e6_audio_release(&lifecycle, &result) == ESP_OK);
    assert(result.complete && lifecycle.safe_high_proven);
    assert(s_stop_calls == 0U && s_unbind_calls == 0U &&
           s_destroy_calls == 0U && s_recover_calls == 1U);
}

int main(void)
{
    test_untouched_fast_path();
    test_failed_engine_init_bound_cleanup();
    test_timeout_retains_single_owner();
    test_unbind_failure_retains_single_owner();
    test_recover_failure_never_claims_safe_release();
    test_destroy_failure_retains_then_second_pass_recovers();
    test_initial_safe_failure_recover_only();
    return 0;
}
