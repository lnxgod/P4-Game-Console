#ifndef P4_LUA_RUNTIME_H
#define P4_LUA_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/runtime_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P4_LUA_RUNTIME_VERSION UINT32_C(1)
#define P4_LUA_HEAP_MIN_BYTES (64U * 1024U)
#define P4_LUA_HEAP_MAX_BYTES (512U * 1024U)
#define P4_LUA_SOURCE_MAX_BYTES (256U * 1024U)
#define P4_LUA_CALLBACK_INSTRUCTION_MAX UINT32_C(200000)
#define P4_LUA_CALLBACK_HOST_CALL_MAX UINT32_C(256)
#define P4_LUA_SAVE_KEY_MAX_BYTES UINT32_C(32)
#define P4_LUA_SAVE_VALUE_MAX_BYTES UINT32_C(256)
#define P4_LUA_ERROR_TEXT_BYTES UINT32_C(192)

typedef enum {
    P4_LUA_WAVE_SQUARE = 0,
    P4_LUA_WAVE_TRIANGLE = 1,
    P4_LUA_WAVE_SAW = 2,
    P4_LUA_WAVE_NOISE = 3,
} p4_lua_waveform_t;

typedef void *(*p4_lua_reallocate_fn)(
    void *context,
    void *pointer,
    size_t old_size,
    size_t new_size);

typedef bool (*p4_lua_play_tone_fn)(
    void *context,
    uint8_t channel,
    uint16_t frequency_hz,
    uint16_t duration_ticks,
    uint8_t volume,
    p4_lua_waveform_t waveform);

typedef void (*p4_lua_stop_tone_fn)(void *context, uint8_t channel);
typedef void (*p4_lua_stop_all_audio_fn)(void *context);

typedef bool (*p4_lua_save_get_fn)(
    void *context,
    const char *key,
    uint8_t *value_out,
    size_t value_capacity,
    size_t *value_bytes_out);

typedef bool (*p4_lua_save_set_fn)(
    void *context,
    const char *key,
    const uint8_t *value,
    size_t value_bytes);

typedef bool (*p4_lua_save_remove_fn)(void *context, const char *key);

typedef struct {
    void *context;
    p4_lua_reallocate_fn reallocate;
    p4_lua_play_tone_fn play_tone;
    p4_lua_stop_tone_fn stop_tone;
    p4_lua_stop_all_audio_fn stop_all_audio;
    p4_lua_save_get_fn save_get;
    p4_lua_save_set_fn save_set;
    p4_lua_save_remove_fn save_remove;
} p4_lua_services_t;

typedef struct {
    size_t heap_limit_bytes;
    uint32_t callback_instruction_limit;
    uint64_t random_seed;
    p4_lua_services_t services;
} p4_lua_config_t;

typedef struct {
    size_t heap_bytes;
    size_t peak_heap_bytes;
    uint64_t callbacks_completed;
    uint64_t instructions_charged;
    uint64_t host_calls;
    uint32_t update_calls;
    uint32_t draw_calls;
    uint32_t instruction_faults;
    uint32_t memory_faults;
    uint32_t api_faults;
    p4_script_status_t last_status;
} p4_lua_stats_t;

typedef struct {
    void *implementation;
    p4_lua_stats_t final_stats;
    char last_error[P4_LUA_ERROR_TEXT_BYTES];
} p4_lua_runtime_t;

void p4_lua_config_default(p4_lua_config_t *config_out);

p4_script_status_t p4_lua_runtime_load(
    p4_lua_runtime_t *runtime,
    const p4_lua_config_t *config,
    const uint8_t *source,
    size_t source_bytes,
    const char *source_name);

p4_script_status_t p4_lua_runtime_tick(
    p4_lua_runtime_t *runtime,
    const p4_script_tick_frame_t *tick,
    p4_render_writer_t *render);

void p4_lua_runtime_request_interrupt(p4_lua_runtime_t *runtime);
p4_script_status_t p4_lua_runtime_unload(p4_lua_runtime_t *runtime);
bool p4_lua_runtime_loaded(const p4_lua_runtime_t *runtime);
void p4_lua_runtime_get_stats(
    const p4_lua_runtime_t *runtime,
    p4_lua_stats_t *stats_out);
const char *p4_lua_runtime_last_error(const p4_lua_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
