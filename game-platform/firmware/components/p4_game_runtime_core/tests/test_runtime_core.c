#include "p4/runtime_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            exit(EXIT_FAILURE);                                                             \
        }                                                                                   \
    } while (0)

_Static_assert(sizeof(p4_script_raw_input_t) == 80U, "raw input layout changed");
_Static_assert(sizeof(p4_script_input_frame_t) == 112U, "input frame layout changed");
_Static_assert(sizeof(p4_script_tick_frame_t) == 472U, "tick frame layout changed");
_Static_assert(sizeof(p4_script_render_command_t) == 32U, "render command layout changed");

static void test_fractional_scheduler(void)
{
    p4_tick_scheduler_t scheduler;
    uint32_t total = 0U;
    uint32_t interval = 0U;
    uint32_t index;

    CHECK(p4_tick_scheduler_init(&scheduler, 1000U, P4_SCRIPT_TICK_HZ) == P4_SCRIPT_STATUS_OK);
    for (index = 0U; index < P4_SCRIPT_TICK_HZ; ++index) {
        CHECK(p4_tick_scheduler_next(&scheduler, &interval) == P4_SCRIPT_STATUS_OK);
        CHECK(interval == 16U || interval == 17U);
        total += interval;
    }
    CHECK(total == 1000U);
    CHECK(scheduler.phase == 0U);

    total = 0U;
    CHECK(p4_tick_scheduler_init(&scheduler, 100U, 60U) == P4_SCRIPT_STATUS_OK);
    for (index = 0U; index < 60U; ++index) {
        CHECK(p4_tick_scheduler_next(&scheduler, &interval) == P4_SCRIPT_STATUS_OK);
        CHECK(interval == 1U || interval == 2U);
        total += interval;
    }
    CHECK(total == 100U);
    CHECK(scheduler.phase == 0U);

    total = 0U;
    CHECK(p4_tick_scheduler_init(&scheduler, 16000U, 60U) == P4_SCRIPT_STATUS_OK);
    for (index = 0U; index < 60U; ++index) {
        CHECK(p4_tick_scheduler_next(&scheduler, &interval) == P4_SCRIPT_STATUS_OK);
        CHECK(interval == 266U || interval == 267U);
        total += interval;
    }
    CHECK(total == 16000U);
    CHECK(scheduler.phase == 0U);

    CHECK(p4_tick_scheduler_init(&scheduler, 50U, 60U) == P4_SCRIPT_STATUS_INVALID_ARGUMENT);
}

static void test_input_edges_and_neutral_barriers(void)
{
    p4_input_latch_t latch;
    p4_script_raw_input_t raw;
    p4_script_input_frame_t frame;
    uint32_t epoch;

    memset(&raw, 0, sizeof(raw));
    raw.connected = 1U;
    raw.source_epoch = 7U;
    raw.down = UINT64_C(1) << P4_SCRIPT_BUTTON_A;
    raw.dpad = P4_SCRIPT_DPAD_UP | P4_SCRIPT_DPAD_LEFT;
    raw.touch_count = 1U;
    raw.touches[0].x = 700;
    raw.touches[0].y = 300;
    raw.touches[0].pressed = 7U;

    p4_input_latch_init(&latch);
    p4_input_latch_sample(&latch, &raw, 1U, &frame);
    CHECK(frame.connected == 0U);
    CHECK(frame.down == 0U);
    epoch = frame.input_epoch;

    p4_input_latch_sample(&latch, &raw, 2U, &frame);
    CHECK(frame.connected == 1U);
    CHECK(frame.down == (UINT64_C(1) << P4_SCRIPT_BUTTON_A));
    CHECK(frame.pressed == frame.down);
    CHECK(frame.released == 0U);
    CHECK(frame.dpad == (P4_SCRIPT_DPAD_UP | P4_SCRIPT_DPAD_LEFT));
    CHECK(frame.dpad_pressed == frame.dpad);
    CHECK(frame.touch_count == 1U);
    CHECK(frame.touches[0].x == 700);
    CHECK(frame.touches[0].pressed == 1U);

    p4_input_latch_sample(&latch, &raw, 3U, &frame);
    CHECK(frame.pressed == 0U);
    CHECK(frame.dpad_pressed == 0U);

    raw.connected = 0U;
    p4_input_latch_sample(&latch, &raw, 4U, &frame);
    CHECK(frame.connected == 0U);
    CHECK(frame.down == 0U);
    CHECK(frame.released == (UINT64_C(1) << P4_SCRIPT_BUTTON_A));
    CHECK(frame.dpad_released == (P4_SCRIPT_DPAD_UP | P4_SCRIPT_DPAD_LEFT));
    CHECK(frame.input_epoch != epoch);
    epoch = frame.input_epoch;

    raw.connected = 1U;
    p4_input_latch_sample(&latch, &raw, 5U, &frame);
    CHECK(frame.connected == 0U);
    CHECK(frame.input_epoch == epoch);
    p4_input_latch_sample(&latch, &raw, 6U, &frame);
    CHECK(frame.connected == 1U);
    CHECK(frame.pressed == (UINT64_C(1) << P4_SCRIPT_BUTTON_A));

    raw.source_epoch++;
    p4_input_latch_sample(&latch, &raw, 7U, &frame);
    CHECK(frame.connected == 0U);
    CHECK(frame.down == 0U);
    p4_input_latch_sample(&latch, &raw, 8U, &frame);
    CHECK(frame.connected == 1U);
    CHECK(frame.pressed == (UINT64_C(1) << P4_SCRIPT_BUTTON_A));

    p4_input_latch_reset(&latch);
    p4_input_latch_sample(&latch, &raw, 9U, &frame);
    CHECK(frame.connected == 0U);
}

static void test_render_writer_bounds(void)
{
    p4_script_render_packet_t packet;
    p4_render_writer_t writer;
    uint32_t index;

    memset(&packet, 0, sizeof(packet));
    p4_render_writer_begin(&writer, &packet, 9U, 42U);
    p4_render_rect(&writer, 0, 0, 0, 1, UINT16_C(0xffff));
    CHECK(packet.command_count == 0U);
    CHECK(p4_render_writer_dropped_count(&writer) == 1U);
    p4_render_writer_finish(&writer);
    CHECK((packet.flags & P4_SCRIPT_RENDER_PACKET_FLAG_OVERFLOW) == 0U);

    for (index = 0U; index < P4_SCRIPT_RENDER_MAX_COMMANDS + 1U; ++index) {
        p4_render_clear(&writer, (uint16_t)index);
    }
    p4_render_writer_finish(&writer);

    CHECK(packet.command_count == P4_SCRIPT_RENDER_MAX_COMMANDS);
    CHECK(p4_render_writer_dropped_count(&writer) == 2U);
    CHECK((packet.flags & P4_SCRIPT_RENDER_PACKET_FLAG_OVERFLOW) != 0U);
    CHECK(p4_render_packet_validate(&packet, 9U) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_render_packet_validate(&packet, 8U) == P4_SCRIPT_STATUS_INVALID_STATE);
}

static void test_render_pool_latest_wins(void)
{
    p4_render_pool_t pool;
    p4_render_writer_t writer;
    const p4_script_render_packet_t *packet = NULL;
    uint8_t first = P4_SCRIPT_RENDER_SLOT_NONE;
    uint8_t second = P4_SCRIPT_RENDER_SLOT_NONE;
    uint8_t acquired = P4_SCRIPT_RENDER_SLOT_NONE;

    p4_render_pool_init(&pool);
    CHECK(p4_render_pool_begin(&pool, 3U, 1U, &writer, &first) == P4_SCRIPT_STATUS_OK);
    p4_render_clear(&writer, 0U);
    p4_render_writer_finish(&writer);
    CHECK(p4_render_pool_publish(&pool, first) == P4_SCRIPT_STATUS_OK);

    CHECK(p4_render_pool_begin(&pool, 3U, 2U, &writer, &second) == P4_SCRIPT_STATUS_OK);
    p4_render_clear(&writer, 1U);
    p4_render_writer_finish(&writer);
    CHECK(p4_render_pool_publish(&pool, second) == P4_SCRIPT_STATUS_OK);
    CHECK(pool.states[first] == (uint8_t)P4_SCRIPT_RENDER_SLOT_FREE);

    CHECK(p4_render_pool_acquire_latest(&pool, 3U, &packet, &acquired) == P4_SCRIPT_STATUS_OK);
    CHECK(packet != NULL);
    CHECK(packet->tick == 2U);
    CHECK(acquired == second);
    CHECK(p4_render_pool_generation_busy(&pool, 3U));
    p4_render_pool_release(&pool, acquired);
    CHECK(!p4_render_pool_generation_busy(&pool, 3U));

    CHECK(p4_render_pool_begin(&pool, 3U, 3U, &writer, &first) == P4_SCRIPT_STATUS_OK);
    p4_render_clear(&writer, 2U);
    p4_render_writer_finish(&writer);
    CHECK(p4_render_pool_publish(&pool, first) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_render_pool_acquire_latest(&pool, 4U, &packet, &acquired) ==
          P4_SCRIPT_STATUS_INVALID_STATE);
    CHECK(packet == NULL);
}

static void test_lifecycle(void)
{
    p4_lifecycle_t lifecycle;

    p4_lifecycle_init(&lifecycle);
    CHECK(p4_lifecycle_mark_loaded(&lifecycle) == P4_SCRIPT_STATUS_INVALID_STATE);
    CHECK(p4_lifecycle_begin_load(&lifecycle, 12U) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_lifecycle_mark_loaded(&lifecycle) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_lifecycle_request_stop(&lifecycle) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_lifecycle_request_stop(&lifecycle) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_lifecycle_mark_quiesced(&lifecycle) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_lifecycle_begin_render_drain(&lifecycle) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_lifecycle_begin_unload(&lifecycle) == P4_SCRIPT_STATUS_OK);
    CHECK(p4_lifecycle_finish_unload(&lifecycle) == P4_SCRIPT_STATUS_OK);
    CHECK(lifecycle.state == P4_LIFECYCLE_IDLE);
    CHECK(lifecycle.generation == 0U);

    CHECK(p4_lifecycle_begin_load(&lifecycle, 13U) == P4_SCRIPT_STATUS_OK);
    p4_lifecycle_fault(&lifecycle, P4_SCRIPT_STATUS_BACKEND_FAILED);
    CHECK(lifecycle.state == P4_LIFECYCLE_FAULTED);
    CHECK(p4_lifecycle_request_stop(&lifecycle) == P4_SCRIPT_STATUS_OK);
}

int main(void)
{
    test_fractional_scheduler();
    test_input_edges_and_neutral_barriers();
    test_render_writer_bounds();
    test_render_pool_latest_wins();
    test_lifecycle();
    (void)puts("p4_game_runtime_core_tests: PASS");
    return EXIT_SUCCESS;
}
