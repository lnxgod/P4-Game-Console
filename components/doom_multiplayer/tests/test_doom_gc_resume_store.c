#include "doom_p4mp_adapter.h"
bool fixture_store_ok=true;
unsigned fixture_store_calls;
bool p4_doom_arena_resume_store(const p4_doom_mp_launch_config_t *config,const uint8_t ticket[16])
{ (void)config;(void)ticket;++fixture_store_calls;return fixture_store_ok; }
