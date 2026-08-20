#include "p4/lua_runtime.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include "p4_arcade_source.inc"

#define P4_LUA_HOOK_INTERVAL 100U
#define P4_LUA_REGISTRY_NONE LUA_NOREF

typedef enum {
    P4_LUA_PHASE_IDLE = 0,
    P4_LUA_PHASE_LOAD,
    P4_LUA_PHASE_START,
    P4_LUA_PHASE_UPDATE,
    P4_LUA_PHASE_DRAW,
    P4_LUA_PHASE_STOP,
} p4_lua_phase_t;

typedef enum {
    P4_LUA_FAULT_NONE = 0,
    P4_LUA_FAULT_INSTRUCTION,
    P4_LUA_FAULT_MEMORY,
    P4_LUA_FAULT_API,
    P4_LUA_FAULT_INTERRUPT,
} p4_lua_fault_t;

typedef struct {
    p4_lua_runtime_t *owner;
    lua_State *lua;
    p4_lua_config_t config;
    p4_lua_stats_t stats;
    const p4_script_tick_frame_t *tick;
    p4_render_writer_t *render;
    size_t heap_bytes;
    size_t peak_heap_bytes;
    uint64_t random_state;
    uint64_t previous_tick;
    uint32_t instructions_remaining;
    uint32_t host_calls_remaining;
    int game_reference;
    int p4_backing_reference;
    atomic_uint interrupt_requested;
    p4_lua_phase_t phase;
    p4_lua_fault_t fault;
    bool saw_tick;
    bool loaded;
} p4_lua_implementation_t;

static void set_error(p4_lua_runtime_t *runtime, const char *text)
{
    if (runtime == NULL) {
        return;
    }
    if (text == NULL) {
        runtime->last_error[0] = '\0';
        return;
    }
    (void)snprintf(runtime->last_error, sizeof(runtime->last_error), "%s", text);
}

static void *default_reallocate(
    void *context, void *pointer, size_t old_size, size_t new_size)
{
    (void)context;
    (void)old_size;
    if (new_size == 0U) {
        free(pointer);
        return NULL;
    }
    return realloc(pointer, new_size);
}

static void *counting_allocator(
    void *context, void *pointer, size_t old_size, size_t new_size)
{
    p4_lua_implementation_t *implementation = context;
    p4_lua_reallocate_fn reallocate;
    size_t effective_old_size;
    size_t next_size;
    void *result;

    if (implementation == NULL) {
        return NULL;
    }
    reallocate = implementation->config.services.reallocate;
    if (reallocate == NULL) {
        reallocate = default_reallocate;
    }
    effective_old_size = pointer == NULL ? 0U : old_size;
    if (new_size == 0U) {
        (void)reallocate(
            implementation->config.services.context,
            pointer,
            effective_old_size,
            0U);
        if (effective_old_size <= implementation->heap_bytes) {
            implementation->heap_bytes -= effective_old_size;
        } else {
            implementation->heap_bytes = 0U;
        }
        implementation->stats.heap_bytes = implementation->heap_bytes;
        return NULL;
    }

    if (implementation->heap_bytes > implementation->config.heap_limit_bytes) {
        implementation->fault = P4_LUA_FAULT_MEMORY;
        implementation->stats.memory_faults++;
        return NULL;
    }
    if (new_size >= effective_old_size) {
        const size_t growth = new_size - effective_old_size;
        if (growth > implementation->config.heap_limit_bytes -
                implementation->heap_bytes) {
            implementation->fault = P4_LUA_FAULT_MEMORY;
            implementation->stats.memory_faults++;
            return NULL;
        }
        next_size = implementation->heap_bytes + growth;
    } else {
        next_size = implementation->heap_bytes -
            (effective_old_size - new_size);
    }

    result = reallocate(
        implementation->config.services.context,
        pointer,
        effective_old_size,
        new_size);
    if (result == NULL) {
        implementation->fault = P4_LUA_FAULT_MEMORY;
        implementation->stats.memory_faults++;
        return NULL;
    }
    implementation->heap_bytes = next_size;
    if (implementation->heap_bytes > implementation->peak_heap_bytes) {
        implementation->peak_heap_bytes = implementation->heap_bytes;
    }
    implementation->stats.heap_bytes = implementation->heap_bytes;
    implementation->stats.peak_heap_bytes = implementation->peak_heap_bytes;
    return result;
}

static p4_lua_implementation_t *implementation_from_lua(lua_State *lua)
{
    return lua_touserdata(lua, lua_upvalueindex(1));
}

static int raise_api_error(
    lua_State *lua, p4_lua_implementation_t *implementation, const char *message)
{
    implementation->fault = P4_LUA_FAULT_API;
    implementation->stats.api_faults++;
    return luaL_error(lua, "%s", message);
}

static bool charge_host_call(
    lua_State *lua, p4_lua_implementation_t *implementation)
{
    if (implementation->host_calls_remaining == 0U) {
        (void)raise_api_error(lua, implementation, "P4 host-call limit exceeded");
        return false;
    }
    implementation->host_calls_remaining--;
    implementation->stats.host_calls++;
    return true;
}

static void instruction_hook(lua_State *lua, lua_Debug *debug)
{
    p4_lua_implementation_t *implementation;
    (void)debug;
    implementation = *(p4_lua_implementation_t **)lua_getextraspace(lua);
    if (implementation == NULL) {
        return;
    }
    if (atomic_load_explicit(
            &implementation->interrupt_requested, memory_order_relaxed) != 0U) {
        implementation->fault = P4_LUA_FAULT_INTERRUPT;
        (void)luaL_error(lua, "P4 cartridge interrupted");
        return;
    }
    if (implementation->instructions_remaining <= P4_LUA_HOOK_INTERVAL) {
        implementation->instructions_remaining = 0U;
        implementation->fault = P4_LUA_FAULT_INSTRUCTION;
        implementation->stats.instruction_faults++;
        (void)luaL_error(lua, "P4 instruction limit exceeded");
        return;
    }
    implementation->instructions_remaining -= P4_LUA_HOOK_INTERVAL;
    implementation->stats.instructions_charged += P4_LUA_HOOK_INTERVAL;
}

static void callback_begin(
    p4_lua_implementation_t *implementation, p4_lua_phase_t phase)
{
    implementation->phase = phase;
    implementation->fault = P4_LUA_FAULT_NONE;
    implementation->instructions_remaining =
        implementation->config.callback_instruction_limit;
    implementation->host_calls_remaining = P4_LUA_CALLBACK_HOST_CALL_MAX;
    lua_sethook(
        implementation->lua,
        instruction_hook,
        LUA_MASKCOUNT,
        (int)P4_LUA_HOOK_INTERVAL);
}

static void callback_end(p4_lua_implementation_t *implementation)
{
    lua_sethook(implementation->lua, NULL, 0, 0);
    implementation->phase = P4_LUA_PHASE_IDLE;
    implementation->render = NULL;
}

static p4_script_status_t callback_status(
    p4_lua_implementation_t *implementation, int lua_status)
{
    const char *message = lua_tostring(implementation->lua, -1);
    p4_script_status_t status = P4_SCRIPT_STATUS_SCRIPT_ERROR;
    if (implementation->fault == P4_LUA_FAULT_INTERRUPT) {
        status = P4_SCRIPT_STATUS_INTERRUPTED;
    } else if (implementation->fault == P4_LUA_FAULT_INSTRUCTION) {
        status = P4_SCRIPT_STATUS_TIMED_OUT;
    } else if (implementation->fault == P4_LUA_FAULT_MEMORY ||
               lua_status == LUA_ERRMEM) {
        status = P4_SCRIPT_STATUS_OUT_OF_MEMORY;
    }
    set_error(
        implementation->owner,
        message == NULL ? "Lua callback failed without an error message" : message);
    implementation->stats.last_status = status;
    lua_pop(implementation->lua, 1);
    return status;
}

static p4_script_status_t protected_call(
    p4_lua_implementation_t *implementation,
    int arguments,
    int results,
    p4_lua_phase_t phase)
{
    int result;
    callback_begin(implementation, phase);
    result = lua_pcall(implementation->lua, arguments, results, 0);
    callback_end(implementation);
    if (result != LUA_OK) {
        return callback_status(implementation, result);
    }
    implementation->stats.callbacks_completed++;
    implementation->stats.last_status = P4_SCRIPT_STATUS_OK;
    return P4_SCRIPT_STATUS_OK;
}

static int readonly_newindex(lua_State *lua)
{
    return luaL_error(lua, "P4 API tables are read-only");
}

static void push_readonly_proxy(lua_State *lua, int backing_index)
{
    backing_index = lua_absindex(lua, backing_index);
    lua_newtable(lua);
    lua_newtable(lua);
    lua_pushvalue(lua, backing_index);
    lua_setfield(lua, -2, "__index");
    lua_pushcfunction(lua, readonly_newindex);
    lua_setfield(lua, -2, "__newindex");
    lua_pushboolean(lua, 0);
    lua_setfield(lua, -2, "__metatable");
    lua_setmetatable(lua, -2);
}

typedef struct {
    const char *name;
    lua_CFunction function;
} p4_lua_function_t;

static void set_function(
    lua_State *lua,
    int table_index,
    const char *name,
    lua_CFunction function,
    p4_lua_implementation_t *implementation)
{
    table_index = lua_absindex(lua, table_index);
    lua_pushlightuserdata(lua, implementation);
    lua_pushcclosure(lua, function, 1);
    lua_setfield(lua, table_index, name);
}

static void set_nil_global(lua_State *lua, const char *name)
{
    lua_pushnil(lua);
    lua_setglobal(lua, name);
}

static bool open_safe_libraries(lua_State *lua)
{
    luaL_requiref(lua, LUA_GNAME, luaopen_base, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(lua, 1);

    static const char *const denied_globals[] = {
        "collectgarbage", "dofile", "load", "loadfile", "print", "rawset", "warn",
        "coroutine", "debug", "io", "os", "package", "require",
    };
    for (size_t index = 0U;
         index < sizeof(denied_globals) / sizeof(denied_globals[0]);
         ++index) {
        set_nil_global(lua, denied_globals[index]);
    }
    lua_getglobal(lua, LUA_STRLIBNAME);
    if (!lua_istable(lua, -1)) {
        lua_pop(lua, 1);
        return false;
    }
    lua_pushnil(lua);
    lua_setfield(lua, -2, "dump");
    lua_pop(lua, 1);

    lua_getglobal(lua, LUA_MATHLIBNAME);
    if (!lua_istable(lua, -1)) {
        lua_pop(lua, 1);
        return false;
    }
    lua_pushnil(lua);
    lua_setfield(lua, -2, "random");
    lua_pushnil(lua);
    lua_setfield(lua, -2, "randomseed");
    lua_pop(lua, 1);
    return true;
}

static lua_Integer check_integer_range(
    lua_State *lua,
    p4_lua_implementation_t *implementation,
    int argument,
    lua_Integer minimum,
    lua_Integer maximum,
    const char *message)
{
    int is_integer = 0;
    const lua_Integer value = lua_tointegerx(lua, argument, &is_integer);
    if (is_integer == 0 || value < minimum || value > maximum) {
        (void)raise_api_error(lua, implementation, message);
        return minimum;
    }
    return value;
}

static uint16_t check_color(
    lua_State *lua, p4_lua_implementation_t *implementation, int argument)
{
    return (uint16_t)check_integer_range(
        lua, implementation, argument, 0, UINT16_MAX,
        "RGB565 color must be an integer in 0..65535");
}

static bool utf8_valid(const uint8_t *text, size_t bytes)
{
    size_t index = 0U;
    while (index < bytes) {
        const uint8_t first = text[index++];
        uint32_t value;
        uint32_t minimum;
        unsigned continuation;
        if (first < 0x80U) {
            if (first == 0U) {
                return false;
            }
            continue;
        }
        if ((first & 0xe0U) == 0xc0U) {
            value = first & 0x1fU;
            minimum = 0x80U;
            continuation = 1U;
        } else if ((first & 0xf0U) == 0xe0U) {
            value = first & 0x0fU;
            minimum = 0x800U;
            continuation = 2U;
        } else if ((first & 0xf8U) == 0xf0U) {
            value = first & 0x07U;
            minimum = 0x10000U;
            continuation = 3U;
        } else {
            return false;
        }
        if (continuation > bytes - index) {
            return false;
        }
        for (unsigned offset = 0U; offset < continuation; ++offset) {
            const uint8_t byte = text[index++];
            if ((byte & 0xc0U) != 0x80U) {
                return false;
            }
            value = (value << 6U) | (uint32_t)(byte & 0x3fU);
        }
        if (value < minimum || value > 0x10ffffU ||
            (value >= 0xd800U && value <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

static const p4_script_input_frame_t *player_input(
    lua_State *lua,
    p4_lua_implementation_t *implementation,
    int argument)
{
    const lua_Integer player = check_integer_range(
        lua, implementation, argument, 1, P4_SCRIPT_MAX_PLAYERS,
        "player must be an integer in 1..4");
    if (implementation->tick == NULL) {
        return NULL;
    }
    return &implementation->tick->input[(size_t)player - 1U];
}

static bool input_named_state(
    lua_State *lua,
    p4_lua_implementation_t *implementation,
    const p4_script_input_frame_t *input,
    int name_argument,
    int edge)
{
    size_t bytes = 0U;
    const char *name = lua_tolstring(lua, name_argument, &bytes);
    uint64_t bits = 0U;
    uint8_t dpad = 0U;
    if (name == NULL || bytes == 0U || bytes > 16U) {
        (void)raise_api_error(lua, implementation, "button name is invalid");
        return false;
    }
    if (input == NULL) {
        return false;
    }
    if (edge == 1) {
        bits = input->pressed;
        dpad = input->dpad_pressed;
    } else if (edge == 2) {
        bits = input->released;
        dpad = input->dpad_released;
    } else {
        bits = input->down;
        dpad = input->dpad;
    }
    struct button_name {
        const char *name;
        p4_script_button_t button;
    };
    static const struct button_name buttons[] = {
        {"a", P4_SCRIPT_BUTTON_A}, {"b", P4_SCRIPT_BUTTON_B},
        {"x", P4_SCRIPT_BUTTON_X}, {"y", P4_SCRIPT_BUTTON_Y},
        {"start", P4_SCRIPT_BUTTON_START}, {"select", P4_SCRIPT_BUTTON_SELECT},
    };
    for (size_t index = 0U; index < sizeof(buttons) / sizeof(buttons[0]); ++index) {
        if (strlen(buttons[index].name) == bytes &&
            memcmp(buttons[index].name, name, bytes) == 0) {
            return (bits & (UINT64_C(1) << buttons[index].button)) != 0U;
        }
    }
    struct dpad_name {
        const char *name;
        uint8_t bit;
    };
    static const struct dpad_name directions[] = {
        {"up", P4_SCRIPT_DPAD_UP}, {"right", P4_SCRIPT_DPAD_RIGHT},
        {"down", P4_SCRIPT_DPAD_DOWN}, {"left", P4_SCRIPT_DPAD_LEFT},
    };
    for (size_t index = 0U;
         index < sizeof(directions) / sizeof(directions[0]); ++index) {
        if (strlen(directions[index].name) == bytes &&
            memcmp(directions[index].name, name, bytes) == 0) {
            return (dpad & directions[index].bit) != 0U;
        }
    }
    (void)raise_api_error(lua, implementation, "unknown P4 button name");
    return false;
}

static int api_time_tick(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    (void)charge_host_call(lua, implementation);
    lua_pushinteger(
        lua,
        implementation->tick == NULL ? 0 :
            (lua_Integer)implementation->tick->tick);
    return 1;
}

static uint32_t random_u32(p4_lua_implementation_t *implementation)
{
    uint64_t value = implementation->random_state;
    value ^= value >> 12U;
    value ^= value << 25U;
    value ^= value >> 27U;
    implementation->random_state = value;
    return (uint32_t)((value * UINT64_C(2685821657736338717)) >> 32U);
}

static int api_random_seed(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const lua_Integer seed = check_integer_range(
        lua, implementation, 1, LUA_MININTEGER, LUA_MAXINTEGER,
        "random seed must be an integer");
    (void)charge_host_call(lua, implementation);
    implementation->random_state = (uint64_t)seed;
    if (implementation->random_state == 0U) {
        implementation->random_state = UINT64_C(0x9e3779b97f4a7c15);
    }
    return 0;
}

static int api_random_u32(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    (void)charge_host_call(lua, implementation);
    lua_pushinteger(lua, (lua_Integer)random_u32(implementation));
    return 1;
}

static int api_random_range(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const int32_t minimum = (int32_t)check_integer_range(
        lua, implementation, 1, INT32_MIN, INT32_MAX,
        "random minimum must be a signed 32-bit integer");
    const int32_t maximum = (int32_t)check_integer_range(
        lua, implementation, 2, INT32_MIN, INT32_MAX,
        "random maximum must be a signed 32-bit integer");
    uint64_t span;
    uint32_t random;
    (void)charge_host_call(lua, implementation);
    if (minimum > maximum) {
        return raise_api_error(lua, implementation, "random minimum exceeds maximum");
    }
    span = (uint64_t)((int64_t)maximum - (int64_t)minimum) + 1U;
    if (span == UINT64_C(0x100000000)) {
        random = random_u32(implementation);
    } else {
        const uint32_t bound = (uint32_t)span;
        const uint32_t threshold = (uint32_t)(0U - bound) % bound;
        do {
            random = random_u32(implementation);
        } while (random < threshold);
        random %= bound;
    }
    lua_pushinteger(lua, (lua_Integer)((int64_t)minimum + (int64_t)random));
    return 1;
}

static int api_input_connected(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const p4_script_input_frame_t *input = player_input(lua, implementation, 1);
    (void)charge_host_call(lua, implementation);
    lua_pushboolean(lua, input != NULL && input->connected != 0U);
    return 1;
}

static int input_button_result(lua_State *lua, int edge)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const p4_script_input_frame_t *input = player_input(lua, implementation, 1);
    bool active;
    (void)charge_host_call(lua, implementation);
    active = input_named_state(lua, implementation, input, 2, edge);
    lua_pushboolean(lua, active);
    return 1;
}

static int api_input_down(lua_State *lua)
{
    return input_button_result(lua, 0);
}

static int api_input_pressed(lua_State *lua)
{
    return input_button_result(lua, 1);
}

static int api_input_released(lua_State *lua)
{
    return input_button_result(lua, 2);
}

static int api_input_axis(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const p4_script_input_frame_t *input = player_input(lua, implementation, 1);
    size_t bytes = 0U;
    const char *name = lua_tolstring(lua, 2, &bytes);
    int32_t value = 0;
    (void)charge_host_call(lua, implementation);
    if (name == NULL || bytes == 0U || bytes > 20U) {
        return raise_api_error(lua, implementation, "axis name is invalid");
    }
    if (input == NULL) {
        lua_pushinteger(lua, 0);
        return 1;
    }
#define P4_AXIS_MATCH(text) (bytes == sizeof(text) - 1U && memcmp(name, text, bytes) == 0)
    if (P4_AXIS_MATCH("left-x")) value = input->left_x;
    else if (P4_AXIS_MATCH("left-y")) value = input->left_y;
    else if (P4_AXIS_MATCH("right-x")) value = input->right_x;
    else if (P4_AXIS_MATCH("right-y")) value = input->right_y;
    else if (P4_AXIS_MATCH("left-trigger")) value = (int32_t)(input->left_trigger >> 1U);
    else if (P4_AXIS_MATCH("right-trigger")) value = (int32_t)(input->right_trigger >> 1U);
    else return raise_api_error(lua, implementation, "unknown P4 axis name");
#undef P4_AXIS_MATCH
    lua_pushinteger(lua, value);
    return 1;
}

static int api_input_touch_count(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    (void)charge_host_call(lua, implementation);
    lua_pushinteger(
        lua,
        implementation->tick == NULL ? 0 :
            implementation->tick->input[0].touch_count);
    return 1;
}

static int api_input_touch(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const lua_Integer index = check_integer_range(
        lua, implementation, 1, 1, P4_SCRIPT_TOUCH_MAX_POINTS,
        "touch index must be an integer in 1..5");
    const p4_script_input_frame_t *input = implementation->tick == NULL
        ? NULL : &implementation->tick->input[0];
    (void)charge_host_call(lua, implementation);
    if (input == NULL || index > input->touch_count) {
        lua_pushinteger(lua, 0);
        lua_pushinteger(lua, 0);
        lua_pushboolean(lua, 0);
        return 3;
    }
    const p4_script_touch_point_t *point = &input->touches[(size_t)index - 1U];
    lua_pushinteger(lua, point->x);
    lua_pushinteger(lua, point->y);
    lua_pushboolean(lua, point->pressed != 0U);
    return 3;
}

static bool drawing_allowed(
    lua_State *lua, p4_lua_implementation_t *implementation)
{
    if (implementation->phase != P4_LUA_PHASE_DRAW || implementation->render == NULL) {
        (void)raise_api_error(lua, implementation, "drawing is allowed only in game.draw");
        return false;
    }
    return charge_host_call(lua, implementation);
}

static int32_t check_coordinate(
    lua_State *lua, p4_lua_implementation_t *implementation, int argument)
{
    return (int32_t)check_integer_range(
        lua, implementation, argument, INT32_MIN, INT32_MAX,
        "drawing coordinate must be a signed 32-bit integer");
}

static int api_screen_clear(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const uint16_t color = check_color(lua, implementation, 1);
    if (drawing_allowed(lua, implementation)) {
        p4_render_clear(implementation->render, color);
    }
    return 0;
}

static int api_screen_pixel(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const int32_t x = check_coordinate(lua, implementation, 1);
    const int32_t y = check_coordinate(lua, implementation, 2);
    const uint16_t color = check_color(lua, implementation, 3);
    if (drawing_allowed(lua, implementation)) {
        p4_render_rect(implementation->render, x, y, 1, 1, color);
    }
    return 0;
}

static int api_screen_line(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const int32_t x0 = check_coordinate(lua, implementation, 1);
    const int32_t y0 = check_coordinate(lua, implementation, 2);
    const int32_t x1 = check_coordinate(lua, implementation, 3);
    const int32_t y1 = check_coordinate(lua, implementation, 4);
    const uint16_t color = check_color(lua, implementation, 5);
    if (drawing_allowed(lua, implementation)) {
        p4_render_line(implementation->render, x0, y0, x1, y1, color);
    }
    return 0;
}

static int api_screen_rect(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const int32_t x = check_coordinate(lua, implementation, 1);
    const int32_t y = check_coordinate(lua, implementation, 2);
    const int32_t width = check_coordinate(lua, implementation, 3);
    const int32_t height = check_coordinate(lua, implementation, 4);
    const uint16_t color = check_color(lua, implementation, 5);
    const bool filled = lua_toboolean(lua, 6) != 0;
    if (drawing_allowed(lua, implementation)) {
        if (filled) {
            p4_render_rect(implementation->render, x, y, width, height, color);
        } else {
            p4_render_rect_outline(
                implementation->render, x, y, width, height, color);
        }
    }
    return 0;
}

static int api_screen_circle(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const int32_t x = check_coordinate(lua, implementation, 1);
    const int32_t y = check_coordinate(lua, implementation, 2);
    const int32_t radius = check_coordinate(lua, implementation, 3);
    const uint16_t color = check_color(lua, implementation, 4);
    const bool filled = lua_toboolean(lua, 5) != 0;
    if (drawing_allowed(lua, implementation)) {
        p4_render_circle(implementation->render, x, y, radius, color, filled);
    }
    return 0;
}

static int api_screen_text(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    size_t text_bytes = 0U;
    const char *text = lua_tolstring(lua, 1, &text_bytes);
    const int32_t x = check_coordinate(lua, implementation, 2);
    const int32_t y = check_coordinate(lua, implementation, 3);
    const uint16_t color = check_color(lua, implementation, 4);
    if (text == NULL || text_bytes == 0U || text_bytes > P4_SCRIPT_RENDER_MAX_TEXT_BYTES ||
        !utf8_valid((const uint8_t *)text, text_bytes)) {
        return raise_api_error(
            lua, implementation, "screen text must be 1..96 valid UTF-8 bytes");
    }
    if (drawing_allowed(lua, implementation)) {
        p4_render_text(
            implementation->render,
            x,
            y,
            (const uint8_t *)text,
            (uint32_t)text_bytes,
            color);
    }
    return 0;
}

static int api_screen_sprite(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const p4_script_asset_id_t asset = (p4_script_asset_id_t)check_integer_range(
        lua, implementation, 1, 1, 4095,
        "sprite asset ID must be an integer in 1..4095");
    const int32_t x = check_coordinate(lua, implementation, 2);
    const int32_t y = check_coordinate(lua, implementation, 3);
    const uint32_t frame = (uint32_t)check_integer_range(
        lua, implementation, 4, 0, UINT32_MAX,
        "sprite frame must be a nonnegative 32-bit integer");
    const uint8_t flags = (uint8_t)check_integer_range(
        lua, implementation, 5, 0,
        P4_SCRIPT_RENDER_FLAG_SPRITE_FLIP_X | P4_SCRIPT_RENDER_FLAG_SPRITE_FLIP_Y,
        "sprite flags are unsupported");
    if (drawing_allowed(lua, implementation)) {
        p4_render_sprite(implementation->render, asset, x, y, frame, flags);
    }
    return 0;
}

static p4_lua_waveform_t check_waveform(
    lua_State *lua, p4_lua_implementation_t *implementation, int argument)
{
    size_t bytes = 0U;
    const char *name = lua_tolstring(lua, argument, &bytes);
    if (name == NULL) {
        (void)raise_api_error(lua, implementation, "waveform must be text");
        return P4_LUA_WAVE_SQUARE;
    }
#define P4_WAVE_MATCH(text) (bytes == sizeof(text) - 1U && memcmp(name, text, bytes) == 0)
    if (P4_WAVE_MATCH("square")) return P4_LUA_WAVE_SQUARE;
    if (P4_WAVE_MATCH("triangle")) return P4_LUA_WAVE_TRIANGLE;
    if (P4_WAVE_MATCH("saw")) return P4_LUA_WAVE_SAW;
    if (P4_WAVE_MATCH("noise")) return P4_LUA_WAVE_NOISE;
#undef P4_WAVE_MATCH
    (void)raise_api_error(lua, implementation, "unknown waveform");
    return P4_LUA_WAVE_SQUARE;
}

static int api_audio_tone(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const uint8_t channel = (uint8_t)check_integer_range(
        lua, implementation, 1, 1, 4, "tone channel must be in 1..4");
    const uint16_t frequency = (uint16_t)check_integer_range(
        lua, implementation, 2, 40, 4000, "tone frequency must be in 40..4000 Hz");
    const uint16_t duration = (uint16_t)check_integer_range(
        lua, implementation, 3, 1, 600, "tone duration must be in 1..600 ticks");
    const uint8_t volume = (uint8_t)check_integer_range(
        lua, implementation, 4, 0, 255, "tone volume must be in 0..255");
    const p4_lua_waveform_t waveform = check_waveform(lua, implementation, 5);
    bool accepted = false;
    (void)charge_host_call(lua, implementation);
    if (implementation->config.services.play_tone != NULL) {
        accepted = implementation->config.services.play_tone(
            implementation->config.services.context,
            channel,
            frequency,
            duration,
            volume,
            waveform);
    }
    lua_pushboolean(lua, accepted);
    return 1;
}

static int api_audio_stop(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const uint8_t channel = (uint8_t)check_integer_range(
        lua, implementation, 1, 1, 4, "tone channel must be in 1..4");
    (void)charge_host_call(lua, implementation);
    if (implementation->config.services.stop_tone != NULL) {
        implementation->config.services.stop_tone(
            implementation->config.services.context, channel);
    }
    return 0;
}

static int api_audio_stop_all(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    (void)charge_host_call(lua, implementation);
    if (implementation->config.services.stop_all_audio != NULL) {
        implementation->config.services.stop_all_audio(
            implementation->config.services.context);
    }
    return 0;
}

static bool save_key_valid(const char *key, size_t bytes)
{
    if (key == NULL || bytes == 0U || bytes > P4_LUA_SAVE_KEY_MAX_BYTES ||
        key[0] < 'a' || key[0] > 'z') {
        return false;
    }
    for (size_t index = 1U; index < bytes; ++index) {
        const char character = key[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '.' || character == '-')) {
            return false;
        }
    }
    return true;
}

static const char *check_save_key(
    lua_State *lua,
    p4_lua_implementation_t *implementation,
    int argument)
{
    size_t bytes = 0U;
    const char *key = lua_tolstring(lua, argument, &bytes);
    if (!save_key_valid(key, bytes)) {
        (void)raise_api_error(lua, implementation, "save key is invalid");
        return "invalid";
    }
    return key;
}

static int api_save_get(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const char *key = check_save_key(lua, implementation, 1);
    uint8_t value[P4_LUA_SAVE_VALUE_MAX_BYTES];
    size_t value_bytes = 0U;
    bool found = false;
    (void)charge_host_call(lua, implementation);
    if (implementation->config.services.save_get != NULL) {
        found = implementation->config.services.save_get(
            implementation->config.services.context,
            key,
            value,
            sizeof(value),
            &value_bytes);
    }
    if (!found) {
        lua_pushnil(lua);
        return 1;
    }
    if (value_bytes > sizeof(value) || !utf8_valid(value, value_bytes)) {
        return raise_api_error(lua, implementation, "save service returned invalid UTF-8");
    }
    lua_pushlstring(lua, (const char *)value, value_bytes);
    return 1;
}

static int api_save_set(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const char *key = check_save_key(lua, implementation, 1);
    size_t value_bytes = 0U;
    const char *value = lua_tolstring(lua, 2, &value_bytes);
    bool accepted = false;
    if (value == NULL || value_bytes > P4_LUA_SAVE_VALUE_MAX_BYTES ||
        !utf8_valid((const uint8_t *)value, value_bytes)) {
        return raise_api_error(
            lua, implementation, "save value must be at most 256 valid UTF-8 bytes");
    }
    (void)charge_host_call(lua, implementation);
    if (implementation->config.services.save_set != NULL) {
        accepted = implementation->config.services.save_set(
            implementation->config.services.context,
            key,
            (const uint8_t *)value,
            value_bytes);
    }
    lua_pushboolean(lua, accepted);
    return 1;
}

static int api_save_remove(lua_State *lua)
{
    p4_lua_implementation_t *implementation = implementation_from_lua(lua);
    const char *key = check_save_key(lua, implementation, 1);
    (void)charge_host_call(lua, implementation);
    if (implementation->config.services.save_remove != NULL) {
        (void)implementation->config.services.save_remove(
            implementation->config.services.context, key);
    }
    return 0;
}

static void add_module(
    lua_State *lua,
    int parent_index,
    const char *name,
    const p4_lua_function_t *functions,
    size_t function_count,
    p4_lua_implementation_t *implementation)
{
    parent_index = lua_absindex(lua, parent_index);
    lua_newtable(lua);
    const int backing = lua_gettop(lua);
    for (size_t index = 0U; index < function_count; ++index) {
        set_function(
            lua,
            backing,
            functions[index].name,
            functions[index].function,
            implementation);
    }
    push_readonly_proxy(lua, backing);
    lua_setfield(lua, parent_index, name);
    lua_pop(lua, 1);
}

static bool install_p4_api(p4_lua_implementation_t *implementation)
{
    lua_State *lua = implementation->lua;
    static const p4_lua_function_t time_functions[] = {
        {"tick", api_time_tick},
    };
    static const p4_lua_function_t random_functions[] = {
        {"seed", api_random_seed}, {"u32", api_random_u32},
        {"range", api_random_range},
    };
    static const p4_lua_function_t input_functions[] = {
        {"connected", api_input_connected}, {"down", api_input_down},
        {"pressed", api_input_pressed}, {"released", api_input_released},
        {"axis", api_input_axis}, {"touch_count", api_input_touch_count},
        {"touch", api_input_touch},
    };
    static const p4_lua_function_t screen_functions[] = {
        {"clear", api_screen_clear}, {"pixel", api_screen_pixel},
        {"line", api_screen_line}, {"rect", api_screen_rect},
        {"circle", api_screen_circle}, {"text", api_screen_text},
        {"sprite", api_screen_sprite},
    };
    static const p4_lua_function_t audio_functions[] = {
        {"tone", api_audio_tone}, {"stop", api_audio_stop},
        {"stop_all", api_audio_stop_all},
    };
    static const p4_lua_function_t save_functions[] = {
        {"get", api_save_get}, {"set", api_save_set},
        {"remove", api_save_remove},
    };

    lua_newtable(lua);
    const int p4_backing = lua_gettop(lua);
    add_module(lua, p4_backing, "time", time_functions,
               sizeof(time_functions) / sizeof(time_functions[0]), implementation);
    add_module(lua, p4_backing, "random", random_functions,
               sizeof(random_functions) / sizeof(random_functions[0]), implementation);
    add_module(lua, p4_backing, "input", input_functions,
               sizeof(input_functions) / sizeof(input_functions[0]), implementation);
    add_module(lua, p4_backing, "screen", screen_functions,
               sizeof(screen_functions) / sizeof(screen_functions[0]), implementation);
    add_module(lua, p4_backing, "audio", audio_functions,
               sizeof(audio_functions) / sizeof(audio_functions[0]), implementation);
    add_module(lua, p4_backing, "save", save_functions,
               sizeof(save_functions) / sizeof(save_functions[0]), implementation);

    lua_pushvalue(lua, p4_backing);
    implementation->p4_backing_reference = luaL_ref(lua, LUA_REGISTRYINDEX);
    push_readonly_proxy(lua, p4_backing);
    lua_setglobal(lua, "p4");
    lua_pop(lua, 1);
    return implementation->p4_backing_reference != LUA_NOREF;
}

static p4_script_status_t install_arcade_helpers(p4_lua_implementation_t *implementation)
{
    lua_State *lua = implementation->lua;
    int result = luaL_loadbufferx(
        lua,
        (const char *)P4_ARCADE_LUA_SOURCE,
        P4_ARCADE_LUA_SOURCE_BYTES,
        "@p4.arcade",
        "t");
    if (result != LUA_OK) {
        return callback_status(implementation, result);
    }
    p4_script_status_t status = protected_call(
        implementation, 0, 1, P4_LUA_PHASE_LOAD);
    if (status != P4_SCRIPT_STATUS_OK) {
        return status;
    }
    if (!lua_istable(lua, -1)) {
        lua_pop(lua, 1);
        set_error(implementation->owner, "p4.arcade helper did not return a table");
        return P4_SCRIPT_STATUS_BAD_FORMAT;
    }
    const int arcade_backing = lua_gettop(lua);
    lua_getfield(lua, arcade_backing, "version");
    const bool version_valid = lua_isinteger(lua, -1) && lua_tointeger(lua, -1) == 1;
    lua_pop(lua, 1);
    if (!version_valid) {
        lua_pop(lua, 1);
        set_error(implementation->owner, "p4.arcade helper version is invalid");
        return P4_SCRIPT_STATUS_BAD_FORMAT;
    }
    lua_rawgeti(lua, LUA_REGISTRYINDEX, implementation->p4_backing_reference);
    const int p4_backing = lua_gettop(lua);
    push_readonly_proxy(lua, arcade_backing);
    lua_setfield(lua, p4_backing, "arcade");
    lua_pop(lua, 2);
    (void)P4_ARCADE_LUA_SOURCE_SHA256;
    return P4_SCRIPT_STATUS_OK;
}

static bool source_valid(const uint8_t *source, size_t source_bytes)
{
    return source != NULL && source_bytes != 0U &&
        source_bytes <= P4_LUA_SOURCE_MAX_BYTES && source[0] != 0x1bU &&
        memchr(source, 0, source_bytes) == NULL && utf8_valid(source, source_bytes);
}

static p4_script_status_t call_game_function(
    p4_lua_implementation_t *implementation,
    const char *name,
    bool optional,
    p4_lua_phase_t phase)
{
    lua_State *lua = implementation->lua;
    lua_rawgeti(lua, LUA_REGISTRYINDEX, implementation->game_reference);
    lua_getfield(lua, -1, name);
    lua_remove(lua, -2);
    if (lua_isnil(lua, -1) && optional) {
        lua_pop(lua, 1);
        return P4_SCRIPT_STATUS_OK;
    }
    if (!lua_isfunction(lua, -1)) {
        lua_pop(lua, 1);
        set_error(implementation->owner, "P4 game callback is missing or not a function");
        return P4_SCRIPT_STATUS_BAD_FORMAT;
    }
    return protected_call(implementation, 0, 0, phase);
}

void p4_lua_config_default(p4_lua_config_t *config_out)
{
    if (config_out == NULL) {
        return;
    }
    memset(config_out, 0, sizeof(*config_out));
    config_out->heap_limit_bytes = P4_LUA_HEAP_MIN_BYTES;
    config_out->callback_instruction_limit = P4_LUA_CALLBACK_INSTRUCTION_MAX;
    config_out->random_seed = UINT64_C(0x9e3779b97f4a7c15);
}

static bool config_valid(const p4_lua_config_t *config)
{
    return config != NULL &&
        config->heap_limit_bytes >= P4_LUA_HEAP_MIN_BYTES &&
        config->heap_limit_bytes <= P4_LUA_HEAP_MAX_BYTES &&
        (config->heap_limit_bytes % 4096U) == 0U &&
        config->callback_instruction_limit >= P4_LUA_HOOK_INTERVAL &&
        config->callback_instruction_limit <= P4_LUA_CALLBACK_INSTRUCTION_MAX;
}

p4_script_status_t p4_lua_runtime_load(
    p4_lua_runtime_t *runtime,
    const p4_lua_config_t *config,
    const uint8_t *source,
    size_t source_bytes,
    const char *source_name)
{
    p4_lua_implementation_t *implementation;
    p4_script_status_t status;
    int result;

    if (runtime == NULL || runtime->implementation != NULL ||
        !config_valid(config) || !source_valid(source, source_bytes)) {
        return P4_SCRIPT_STATUS_INVALID_ARGUMENT;
    }
    memset(&runtime->final_stats, 0, sizeof(runtime->final_stats));
    set_error(runtime, NULL);
    implementation = calloc(1U, sizeof(*implementation));
    if (implementation == NULL) {
        set_error(runtime, "cannot allocate Lua runtime state");
        return P4_SCRIPT_STATUS_OUT_OF_MEMORY;
    }
    implementation->owner = runtime;
    implementation->config = *config;
    implementation->game_reference = P4_LUA_REGISTRY_NONE;
    implementation->p4_backing_reference = P4_LUA_REGISTRY_NONE;
    implementation->random_state = config->random_seed == 0U
        ? UINT64_C(0x9e3779b97f4a7c15) : config->random_seed;
    atomic_init(&implementation->interrupt_requested, 0U);
    runtime->implementation = implementation;

    implementation->lua = lua_newstate(counting_allocator, implementation);
    if (implementation->lua == NULL) {
        set_error(runtime, "Lua state exceeds configured heap or allocator failed");
        runtime->implementation = NULL;
        free(implementation);
        return P4_SCRIPT_STATUS_OUT_OF_MEMORY;
    }
    *(p4_lua_implementation_t **)lua_getextraspace(implementation->lua) = implementation;
    if (!open_safe_libraries(implementation->lua) || !install_p4_api(implementation)) {
        set_error(runtime, "cannot construct the P4 Lua allowlist");
        (void)p4_lua_runtime_unload(runtime);
        return P4_SCRIPT_STATUS_OUT_OF_MEMORY;
    }
    status = install_arcade_helpers(implementation);
    if (status != P4_SCRIPT_STATUS_OK) {
        (void)p4_lua_runtime_unload(runtime);
        return status;
    }

    const char *chunk_name = source_name == NULL ? "@main.lua" : source_name;
    result = luaL_loadbufferx(
        implementation->lua,
        (const char *)source,
        source_bytes,
        chunk_name,
        "t");
    if (result != LUA_OK) {
        status = callback_status(implementation, result);
        (void)p4_lua_runtime_unload(runtime);
        return status;
    }
    status = protected_call(implementation, 0, 1, P4_LUA_PHASE_LOAD);
    if (status != P4_SCRIPT_STATUS_OK) {
        (void)p4_lua_runtime_unload(runtime);
        return status;
    }
    if (!lua_istable(implementation->lua, -1)) {
        lua_pop(implementation->lua, 1);
        set_error(runtime, "main.lua must return one game table");
        (void)p4_lua_runtime_unload(runtime);
        return P4_SCRIPT_STATUS_BAD_FORMAT;
    }
    implementation->game_reference =
        luaL_ref(implementation->lua, LUA_REGISTRYINDEX);
    static const char *const required[] = {"start", "update", "draw"};
    for (size_t index = 0U; index < sizeof(required) / sizeof(required[0]); ++index) {
        lua_rawgeti(
            implementation->lua, LUA_REGISTRYINDEX, implementation->game_reference);
        lua_getfield(implementation->lua, -1, required[index]);
        const bool valid = lua_isfunction(implementation->lua, -1);
        lua_pop(implementation->lua, 2);
        if (!valid) {
            set_error(runtime, "game table is missing a required callback");
            (void)p4_lua_runtime_unload(runtime);
            return P4_SCRIPT_STATUS_BAD_FORMAT;
        }
    }
    implementation->loaded = true;
    status = call_game_function(
        implementation, "start", false, P4_LUA_PHASE_START);
    if (status != P4_SCRIPT_STATUS_OK) {
        (void)p4_lua_runtime_unload(runtime);
        return status;
    }
    implementation->stats.last_status = P4_SCRIPT_STATUS_OK;
    return P4_SCRIPT_STATUS_OK;
}

static bool tick_valid(const p4_script_tick_frame_t *tick)
{
    if (tick == NULL || tick->version != P4_SCRIPT_GAME_API_VERSION ||
        tick->size != sizeof(*tick) || tick->generation == 0U ||
        tick->dt_numerator != 1U || tick->dt_denominator != P4_SCRIPT_TICK_HZ) {
        return false;
    }
    for (size_t player = 0U; player < P4_SCRIPT_MAX_PLAYERS; ++player) {
        if (tick->input[player].version != P4_SCRIPT_GAME_API_VERSION ||
            tick->input[player].size != sizeof(tick->input[player])) {
            return false;
        }
    }
    return true;
}

p4_script_status_t p4_lua_runtime_tick(
    p4_lua_runtime_t *runtime,
    const p4_script_tick_frame_t *tick,
    p4_render_writer_t *render)
{
    if (runtime == NULL || runtime->implementation == NULL ||
        !tick_valid(tick) || render == NULL) {
        return P4_SCRIPT_STATUS_INVALID_ARGUMENT;
    }
    p4_lua_implementation_t *implementation = runtime->implementation;
    if (!implementation->loaded) {
        return P4_SCRIPT_STATUS_INVALID_STATE;
    }
    if (implementation->saw_tick && tick->tick != implementation->previous_tick + 1U) {
        set_error(runtime, "logical tick is not monotonic");
        return P4_SCRIPT_STATUS_INVALID_STATE;
    }
    implementation->tick = tick;
    p4_script_status_t status = call_game_function(
        implementation, "update", false, P4_LUA_PHASE_UPDATE);
    if (status == P4_SCRIPT_STATUS_OK && (tick->tick & 1U) == 0U) {
        implementation->render = render;
        status = call_game_function(
            implementation, "draw", false, P4_LUA_PHASE_DRAW);
        implementation->stats.draw_calls++;
    }
    implementation->stats.update_calls++;
    implementation->previous_tick = tick->tick;
    implementation->saw_tick = true;
    implementation->tick = NULL;
    implementation->render = NULL;
    implementation->stats.heap_bytes = implementation->heap_bytes;
    implementation->stats.peak_heap_bytes = implementation->peak_heap_bytes;
    implementation->stats.last_status = status;
    return status;
}

void p4_lua_runtime_request_interrupt(p4_lua_runtime_t *runtime)
{
    if (runtime != NULL && runtime->implementation != NULL) {
        p4_lua_implementation_t *implementation = runtime->implementation;
        atomic_store_explicit(
            &implementation->interrupt_requested, 1U, memory_order_relaxed);
    }
}

p4_script_status_t p4_lua_runtime_unload(p4_lua_runtime_t *runtime)
{
    p4_script_status_t status = P4_SCRIPT_STATUS_OK;
    if (runtime == NULL) {
        return P4_SCRIPT_STATUS_INVALID_ARGUMENT;
    }
    if (runtime->implementation == NULL) {
        return P4_SCRIPT_STATUS_OK;
    }
    p4_lua_implementation_t *implementation = runtime->implementation;
    if (implementation->loaded && implementation->lua != NULL &&
        implementation->game_reference != LUA_NOREF) {
        status = call_game_function(
            implementation, "stop", true, P4_LUA_PHASE_STOP);
    }
    if (implementation->config.services.stop_all_audio != NULL) {
        implementation->config.services.stop_all_audio(
            implementation->config.services.context);
    }
    implementation->stats.heap_bytes = implementation->heap_bytes;
    implementation->stats.peak_heap_bytes = implementation->peak_heap_bytes;
    runtime->final_stats = implementation->stats;
    if (implementation->lua != NULL) {
        lua_close(implementation->lua);
        implementation->lua = NULL;
    }
    runtime->implementation = NULL;
    free(implementation);
    return status;
}

bool p4_lua_runtime_loaded(const p4_lua_runtime_t *runtime)
{
    if (runtime == NULL || runtime->implementation == NULL) {
        return false;
    }
    const p4_lua_implementation_t *implementation = runtime->implementation;
    return implementation->loaded;
}

void p4_lua_runtime_get_stats(
    const p4_lua_runtime_t *runtime, p4_lua_stats_t *stats_out)
{
    if (stats_out == NULL) {
        return;
    }
    memset(stats_out, 0, sizeof(*stats_out));
    if (runtime == NULL) {
        return;
    }
    if (runtime->implementation == NULL) {
        *stats_out = runtime->final_stats;
    } else {
        const p4_lua_implementation_t *implementation = runtime->implementation;
        *stats_out = implementation->stats;
        stats_out->heap_bytes = implementation->heap_bytes;
        stats_out->peak_heap_bytes = implementation->peak_heap_bytes;
    }
}

const char *p4_lua_runtime_last_error(const p4_lua_runtime_t *runtime)
{
    return runtime == NULL ? "invalid Lua runtime" : runtime->last_error;
}
