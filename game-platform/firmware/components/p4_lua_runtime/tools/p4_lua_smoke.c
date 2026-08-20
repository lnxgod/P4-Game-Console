#include "p4/lua_runtime.h"
#include "p4/content_catalog.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool accept_tone(
    void *context,
    uint8_t channel,
    uint16_t frequency_hz,
    uint16_t duration_ticks,
    uint8_t volume,
    p4_lua_waveform_t waveform)
{
    (void)context;
    (void)channel;
    (void)frequency_hz;
    (void)duration_ticks;
    (void)volume;
    (void)waveform;
    return true;
}

static uint8_t *read_source(const char *path, size_t *bytes_out)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return NULL;
    }
    const long length = ftell(file);
    if (length <= 0L || (unsigned long)length > P4_LUA_SOURCE_MAX_BYTES ||
        fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    const size_t bytes = (size_t)length;
    uint8_t *source = malloc(bytes);
    if (source == NULL || fread(source, 1U, bytes, file) != bytes) {
        free(source);
        source = NULL;
    }
    (void)fclose(file);
    if (source != NULL) {
        *bytes_out = bytes;
    }
    return source;
}

static bool is_cart_path(const char *path)
{
    static const char extension[] = ".p4cart";
    const size_t path_bytes = strlen(path);
    if (path_bytes < sizeof(extension) - 1U) {
        return false;
    }
    const char *suffix = &path[path_bytes - (sizeof(extension) - 1U)];
    for (size_t index = 0U; index < sizeof(extension) - 1U; ++index) {
        if ((char)tolower((unsigned char)suffix[index]) != extension[index]) {
            return false;
        }
    }
    return true;
}

static void initialize_tick(p4_script_tick_frame_t *tick, uint64_t number)
{
    memset(tick, 0, sizeof(*tick));
    tick->tick = number;
    tick->generation = 1U;
    tick->dt_numerator = 1U;
    tick->dt_denominator = P4_SCRIPT_TICK_HZ;
    tick->version = P4_SCRIPT_GAME_API_VERSION;
    tick->size = (uint16_t)sizeof(*tick);
    for (size_t player = 0U; player < P4_SCRIPT_MAX_PLAYERS; ++player) {
        tick->input[player].tick = number;
        tick->input[player].version = P4_SCRIPT_GAME_API_VERSION;
        tick->input[player].size = (uint16_t)sizeof(tick->input[player]);
    }
    tick->input[0].connected = 1U;
    if (number == 1U) {
        tick->input[0].down = UINT64_C(1) << P4_SCRIPT_BUTTON_A;
        tick->input[0].pressed = UINT64_C(1) << P4_SCRIPT_BUTTON_A;
    } else if (number < 80U) {
        tick->input[0].dpad = P4_SCRIPT_DPAD_RIGHT;
    } else if (number < 140U) {
        tick->input[0].dpad = P4_SCRIPT_DPAD_LEFT;
    }
}

static bool run_source(const char *path)
{
    size_t source_bytes = 0U;
    p4_content_cart_runtime_t cart = {0};
    const bool packed_cart = is_cart_path(path);
    uint8_t *source = packed_cart
        ? malloc(P4_CONTENT_CART_SOURCE_MAX_BYTES)
        : read_source(path, &source_bytes);
    if (source == NULL) {
        fprintf(stderr, "P4_LUA_SMOKE READ_FAIL path=%s\n", path);
        return false;
    }
    if (packed_cart) {
        const p4_content_status_t content_status = p4_content_load_cart_source(
            path, source, P4_CONTENT_CART_SOURCE_MAX_BYTES, &cart);
        if (content_status != P4_CONTENT_OK) {
            fprintf(
                stderr, "P4_LUA_SMOKE CART_FAIL path=%s status=%s\n",
                path, p4_content_status_name(content_status));
            free(source);
            return false;
        }
        source_bytes = cart.source_bytes;
    }
    p4_lua_runtime_t runtime = {0};
    p4_lua_config_t config;
    p4_lua_config_default(&config);
    if (packed_cart) {
        config.heap_limit_bytes = cart.heap_bytes;
    }
    config.services.play_tone = accept_tone;
    p4_script_status_t status = p4_lua_runtime_load(
        &runtime,
        &config,
        source,
        source_bytes,
        packed_cart ? cart.entry_path : path);
    free(source);
    if (status != P4_SCRIPT_STATUS_OK) {
        fprintf(
            stderr, "P4_LUA_SMOKE LOAD_FAIL path=%s status=%ld error=%s\n",
            path, (long)status, p4_lua_runtime_last_error(&runtime));
        return false;
    }

    uint64_t rendered_commands = 0U;
    for (uint64_t number = 1U; number <= 180U; ++number) {
        p4_script_tick_frame_t tick;
        p4_script_render_packet_t packet;
        p4_render_writer_t writer;
        initialize_tick(&tick, number);
        p4_render_writer_begin(&writer, &packet, 1U, number);
        status = p4_lua_runtime_tick(&runtime, &tick, &writer);
        p4_render_writer_finish(&writer);
        if (status != P4_SCRIPT_STATUS_OK ||
            p4_render_packet_validate(&packet, 1U) != P4_SCRIPT_STATUS_OK) {
            fprintf(
                stderr,
                "P4_LUA_SMOKE TICK_FAIL path=%s tick=%" PRIu64
                " status=%ld error=%s\n",
                path, number, (long)status,
                p4_lua_runtime_last_error(&runtime));
            (void)p4_lua_runtime_unload(&runtime);
            return false;
        }
        rendered_commands += packet.command_count;
    }
    p4_lua_stats_t stats;
    p4_lua_runtime_get_stats(&runtime, &stats);
    const p4_script_status_t unload_status = p4_lua_runtime_unload(&runtime);
    if (unload_status != P4_SCRIPT_STATUS_OK) {
        fprintf(stderr, "P4_LUA_SMOKE STOP_FAIL path=%s\n", path);
        return false;
    }
    printf(
        "P4_LUA_SMOKE PASS path=%s format=%s ticks=180 draws=%lu "
        "commands=%" PRIu64 " peak_heap=%lu\n",
        path, packed_cart ? "p4cart" : "lua",
        (unsigned long)stats.draw_calls, rendered_commands,
        (unsigned long)stats.peak_heap_bytes);
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(
            stderr,
            "usage: p4_lua_smoke GAME.LUA|GAME.P4CART [...]\n");
        return 2;
    }
    bool passed = true;
    for (int index = 1; index < argc; ++index) {
        passed = run_source(argv[index]) && passed;
    }
    return passed ? 0 : 1;
}
