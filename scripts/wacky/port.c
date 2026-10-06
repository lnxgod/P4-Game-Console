/* SPDX-License-Identifier: MIT
 * Shareware port: Console OS owns hardware, audio output and networking. */
#include "p4/game.h"
#include "p4/input.h"
#include "p4/draw.h"
#include "ww_race.h"
#include "port_support.h"
#include "network.h"
#include "sound.h"
#include "front.h"
#include "controls.h"
#include "ww_victory.h"
#include "ww_music.h"
#include "ww_intro.h"
#include <string.h>

typedef struct { uint16_t x, y, heading; } Pose;
typedef struct {
    WwArchive archive;
    WwArchiveEntry entries[298];
    WwRace race;
    WwNet network;
    WwRenderer renderer;
    WwDisplay display;
    WwFront front;
    WwSoundPlayer sound;
    WwVictory victory;
    WwIntro intro;
    WwAudio intro_audio;
    uint32_t ui_ticks;
    uint32_t held_buttons;
    bool save_dirty;
    p4_game_save_ticket_t save_ticket;
    uint32_t save_sequence;
    bool racing, results;
    unsigned timer_units;
    bool paused, dirty, advance_pending;
    Pose previous_camera, previous_racers[WW_RACER_COUNT];
    uint16_t previous_horizon;
    /* Rendering in this recreation mutates animation state. Extra draws use
       this bounded snapshot so they cannot accelerate gameplay/animations. */
    uint8_t render_save[sizeof(WwRace) - offsetof(WwRace, initial)];
} Probe;
_Static_assert(sizeof(Probe) <= P4_GAME_MAX_STATE_BYTES, "P4 state budget");

static Pose camera_pose(const WwRace *race)
{
    return (Pose){race->player.camera_x, race->player.camera_y, race->player.heading};
}
static Pose racer_pose(const WwRacerState *racer)
{
    return (Pose){racer->world_x, racer->world_y, racer->heading};
}
static void remember_pose(Probe *s)
{
    s->previous_camera = camera_pose(&s->race);
    s->previous_horizon = s->race.horizon_source_offset;
    for (size_t i = 0; i < WW_RACER_COUNT; ++i)
        s->previous_racers[i] = racer_pose(&s->race.racers[i]);
}
static uint16_t blend_position(uint16_t a, uint16_t b, unsigned alpha)
{
    int d = (int)b - a;
    /* Respawn/teleport transitions snap instead of crossing the whole map. */
    if (d > 128 || d < -128) return b;
    return (uint16_t)((int)a + d * (int)alpha / 12000);
}
static uint16_t blend_heading(uint16_t a, uint16_t b, unsigned alpha)
{
    int d = (int)b - a;
    if (d > 960) d -= 1920;
    if (d < -960) d += 1920;
    int h = (int)a + d * (int)alpha / 12000;
    return (uint16_t)((h + 1920) % 1920);
}
static bool bind_archive(Probe *s, const p4_game_services_t *svc)
{
    if (!svc || !(svc->available_capabilities & P4_GAME_CAP_STORAGE) ||
        svc->resource_format_version != 1 || svc->resource_bytes != 4398948 ||
        !svc->resource_data) return false;
    const uint8_t *bytes = svc->resource_data;
    if (ww_read_le16(bytes) != 298) return false;
    s->archive.bytes = (uint8_t *)bytes; /* upstream view is read-only */
    s->archive.size = svc->resource_bytes;
    s->archive.entries = s->entries;
    s->archive.entry_count = 298;
    uint32_t previous = 2 + 298 * 22;
    for (unsigned i = 0; i < 298; ++i) {
        const uint8_t *r = bytes + 2 + i * 22;
        WwArchiveEntry *e = &s->entries[i];
        memcpy(e->name, r, 14); e->name[14] = 0;
        e->stored_size = ww_read_le32(r + 14); e->offset = ww_read_le32(r + 18);
        if (!e->name[0] || !e->stored_size || e->offset < 2 + 298 * 22 ||
            (i && e->offset != previous) ||
            (uint64_t)e->offset + 2 + e->stored_size > svc->resource_bytes) return false;
        previous = e->offset + e->stored_size;
        e->prefix = ww_read_le16(bytes + e->offset);
    }
    return true;
}
static void save_settings(Probe *s,p4_game_context_t *ctx)
{
    s->save_dirty=true;
    if(s->save_ticket)return;
    uint8_t b[36]={'W','W','S',2,s->front.vehicle,s->front.track,s->front.laps,
        s->front.engine,s->front.race_class,s->front.music,s->front.effects,
        s->front.motor,s->front.duck_best,s->front.map,s->front.clock,s->front.speedometer};
    for(unsigned i=0;i<5;++i)for(unsigned j=0;j<4;++j)b[16+i*4+j]=(uint8_t)(s->front.best_time[i]>>(j*8));
    if(p4_game_queue_save(ctx,"AUTO",2,s->save_sequence,b,sizeof(b),&s->save_ticket))s->save_dirty=false;
}
static void set_volume(Probe *s)
{
    s->sound.music_volume=s->front.music;s->sound.effects_volume=s->front.effects;
    s->sound.engine_volume=s->front.motor;
}
static void stop(p4_game_context_t *ctx)
{
    Probe *s=ctx->state;
    p4_game_stop_audio(ctx);ww_sound_silence(&s->sound);
    ww_intro_close(&s->intro);ww_victory_close(&s->victory);ww_front_close(&s->front);ww_race_close(&s->race);
    memset(s,0,sizeof(*s));
}
static bool home(Probe *s)
{
    ww_intro_close(&s->intro);ww_race_close(&s->race);ww_victory_close(&s->victory);ww_sound_silence(&s->sound);
    s->racing=false;s->results=false;s->paused=false;s->front.screen=WW_HOME;s->front.row=0;
    s->dirty=true;
    ww_sound_music(&s->sound,&s->archive,"MAINMENU.MID");
    return ww_front_open(&s->front,&s->archive);
}
static bool launch(Probe *s)
{
    static const unsigned laps[]={3,6,8,10};
    WwRaceSelection selection;
    unsigned track=s->front.mode==WW_DUCK?7u:(unsigned)s->front.track+1;
    ww_intro_close(&s->intro);ww_front_close(&s->front);ww_victory_close(&s->victory);ww_race_close(&s->race);
    ww_sound_silence(&s->sound);
    if(!ww_race_selection_init(&selection,false,(track-1)/5,(track-1)%5,s->front.engine))return false;
    bool ok=s->front.mode==WW_DUCK?
        ww_race_open_duck(&s->race,&s->archive,&selection,s->front.vehicle,s->front.speedometer,s->front.map):
        ww_race_open(&s->race,&s->archive,&selection,s->front.vehicle,s->front.race_class==4?6:s->front.race_class==5?3:laps[s->front.laps],s->front.race_class,
            s->front.clock,s->front.speedometer,s->front.map);
    if(!ok)return false;
    if(s->front.race_class==4&&s->front.mode!=WW_DUCK)for(unsigned i=1;i<WW_RACER_COUNT;++i)s->race.racers[i].active=false;
    char music[16];
    if(ww_music_race_asset(false,selection.board_page,selection.race_index%3,false,music,sizeof(music)))
        ww_sound_music(&s->sound,&s->archive,music);
    s->racing=true;s->results=false;s->paused=false;s->timer_units=0;
    remember_pose(s);s->dirty=true;s->advance_pending=true;return true;
}
static bool intro(Probe *s)
{
    ww_front_close(&s->front);s->intro_audio=(WwAudio){.player=&s->sound,.music_volume=s->front.music};s->ui_ticks=0;
    return ww_intro_open(&s->intro,&s->archive,&s->display,&s->intro_audio,&s->renderer,s->front.music);
}
static bool start(p4_game_context_t *ctx)
{
    Probe *s=ctx->state;memset(s,0,sizeof(*s));
    if(!bind_archive(s,ctx->services))return false;
    s->front=(WwFront){.engine=1,.race_class=1,.music=7,.effects=8,.motor=4,.map=true,.clock=true,.speedometer=true};
    const p4_game_services_t *svc=ctx->services;
    if(svc->save_schema_version==2&&svc->save_bytes==36&&svc->save_data) {
        const uint8_t *b=svc->save_data;
        if(!memcmp(b,"WWS\2",4)&&b[4]<8&&b[5]<5&&b[6]<4&&b[7]>=1&&b[7]<=2&&
           b[8]>=1&&b[8]<=5&&b[9]<=10&&b[10]<=10&&b[11]<=10&&b[12]<=99&&b[13]<=1&&b[14]<=1&&b[15]<=1) {
            s->front.vehicle=b[4];s->front.track=b[5];s->front.laps=b[6];s->front.engine=b[7];
            s->front.race_class=b[8];s->front.music=b[9];s->front.effects=b[10];s->front.motor=b[11];
            s->front.duck_best=b[12];s->front.map=b[13]!=0;s->front.clock=b[14]!=0;s->front.speedometer=b[15]!=0;
            for(unsigned i=0;i<5;++i)s->front.best_time[i]=ww_read_le32(b+16+i*4);
        }
        s->save_sequence=svc->save_sequence;
    }
    s->sound.enabled=(svc->available_capabilities&P4_GAME_CAP_AUDIO_STREAM)!=0;set_volume(s);
    ww_renderer_init(&s->renderer);
    if(!ww_renderer_load_assets(&s->renderer,&s->archive)){stop(ctx);return false;}
    p4_game_multiplayer_status_t st={0};
    if(svc->available_capabilities&P4_GAME_CAP_MULTIPLAYER_SESSION) {
        if(!p4_game_multiplayer_read_status(ctx,&st)){stop(ctx);return false;}
        if(st.state!=P4_GAME_MULTIPLAYER_OFFLINE) {
            s->front.track=(uint8_t)((uint32_t)(st.session_seed^(st.session_seed>>32))%5u);s->front.laps=0;s->front.race_class=1;s->front.engine=1;s->front.vehicle=st.local_player_slot;
            if(!launch(s)||!ww_net_start(&s->network,ctx,&s->race)||
               !ww_net_view(&s->network,&s->race,&s->renderer)){stop(ctx);return false;}
            remember_pose(s);return true;
        }
    }
    if(!home(s)||!intro(s)){stop(ctx);return false;}
    return true;
}
static p4_game_result_t update(p4_game_context_t *ctx,
    const p4_game_input_t *input, uint32_t ms)
{
    Probe *s = ctx->state;
    s->held_buttons = input->held;
    if (input->pressed & P4_BUTTON_BACK) {save_settings(s,ctx);return P4_GAME_EXIT_TO_LAUNCHER;}
    if (ms > P4_GAME_MAX_FRAME_DELTA_MS) ms = P4_GAME_MAX_FRAME_DELTA_MS;
    if(s->save_ticket) {
        p4_game_save_status_t status;uint32_t sequence;
        if(p4_game_read_save_status(ctx,s->save_ticket,&status,&sequence)&&status!=P4_GAME_SAVE_QUEUED) {
            if(status==P4_GAME_SAVE_COMMITTED)s->save_sequence=sequence;
            s->save_ticket=0;
        }
    }
    if(s->save_dirty&&!s->save_ticket)save_settings(s,ctx);
    set_volume(s);
    ww_sound_engine(&s->sound,&s->archive,s->race.player.speed_index,s->racing&&!s->paused&&!s->results);
    ww_sound_update(&s->sound,ctx,ms);
    if(s->intro.open) {
        WwInput keys={0};keys.pressed[WW_SCAN_ENTER]=(input->pressed||input->touch_count)!=0;
        s->ui_ticks+=ms*136u;ww_intro_update(&s->intro,&keys,s->ui_ticks/1000u);s->ui_ticks%=1000u;
        if(ww_intro_complete(&s->intro)&&!home(s))return P4_GAME_ERROR;
        s->dirty=true;return P4_GAME_CONTINUE;
    }
    if(!s->racing || s->paused) {
        uint8_t old_screen=s->front.screen;
        unsigned action=ww_front_input(&s->front,input);
        if(input->pressed)ww_sound_effect(&s->sound,&s->archive,"PLIP.VOC");
        if(old_screen==WW_OPTIONS&&s->front.screen!=WW_OPTIONS)save_settings(s,ctx);
        if(action==WW_FRONT_EXIT){save_settings(s,ctx);return P4_GAME_EXIT_TO_LAUNCHER;}
        if(action==WW_FRONT_INTRO){if(!intro(s))return P4_GAME_ERROR;}
        if(action==WW_FRONT_LAUNCH||action==WW_FRONT_RESTART) {
            if(action==WW_FRONT_LAUNCH&&s->front.race_class==4)s->front.mode=WW_SINGLE;
            if(action==WW_FRONT_LAUNCH&&s->front.mode==WW_CUP){s->front.track=0;s->front.cup_points=0;}
            save_settings(s,ctx);
            if(!launch(s))return P4_GAME_ERROR;
        } else if(action==WW_FRONT_HOME) {save_settings(s,ctx);if(!home(s))return P4_GAME_ERROR;}
        else if(action==WW_FRONT_RESUME) {s->paused=false;ww_front_close(&s->front);}
        else if(s->paused&&(input->pressed&P4_BUTTON_START)) {s->paused=false;ww_front_close(&s->front);}
        s->dirty=true;return P4_GAME_CONTINUE;
    }
    if(s->results) {
        WwInput keys={0};keys.keys[WW_SCAN_ENTER]=(input->held&(P4_BUTTON_A|P4_BUTTON_START))!=0 ||
            (input->touch_valid&&input->touch_count);
        s->ui_ticks+=ms*136u;ww_victory_update(&s->victory,&keys,s->ui_ticks/1000u);s->ui_ticks%=1000u;
        if(ww_victory_complete(&s->victory)) {
            if(s->front.mode==WW_CUP&&s->front.track<4){++s->front.track;if(!launch(s))return P4_GAME_ERROR;}
            else if(!home(s))return P4_GAME_ERROR;
        }
        s->dirty=true;return P4_GAME_CONTINUE;
    }
    if (s->network.active) {
        if (ww_net_poll(&s->network, ctx, input->held, ms)) {
            remember_pose(s);
            if (!ww_net_view(&s->network, &s->race, &s->renderer)) return P4_GAME_ERROR;
            s->timer_units = 0;
        }
        if (!s->network.ended) {
            s->timer_units += ms * 136;
            if (s->network.host && s->timer_units >= 12000) {
                s->timer_units -= 12000; remember_pose(s);
                if (!ww_net_step(&s->network, &s->race, &s->renderer) ||
                    !ww_net_view(&s->network, &s->race, &s->renderer)) return P4_GAME_ERROR;
            }
            if (!s->network.host && s->timer_units > 12000) s->timer_units = 12000;
        }
        s->advance_pending = false; s->dirty = true;
        return P4_GAME_CONTINUE;
    }
    if (input->pressed & P4_BUTTON_START) {
        s->paused=true;s->front.screen=WW_PAUSE;s->front.row=0;
        if(!ww_front_open(&s->front,&s->archive))return P4_GAME_ERROR;
        s->dirty=true;
    }
    if (s->paused) return P4_GAME_CONTINUE;
    uint32_t buttons = input->held;
    /* Standard touch regions use the same canonical P4 control mapping. */
    WwInput keys = {0};
    keys.steering = (buttons & P4_BUTTON_LEFT) ? -32767 :
                    (buttons & P4_BUTTON_RIGHT) ? 32767 : 0;
    keys.throttle = (buttons & (P4_BUTTON_UP | P4_BUTTON_A)) ? 32767 :
                    (buttons & P4_BUTTON_DOWN) ? -32767 : 0;
    keys.fire = (buttons & P4_BUTTON_B) != 0;
    if (ms > P4_GAME_MAX_FRAME_DELTA_MS) ms = P4_GAME_MAX_FRAME_DELTA_MS;
    s->timer_units += ms * 136;
    /* Preserve the engine's 12/136-second simulation and animation step. */
    if (s->timer_units >= 12000) {
        s->timer_units -= 12000;
        remember_pose(s);
        WwRacerCrashState crash=s->race.racers[0].crash_state;
        uint16_t ammunition=s->race.player_weapon.ammunition;
        if (!ww_race_update(&s->race, &s->renderer, &keys, WW_ROAD_DETAIL_HIGH, 12))
            return P4_GAME_ERROR;
        if(!s->race.open) {ww_sound_effect(&s->sound,&s->archive,"NO.VOC");if(!home(s))return P4_GAME_ERROR;return P4_GAME_CONTINUE;}
        if(s->race.start_sound_pending){s->race.start_sound_pending=false;ww_sound_effect(&s->sound,&s->archive,"START.VOC");}
        if(s->race.water.splash_sound_pending){s->race.water.splash_sound_pending=false;ww_sound_effect(&s->sound,&s->archive,"SPLASH.VOC");}
        if(s->race.last_lap_sound_pending){s->race.last_lap_sound_pending=false;ww_sound_effect(&s->sound,&s->archive,"BELL.VOC");}
        if(crash==WW_RACER_CRASH_NONE&&s->race.racers[0].crash_state!=crash)ww_sound_effect(&s->sound,&s->archive,"BOOM.VOC");
        if(s->race.player_weapon.ammunition<ammunition)ww_sound_effect(&s->sound,&s->archive,"HOG.VOC");
        if(s->race.duck.score_sound_pending){s->race.duck.score_sound_pending=false;ww_sound_effect(&s->sound,&s->archive,"QUACK.VOC");}
        if(s->race.duck.active&&s->race.duck.finished) {
            uint8_t score=s->race.duck.score;
            if(!home(s))return P4_GAME_ERROR;
            s->front.screen=WW_RESULTS;s->front.result_score=score;
            if(score>s->front.duck_best)s->front.duck_best=score;
            save_settings(s,ctx);ww_sound_music(&s->sound,&s->archive,"LEADRBRD.MID");
        } else if(ww_finish_results_due(&s->race.finish,s->race.elapsed_136_ticks)) {
            if(s->front.race_class==4) {
                uint32_t time=s->race.finish.final_time_tenths;
                if(!home(s))return P4_GAME_ERROR;
                s->front.screen=WW_RESULTS;s->front.result_time=time;
                if(!s->front.best_time[s->front.track]||time<s->front.best_time[s->front.track])s->front.best_time[s->front.track]=time;
                save_settings(s,ctx);return P4_GAME_CONTINUE;
            }
            if(!ww_victory_open(&s->victory,&s->archive,&s->display,&s->race))return P4_GAME_ERROR;
            ww_race_close(&s->race);s->results=true;ww_sound_silence(&s->sound);
            ww_sound_music(&s->sound,&s->archive,"LEADRBRD.MID");
        }
        s->advance_pending = true;
    }
    s->dirty = true;
    return P4_GAME_CONTINUE;
}
static bool render(p4_game_context_t *ctx, p4_game_surface_t *surface)
{
    Probe *s = ctx->state;
    if (!surface || !surface->pixels || surface->width != 320 ||
        surface->height != 200 || surface->stride_pixels != 320) return false;
    if (!s->dirty) return true;
    s->display.pixels=(uint8_t *)surface->pixels;
    if(s->intro.open) {s->intro.dirty=true;ww_display_set_palette(&s->display,s->intro.image[s->intro.stage].palette);if(!ww_intro_render(&s->intro))return false;goto expand;}
    if(!s->racing||s->paused) {
        if(!ww_front_draw(&s->front,&s->display,&s->archive))return false;
        goto expand;
    }
    if(s->results) {if(!ww_victory_render(&s->victory))return false;goto expand;}
    const Pose camera = camera_pose(&s->race);
    Pose racers[WW_RACER_COUNT];
    for (size_t i = 0; i < WW_RACER_COUNT; ++i) racers[i] = racer_pose(&s->race.racers[i]);
    uint16_t horizon = s->race.horizon_source_offset;
    uint8_t *tail = (uint8_t *)&s->race + offsetof(WwRace, initial);
    memcpy(s->render_save, tail, sizeof(s->render_save));
    unsigned alpha = s->timer_units < 12000 ? s->timer_units : 11999;
    s->race.player.camera_x = blend_position(s->previous_camera.x, camera.x, alpha);
    s->race.player.camera_y = blend_position(s->previous_camera.y, camera.y, alpha);
    s->race.player.heading = blend_heading(s->previous_camera.heading, camera.heading, alpha);
    s->race.horizon_source_offset = blend_position(s->previous_horizon, horizon, alpha);
    for (size_t i = 0; i < WW_RACER_COUNT; ++i) {
        s->race.racers[i].world_x = blend_position(s->previous_racers[i].x, racers[i].x, alpha);
        s->race.racers[i].world_y = blend_position(s->previous_racers[i].y, racers[i].y, alpha);
        s->race.racers[i].heading = blend_heading(s->previous_racers[i].heading, racers[i].heading, alpha);
    }
    s->display.advance_simulation = s->advance_pending;
    s->display.pixels = (uint8_t *)surface->pixels;
    bool ok = ww_race_render_bringup(&s->race, &s->renderer, &s->display, WW_ROAD_DETAIL_HIGH);
    if (!s->advance_pending) memcpy(tail, s->render_save, sizeof(s->render_save));
    s->race.player.camera_x = camera.x; s->race.player.camera_y = camera.y;
    s->race.player.heading = camera.heading; s->race.horizon_source_offset = horizon;
    for (size_t i = 0; i < WW_RACER_COUNT; ++i) {
        s->race.racers[i].world_x = racers[i].x; s->race.racers[i].world_y = racers[i].y;
        s->race.racers[i].heading = racers[i].heading;
    }
    if (!ok) { s->display.pixels = NULL; return false; }
    s->advance_pending = false;
expand: ;
    uint16_t palette[256];
    for (size_t i = 0; i < 256; ++i) {
        const uint8_t *p = s->display.palette + i * 3;
        palette[i] = (uint16_t)(((uint16_t)(p[0] >> 3) << 11) |
                              ((uint16_t)(p[1] >> 2) << 5) | (p[2] >> 3));
    }
    for (size_t i = 64000; i-- > 0;) surface->pixels[i] = palette[s->display.pixels[i]];
    s->display.pixels = NULL;
    if(!s->intro.open&&(!s->racing||s->paused))ww_front_overlay(&s->front,surface);
    if(s->racing&&!s->paused&&!s->results)
        ww_controls_draw(surface,s->held_buttons,s->network.active);
    if (s->network.active) {
        const WwNetPlayer *p = &s->network.players[s->network.slot];
        const char *label = s->network.ended ? "PEER LEFT - BACK TO EXIT" :
            !s->network.heard_peer ? "WAITING FOR OTHER RACER" :
            p->lap.finished ? (p->lap.finish_place == 1 ? "YOU WIN! BACK TO EXIT" : "RACE FINISHED - BACK TO EXIT") :
            (const char *const[]){"P1 - THREE LAP RACE","P2 - THREE LAP RACE","P3 - THREE LAP RACE","P4 - THREE LAP RACE"}[s->network.slot];
        p4_draw_fill_rect(surface, 85, 46, 230, 12, 0x0000);
        p4_draw_text(surface, 90, 49, label, 0xffff, 1, 30);
    }
    s->dirty = false;
    return true;
}
const p4_game_descriptor_t p4_wacky_probe_game = {
    .api_version = P4_GAME_API_VERSION, .launcher_id = 9001,
    .id = "org.p4console.wacky-probe", .title = "Wacky Wheels",
    .subtitle = "Racing, duck shoot and MIDI", .accent_rgb565 = 0xffe0,
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS | P4_GAME_CAP_STORAGE,
    .optional_capabilities = P4_GAME_CAP_MULTIPLAYER_SESSION | P4_GAME_CAP_AUDIO_STREAM | P4_GAME_CAP_SAVE,
    .state_bytes = sizeof(Probe), .start = start, .update = update,
    .render = render, .stop = stop
};

#ifdef WW_P4_PROBE_TEST
/* Test-only state observation: verify render frequency cannot alter physics. */
static uint32_t mix_bytes(uint32_t h, const void *data, size_t n)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
uint32_t ww_p4_probe_logic(const void *state)
{
    const Probe *s = state;
    uint32_t h = mix_bytes(2166136261u, &s->race.player, sizeof(s->race.player));
    h = mix_bytes(h, s->race.racers, sizeof(s->race.racers));
    h = mix_bytes(h, s->race.track.spawn_records,
                  s->race.track.spawn_record_count * sizeof(WwSpawnRecord));
    h = mix_bytes(h, &s->race.water, sizeof(s->race.water));
    h = mix_bytes(h, &s->race.lap_alert, sizeof(s->race.lap_alert));
    return h;
}
#endif

#ifdef WW_P4_PROBE_TEST
const WwNet *ww_p4_probe_network(const void *state) { return &((const Probe *)state)->network; }
#endif

#ifdef WW_P4_PROBE_TEST
WwRace *ww_p4_probe_race(void *state) { return &((Probe *)state)->race; }
const WwRenderer *ww_p4_probe_renderer(const void *state) { return &((const Probe *)state)->renderer; }
#endif

#ifdef WW_P4_PROBE_TEST
WwFront *ww_p4_probe_front(void *state){return &((Probe *)state)->front;}
WwSoundPlayer *ww_p4_probe_sound(void *state){return &((Probe *)state)->sound;}
bool ww_p4_probe_launch(void *state,unsigned mode,unsigned track,unsigned vehicle){
    Probe *s=state;if(mode>WW_DUCK||track>4||vehicle>7)return false;
    s->front.mode=(uint8_t)mode;s->front.track=(uint8_t)track;s->front.vehicle=(uint8_t)vehicle;
    return launch(s);
}
#endif
#ifdef WW_P4_PROBE_TEST
const WwArchive *ww_p4_probe_archive(void *state){return &((Probe *)state)->archive;}
#endif

#ifdef WW_P4_PROBE_TEST
WwIntro *ww_p4_probe_intro(void *state){return &((Probe *)state)->intro;}
WwVictory *ww_p4_probe_victory(void *state){return &((Probe *)state)->victory;}
#endif
