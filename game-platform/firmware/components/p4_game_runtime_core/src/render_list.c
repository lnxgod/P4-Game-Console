#include "p4/runtime_core.h"

#include <stddef.h>
#include <string.h>

static p4_render_command_t *next_command(p4_render_writer_t *writer)
{
    p4_render_command_t *command;

    if (writer == NULL || writer->packet == NULL) {
        if (writer != NULL) {
            writer->dropped_count++;
        }
        return NULL;
    }

    if (writer->packet->command_count >= P4_RENDER_MAX_COMMANDS) {
        writer->dropped_count++;
        writer->capacity_overflowed = true;
        return NULL;
    }

    command = &writer->packet->commands[writer->packet->command_count];
    writer->packet->command_count++;
    memset(command, 0, sizeof(*command));
    command->size = (uint16_t)sizeof(*command);
    return command;
}

void p4_render_writer_begin(
    p4_render_writer_t *writer,
    p4_render_packet_t *packet,
    uint32_t generation,
    uint64_t tick)
{
    if (writer == NULL) {
        return;
    }

    writer->packet = packet;
    writer->dropped_count = 0U;
    writer->capacity_overflowed = false;

    if (packet != NULL) {
        packet->tick = tick;
        packet->generation = generation;
        packet->command_count = 0U;
        packet->version = P4_GAME_API_VERSION;
        packet->flags = 0U;
    }
}

void p4_render_clear(p4_render_writer_t *writer, uint16_t color_rgb565)
{
    p4_render_command_t *command = next_command(writer);
    if (command == NULL) {
        return;
    }

    command->type = (uint8_t)P4_RENDER_COMMAND_CLEAR;
    command->color_rgb565 = color_rgb565;
}

void p4_render_rect(
    p4_render_writer_t *writer,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color_rgb565)
{
    p4_render_command_t *command;

    if (width <= 0 || height <= 0) {
        if (writer != NULL) {
            writer->dropped_count++;
        }
        return;
    }

    command = next_command(writer);
    if (command == NULL) {
        return;
    }

    command->type = (uint8_t)P4_RENDER_COMMAND_RECT;
    command->x = x;
    command->y = y;
    command->width = width;
    command->height = height;
    command->color_rgb565 = color_rgb565;
}

void p4_render_sprite(
    p4_render_writer_t *writer,
    p4_asset_id_t asset_id,
    int32_t x,
    int32_t y,
    uint32_t frame,
    uint8_t flags)
{
    p4_render_command_t *command = next_command(writer);
    if (command == NULL) {
        return;
    }

    command->type = (uint8_t)P4_RENDER_COMMAND_SPRITE;
    command->asset_id = asset_id;
    command->x = x;
    command->y = y;
    command->frame = frame;
    command->flags = flags;
}

void p4_render_writer_finish(p4_render_writer_t *writer)
{
    if (writer != NULL && writer->packet != NULL && writer->capacity_overflowed) {
        writer->packet->flags |= P4_RENDER_PACKET_FLAG_OVERFLOW;
    }
}

uint32_t p4_render_writer_dropped_count(const p4_render_writer_t *writer)
{
    return writer != NULL ? writer->dropped_count : 0U;
}

p4_status_t p4_render_packet_validate(
    const p4_render_packet_t *packet,
    uint32_t expected_generation)
{
    uint32_t index;

    if (packet == NULL || expected_generation == 0U) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    if (packet->version != P4_GAME_API_VERSION || packet->generation != expected_generation ||
        packet->command_count > P4_RENDER_MAX_COMMANDS ||
        (packet->flags & (uint16_t)~P4_RENDER_PACKET_FLAG_OVERFLOW) != 0U) {
        return P4_STATUS_INVALID_STATE;
    }

    for (index = 0U; index < packet->command_count; ++index) {
        const p4_render_command_t *command = &packet->commands[index];
        if (command->size != (uint16_t)sizeof(*command)) {
            return P4_STATUS_INVALID_STATE;
        }
        switch ((p4_render_command_type_t)command->type) {
        case P4_RENDER_COMMAND_CLEAR:
            break;
        case P4_RENDER_COMMAND_RECT:
            if (command->width <= 0 || command->height <= 0) {
                return P4_STATUS_INVALID_STATE;
            }
            break;
        case P4_RENDER_COMMAND_SPRITE:
            break;
        default:
            return P4_STATUS_INVALID_STATE;
        }
    }

    return P4_STATUS_OK;
}

void p4_render_pool_init(p4_render_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }
    memset(pool, 0, sizeof(*pool));
    pool->pending_slot = P4_RENDER_SLOT_NONE;
}

p4_status_t p4_render_pool_begin(
    p4_render_pool_t *pool,
    uint32_t generation,
    uint64_t tick,
    p4_render_writer_t *writer_out,
    uint8_t *slot_out)
{
    uint32_t index;

    if (pool == NULL || writer_out == NULL || slot_out == NULL || generation == 0U) {
        return P4_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < P4_RENDER_PACKET_COUNT; ++index) {
        if (pool->states[index] == (uint8_t)P4_RENDER_SLOT_FREE) {
            pool->states[index] = (uint8_t)P4_RENDER_SLOT_BUILDING;
            *slot_out = (uint8_t)index;
            p4_render_writer_begin(writer_out, &pool->packets[index], generation, tick);
            return P4_STATUS_OK;
        }
    }

    *slot_out = P4_RENDER_SLOT_NONE;
    p4_render_writer_begin(writer_out, NULL, generation, tick);
    return P4_STATUS_WOULD_BLOCK;
}

p4_status_t p4_render_pool_publish(p4_render_pool_t *pool, uint8_t slot)
{
    if (pool == NULL || slot >= P4_RENDER_PACKET_COUNT ||
        pool->states[slot] != (uint8_t)P4_RENDER_SLOT_BUILDING) {
        return P4_STATUS_INVALID_ARGUMENT;
    }

    if (pool->pending_slot != P4_RENDER_SLOT_NONE) {
        pool->states[pool->pending_slot] = (uint8_t)P4_RENDER_SLOT_FREE;
    }

    pool->states[slot] = (uint8_t)P4_RENDER_SLOT_PENDING;
    pool->pending_slot = slot;
    return P4_STATUS_OK;
}

void p4_render_pool_cancel(p4_render_pool_t *pool, uint8_t slot)
{
    if (pool != NULL && slot < P4_RENDER_PACKET_COUNT &&
        pool->states[slot] == (uint8_t)P4_RENDER_SLOT_BUILDING) {
        pool->states[slot] = (uint8_t)P4_RENDER_SLOT_FREE;
    }
}

p4_status_t p4_render_pool_acquire_latest(
    p4_render_pool_t *pool,
    uint32_t current_generation,
    const p4_render_packet_t **packet_out,
    uint8_t *slot_out)
{
    uint8_t slot;
    p4_status_t validation;

    if (pool == NULL || packet_out == NULL || slot_out == NULL || current_generation == 0U) {
        return P4_STATUS_INVALID_ARGUMENT;
    }

    *packet_out = NULL;
    *slot_out = P4_RENDER_SLOT_NONE;
    if (pool->pending_slot == P4_RENDER_SLOT_NONE) {
        return P4_STATUS_WOULD_BLOCK;
    }

    slot = pool->pending_slot;
    pool->pending_slot = P4_RENDER_SLOT_NONE;
    validation = p4_render_packet_validate(&pool->packets[slot], current_generation);
    if (validation != P4_STATUS_OK) {
        pool->states[slot] = (uint8_t)P4_RENDER_SLOT_FREE;
        return validation;
    }

    pool->states[slot] = (uint8_t)P4_RENDER_SLOT_RENDERING;
    *packet_out = &pool->packets[slot];
    *slot_out = slot;
    return P4_STATUS_OK;
}

void p4_render_pool_release(p4_render_pool_t *pool, uint8_t slot)
{
    if (pool != NULL && slot < P4_RENDER_PACKET_COUNT &&
        pool->states[slot] == (uint8_t)P4_RENDER_SLOT_RENDERING) {
        pool->states[slot] = (uint8_t)P4_RENDER_SLOT_FREE;
    }
}

void p4_render_pool_discard_generation(p4_render_pool_t *pool, uint32_t generation)
{
    uint32_t index;

    if (pool == NULL || generation == 0U) {
        return;
    }

    for (index = 0U; index < P4_RENDER_PACKET_COUNT; ++index) {
        if (pool->packets[index].generation == generation &&
            (pool->states[index] == (uint8_t)P4_RENDER_SLOT_BUILDING ||
             pool->states[index] == (uint8_t)P4_RENDER_SLOT_PENDING)) {
            if (pool->pending_slot == (uint8_t)index) {
                pool->pending_slot = P4_RENDER_SLOT_NONE;
            }
            pool->states[index] = (uint8_t)P4_RENDER_SLOT_FREE;
        }
    }
}

bool p4_render_pool_generation_busy(const p4_render_pool_t *pool, uint32_t generation)
{
    uint32_t index;

    if (pool == NULL || generation == 0U) {
        return false;
    }

    for (index = 0U; index < P4_RENDER_PACKET_COUNT; ++index) {
        if (pool->states[index] != (uint8_t)P4_RENDER_SLOT_FREE &&
            pool->packets[index].generation == generation) {
            return true;
        }
    }
    return false;
}
