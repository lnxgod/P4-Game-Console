#include "p4/lua_runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char save_key[P4_LUA_SAVE_KEY_MAX_BYTES + 1U];
    uint8_t save_value[P4_LUA_SAVE_VALUE_MAX_BYTES];
    size_t save_bytes;
    unsigned tone_calls;
    unsigned stop_all_calls;
    bool save_present;
} test_services_t;

static bool test_tone(
    void *context,
    uint8_t channel,
    uint16_t frequency_hz,
    uint16_t duration_ticks,
    uint8_t volume,
    p4_lua_waveform_t waveform)
{
    test_services_t *services = context;
    assert(channel == 1U);
    assert(frequency_hz == 440U);
    assert(duration_ticks == 3U);
    assert(volume == 120U);
    assert(waveform == P4_LUA_WAVE_TRIANGLE);
    services->tone_calls++;
    return true;
}

static void test_stop_all(void *context)
{
    test_services_t *services = context;
    services->stop_all_calls++;
}

static bool test_save_get(
    void *context,
    const char *key,
    uint8_t *value_out,
    size_t value_capacity,
    size_t *value_bytes_out)
{
    const test_services_t *services = context;
    if (!services->save_present || strcmp(key, services->save_key) != 0 ||
        services->save_bytes > value_capacity) {
        return false;
    }
    memcpy(value_out, services->save_value, services->save_bytes);
    *value_bytes_out = services->save_bytes;
    return true;
}

static bool test_save_set(
    void *context,
    const char *key,
    const uint8_t *value,
    size_t value_bytes)
{
    test_services_t *services = context;
    const size_t key_bytes = strlen(key);
    if (key_bytes > P4_LUA_SAVE_KEY_MAX_BYTES ||
        value_bytes > sizeof(services->save_value)) {
        return false;
    }
    memcpy(services->save_key, key, key_bytes + 1U);
    memcpy(services->save_value, value, value_bytes);
    services->save_bytes = value_bytes;
    services->save_present = true;
    return true;
}

static bool test_save_remove(void *context, const char *key)
{
    test_services_t *services = context;
    if (services->save_present && strcmp(key, services->save_key) == 0) {
        services->save_present = false;
        return true;
    }
    return false;
}

static void initialize_tick(p4_script_tick_frame_t *tick, uint64_t number)
{
    memset(tick, 0, sizeof(*tick));
    tick->tick = number;
    tick->generation = 7U;
    tick->dt_numerator = 1U;
    tick->dt_denominator = P4_SCRIPT_TICK_HZ;
    tick->version = P4_SCRIPT_GAME_API_VERSION;
    tick->size = (uint16_t)sizeof(*tick);
    for (size_t player = 0U; player < P4_SCRIPT_MAX_PLAYERS; ++player) {
        tick->input[player].tick = number;
        tick->input[player].version = P4_SCRIPT_GAME_API_VERSION;
        tick->input[player].size = (uint16_t)sizeof(tick->input[player]);
    }
}

static p4_script_status_t run_tick(
    p4_lua_runtime_t *runtime,
    p4_script_tick_frame_t *tick,
    p4_script_render_packet_t *packet)
{
    p4_render_writer_t writer;
    p4_render_writer_begin(&writer, packet, tick->generation, tick->tick);
    const p4_script_status_t status = p4_lua_runtime_tick(runtime, tick, &writer);
    p4_render_writer_finish(&writer);
    return status;
}

static void test_game_api_and_arcade(void)
{
    static const char source[] =
        "local game, q = {}, p4.arcade\n"
        "local hero = q.actor(10, 20, 8, 9, q.WHITE)\n"
        "function game.start()\n"
        "  assert(os == nil and io == nil and debug == nil and package == nil)\n"
        "  assert(require == nil and load == nil and string.dump == nil)\n"
        "  assert(math.random == nil and getmetatable(p4) == false)\n"
        "  local ok = pcall(function() p4.screen.bad = true end)\n"
        "  assert(not ok)\n"
        "  assert(p4.save.set('score', '7'))\n"
        "  assert(p4.save.get('score') == '7')\n"
        "  assert(p4.audio.tone(1, 440, 3, 120, 'triangle'))\n"
        "end\n"
        "function game.update()\n"
        "  if p4.input.pressed(2, 'a') then hero.x = 33 end\n"
        "  local _, y, pressed = p4.input.touch(1)\n"
        "  if pressed then hero.y = y end\n"
        "end\n"
        "function game.draw()\n"
        "  p4.screen.clear(q.BLACK)\n"
        "  q.draw(hero)\n"
        "  p4.screen.line(1, 2, 3, 4, q.CYAN)\n"
        "  p4.screen.circle(50, 60, 7, q.YELLOW, false)\n"
        "  p4.screen.text('OK', 70, 80, q.WHITE)\n"
        "end\n"
        "function game.stop() p4.save.remove('score') end\n"
        "return game\n";
    p4_lua_runtime_t runtime = {0};
    p4_lua_config_t config;
    p4_script_tick_frame_t tick;
    p4_script_render_packet_t packet;
    test_services_t services = {0};

    p4_lua_config_default(&config);
    config.services.context = &services;
    config.services.play_tone = test_tone;
    config.services.stop_all_audio = test_stop_all;
    config.services.save_get = test_save_get;
    config.services.save_set = test_save_set;
    config.services.save_remove = test_save_remove;
    assert(p4_lua_runtime_load(
        &runtime,
        &config,
        (const uint8_t *)source,
        sizeof(source) - 1U,
        "@test.lua") == P4_SCRIPT_STATUS_OK);
    assert(p4_lua_runtime_loaded(&runtime));
    assert(services.tone_calls == 1U);
    assert(services.save_present);

    initialize_tick(&tick, 2U);
    tick.input[0].touch_count = 1U;
    tick.input[0].touches[0].x = 120;
    tick.input[0].touches[0].y = 99;
    tick.input[0].touches[0].pressed = 1U;
    tick.input[1].connected = 1U;
    tick.input[1].down = UINT64_C(1) << P4_SCRIPT_BUTTON_A;
    tick.input[1].pressed = UINT64_C(1) << P4_SCRIPT_BUTTON_A;
    assert(run_tick(&runtime, &tick, &packet) == P4_SCRIPT_STATUS_OK);
    assert(p4_render_packet_validate(&packet, 7U) == P4_SCRIPT_STATUS_OK);
    assert(packet.command_count == 5U);
    assert(packet.commands[0].type == P4_SCRIPT_RENDER_COMMAND_CLEAR);
    assert(packet.commands[1].type == P4_SCRIPT_RENDER_COMMAND_RECT);
    assert(packet.commands[1].x == 33);
    assert(packet.commands[1].y == 99);
    assert(packet.commands[4].type == P4_SCRIPT_RENDER_COMMAND_TEXT);
    assert(packet.commands[4].frame == 2U);
    assert(packet.commands[4].asset_id == 0U);
    assert(packet.text_bytes == 2U);
    assert(memcmp(packet.text, "OK", 2U) == 0);

    initialize_tick(&tick, 3U);
    assert(run_tick(&runtime, &tick, &packet) == P4_SCRIPT_STATUS_OK);
    assert(packet.command_count == 0U);

    assert(p4_lua_runtime_unload(&runtime) == P4_SCRIPT_STATUS_OK);
    assert(!p4_lua_runtime_loaded(&runtime));
    assert(!services.save_present);
    assert(services.stop_all_calls == 1U);
}

static void test_instruction_budget(void)
{
    static const char source[] =
        "local game = {}\n"
        "function game.start() end\n"
        "function game.update() while true do end end\n"
        "function game.draw() end\n"
        "return game\n";
    p4_lua_runtime_t runtime = {0};
    p4_lua_config_t config;
    p4_script_tick_frame_t tick;
    p4_script_render_packet_t packet;

    p4_lua_config_default(&config);
    config.callback_instruction_limit = 1000U;
    assert(p4_lua_runtime_load(
        &runtime, &config, (const uint8_t *)source, sizeof(source) - 1U,
        "@loop.lua") == P4_SCRIPT_STATUS_OK);
    initialize_tick(&tick, 1U);
    assert(run_tick(&runtime, &tick, &packet) == P4_SCRIPT_STATUS_TIMED_OUT);
    assert(strstr(p4_lua_runtime_last_error(&runtime), "instruction limit") != NULL);
    (void)p4_lua_runtime_unload(&runtime);
}

static void test_rejected_sources(void)
{
    static const uint8_t bytecode[] = {0x1bU, 'L', 'u', 'a'};
    static const uint8_t embedded_nul[] = {'r', 'e', 't', 'u', 'r', 'n', 0U};
    static const char incomplete[] = "return {start=function() end}";
    p4_lua_config_t config;
    p4_lua_runtime_t runtime = {0};

    p4_lua_config_default(&config);
    assert(p4_lua_runtime_load(
        &runtime, &config, bytecode, sizeof(bytecode), "@bytecode") ==
        P4_SCRIPT_STATUS_INVALID_ARGUMENT);
    assert(p4_lua_runtime_load(
        &runtime, &config, embedded_nul, sizeof(embedded_nul), "@nul") ==
        P4_SCRIPT_STATUS_INVALID_ARGUMENT);
    assert(p4_lua_runtime_load(
        &runtime,
        &config,
        (const uint8_t *)incomplete,
        sizeof(incomplete) - 1U,
        "@incomplete.lua") == P4_SCRIPT_STATUS_BAD_FORMAT);
    assert(!p4_lua_runtime_loaded(&runtime));
}

int main(void)
{
    test_game_api_and_arcade();
    test_instruction_budget();
    test_rejected_sources();
    puts("p4_lua_runtime_tests: PASS");
    return 0;
}
