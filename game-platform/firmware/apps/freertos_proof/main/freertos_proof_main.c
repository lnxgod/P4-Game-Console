#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "p4/runtime.h"

#define PROOF_RUNTIME_MS 3000U
#define PROOF_MINIMUM_TICKS 170U

typedef struct {
    uint32_t load_calls;
    uint32_t tick_calls;
    uint32_t unload_calls;
    uint32_t interrupt_calls;
    uint32_t submit_calls;
    uint64_t total_ticks;
    uint64_t previous_tick;
    uint32_t active_generation;
    bool saw_tick;
} proof_context_t;

static const char *const TAG = "p4_game_proof";
static proof_context_t proof_context;

static p4_script_status_t proof_load(void *context, const p4_cartridge_ref_t *cartridge)
{
    proof_context_t *proof = context;
    if (cartridge == NULL || cartridge->generation == 0U ||
        cartridge->game_api_version != P4_SCRIPT_GAME_API_VERSION) {
        return P4_SCRIPT_STATUS_INVALID_ARGUMENT;
    }
    proof->load_calls++;
    proof->active_generation = cartridge->generation;
    proof->saw_tick = false;
    return P4_SCRIPT_STATUS_OK;
}

static p4_script_status_t proof_tick(
    void *context,
    const p4_script_tick_frame_t *tick,
    p4_render_writer_t *render)
{
    proof_context_t *proof = context;
    int32_t x;

    if (tick == NULL || render == NULL || tick->version != P4_SCRIPT_GAME_API_VERSION ||
        tick->dt_numerator != 1U || tick->dt_denominator != P4_SCRIPT_TICK_HZ) {
        return P4_SCRIPT_STATUS_INVALID_ARGUMENT;
    }
    if (proof->saw_tick && tick->tick != proof->previous_tick + 1U) {
        return P4_SCRIPT_STATUS_INVALID_STATE;
    }

    proof->saw_tick = true;
    proof->previous_tick = tick->tick;
    proof->tick_calls++;
    proof->total_ticks++;
    x = (int32_t)(tick->tick % (uint64_t)(P4_SCRIPT_SCREEN_WIDTH - 24U));
    p4_render_clear(render, UINT16_C(0x0010));
    p4_render_rect(render, x, 88, 24, 24, UINT16_C(0xffe0));
    return P4_SCRIPT_STATUS_OK;
}

static void proof_interrupt(void *context)
{
    proof_context_t *proof = context;
    proof->interrupt_calls++;
}

static p4_script_status_t proof_unload(void *context)
{
    proof_context_t *proof = context;
    proof->unload_calls++;
    return P4_SCRIPT_STATUS_OK;
}

static bool proof_read_input(
    void *context, uint8_t player, p4_script_raw_input_t *input_out)
{
    (void)context;
    if (input_out == NULL) {
        return false;
    }
    memset(input_out, 0, sizeof(*input_out));
    input_out->source_epoch = (uint32_t)player + 1U;
    return true;
}

static p4_script_status_t proof_submit(void *context, const p4_script_render_packet_t *packet)
{
    proof_context_t *proof = context;
    if (p4_render_packet_validate(packet, proof->active_generation) != P4_SCRIPT_STATUS_OK ||
        packet->command_count != 2U) {
        return P4_SCRIPT_STATUS_INVALID_STATE;
    }
    proof->submit_calls++;
    return P4_SCRIPT_STATUS_OK;
}

void app_main(void)
{
    p4_runtime_config_t config;
    p4_runtime_stats_t stats;
    p4_script_status_t result;
    p4_cartridge_ref_t cartridge = {
        .generation = 1U,
        .game_api_version = P4_SCRIPT_GAME_API_VERSION,
        .code_bytes = 1U,
        .asset_bytes = 0U,
        .storage_id = 1U,
    };
    const p4_cartridge_backend_t backend = {
        .context = &proof_context,
        .load = proof_load,
        .tick = proof_tick,
        .request_interrupt = proof_interrupt,
        .unload = proof_unload,
    };
    const p4_input_source_t input = {
        .context = NULL,
        .read = proof_read_input,
    };
    const p4_render_sink_t render = {
        .context = &proof_context,
        .submit = proof_submit,
    };

    memset(&proof_context, 0, sizeof(proof_context));
    ESP_LOGI(TAG, "F0_START peripheral_free=true tick_hz=%" PRIu32, P4_SCRIPT_TICK_HZ);

    p4_runtime_default_config(&config);
    config.stop_timeout_ms = 1000U;
    for (cartridge.generation = 1U; cartridge.generation <= 2U; ++cartridge.generation) {
        result = p4_runtime_start(&config, &cartridge, &backend, &input, &render);
        if (result != P4_SCRIPT_STATUS_OK) {
            ESP_LOGE(
                TAG,
                "F0_FAIL start generation=%" PRIu32 " status=%" PRId32,
                cartridge.generation,
                result);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(PROOF_RUNTIME_MS));
        result = p4_runtime_request_stop(config.stop_timeout_ms);
        if (result != P4_SCRIPT_STATUS_OK) {
            ESP_LOGE(
                TAG,
                "F0_FAIL stop generation=%" PRIu32 " status=%" PRId32,
                cartridge.generation,
                result);
            return;
        }
    }
    p4_runtime_get_stats(&stats);

    if (result == P4_SCRIPT_STATUS_OK && p4_runtime_get_state() == P4_LIFECYCLE_IDLE &&
        proof_context.load_calls == 2U && proof_context.unload_calls == 2U &&
        proof_context.total_ticks >= ((uint64_t)PROOF_MINIMUM_TICKS * UINT64_C(2)) &&
        proof_context.submit_calls != 0U && stats.simulation_ticks >= PROOF_MINIMUM_TICKS &&
        stats.tick_failures == 0U &&
        stats.render_failures == 0U) {
        ESP_LOGI(
            TAG,
            "F0_PASS ticks=%" PRIu64 " rendered=%" PRIu64
            " dropped=%" PRIu32 " misses=%" PRIu32
            " game_stack_free=%" PRIu32 " render_stack_free=%" PRIu32,
            proof_context.total_ticks,
            stats.frames_rendered,
            stats.frames_dropped,
            stats.deadline_misses,
            stats.game_stack_min_free_bytes,
            stats.render_stack_min_free_bytes);
    } else {
        ESP_LOGE(
            TAG,
            "F0_FAIL stop=%" PRId32 " state=%u load=%" PRIu32
            " ticks=%" PRIu32 " unload=%" PRIu32 " submits=%" PRIu32
            " tick_failures=%" PRIu32 " render_failures=%" PRIu32,
            result,
            (unsigned int)p4_runtime_get_state(),
            proof_context.load_calls,
            proof_context.tick_calls,
            proof_context.unload_calls,
            proof_context.submit_calls,
            stats.tick_failures,
            stats.render_failures);
    }
}
