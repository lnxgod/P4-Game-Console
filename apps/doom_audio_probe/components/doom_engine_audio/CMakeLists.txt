# SPDX-License-Identifier: GPL-2.0-or-later

# Audio-enabled copy of the reviewed Doom engine source manifest. Keeping this
# component probe-local prevents its FEATURE_SOUND definitions and i_sound
# wrapper from changing the frozen, silent doom_engine component used by
# doom_embedded. Reconcile this list explicitly if that reviewed manifest moves.
set(P4_DOOMGENERIC_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/doomgeneric/doomgeneric")

set(P4_DOOM_ENGINE_AUDIO_SOURCES
    "${P4_DOOMGENERIC_DIR}/dummy.c"
    "${P4_DOOMGENERIC_DIR}/am_map.c"
    "${P4_DOOMGENERIC_DIR}/doomdef.c"
    "${P4_DOOMGENERIC_DIR}/doomstat.c"
    "${P4_DOOMGENERIC_DIR}/dstrings.c"
    "${P4_DOOMGENERIC_DIR}/d_event.c"
    "${P4_DOOMGENERIC_DIR}/d_items.c"
    "${P4_DOOMGENERIC_DIR}/d_iwad.c"
    "${P4_DOOMGENERIC_DIR}/d_loop.c"
    "${P4_DOOMGENERIC_DIR}/d_main.c"
    "${P4_DOOMGENERIC_DIR}/d_mode.c"
    "${P4_DOOMGENERIC_DIR}/d_net.c"
    "${P4_DOOMGENERIC_DIR}/f_finale.c"
    "${P4_DOOMGENERIC_DIR}/f_wipe.c"
    "${P4_DOOMGENERIC_DIR}/g_game.c"
    "${P4_DOOMGENERIC_DIR}/hu_lib.c"
    "${P4_DOOMGENERIC_DIR}/hu_stuff.c"
    "${P4_DOOMGENERIC_DIR}/info.c"
    "${P4_DOOMGENERIC_DIR}/i_cdmus.c"
    "${P4_DOOMGENERIC_DIR}/i_endoom.c"
    "${P4_DOOMGENERIC_DIR}/i_joystick.c"
    "${P4_DOOMGENERIC_DIR}/i_scale.c"
    "doom_i_sound_feature.c"
    "${P4_DOOMGENERIC_DIR}/i_system.c"
    "${P4_DOOMGENERIC_DIR}/i_timer.c"
    "${P4_DOOMGENERIC_DIR}/memio.c"
    "${P4_DOOMGENERIC_DIR}/m_argv.c"
    "${P4_DOOMGENERIC_DIR}/m_bbox.c"
    "${P4_DOOMGENERIC_DIR}/m_cheat.c"
    "${P4_DOOMGENERIC_DIR}/m_config.c"
    "${P4_DOOMGENERIC_DIR}/m_controls.c"
    "${P4_DOOMGENERIC_DIR}/m_fixed.c"
    "${P4_DOOMGENERIC_DIR}/m_menu.c"
    "${P4_DOOMGENERIC_DIR}/m_misc.c"
    "${P4_DOOMGENERIC_DIR}/m_random.c"
    "${P4_DOOMGENERIC_DIR}/p_ceilng.c"
    "${P4_DOOMGENERIC_DIR}/p_doors.c"
    "${P4_DOOMGENERIC_DIR}/p_enemy.c"
    "${P4_DOOMGENERIC_DIR}/p_floor.c"
    "${P4_DOOMGENERIC_DIR}/p_inter.c"
    "${P4_DOOMGENERIC_DIR}/p_lights.c"
    "${P4_DOOMGENERIC_DIR}/p_map.c"
    "${P4_DOOMGENERIC_DIR}/p_maputl.c"
    "${P4_DOOMGENERIC_DIR}/p_mobj.c"
    "${P4_DOOMGENERIC_DIR}/p_plats.c"
    "${P4_DOOMGENERIC_DIR}/p_pspr.c"
    "${P4_DOOMGENERIC_DIR}/p_saveg.c"
    "${P4_DOOMGENERIC_DIR}/p_setup.c"
    "${P4_DOOMGENERIC_DIR}/p_sight.c"
    "${P4_DOOMGENERIC_DIR}/p_spec.c"
    "${P4_DOOMGENERIC_DIR}/p_switch.c"
    "${P4_DOOMGENERIC_DIR}/p_telept.c"
    "${P4_DOOMGENERIC_DIR}/p_tick.c"
    "${P4_DOOMGENERIC_DIR}/p_user.c"
    "${P4_DOOMGENERIC_DIR}/r_bsp.c"
    "${P4_DOOMGENERIC_DIR}/r_data.c"
    "${P4_DOOMGENERIC_DIR}/r_draw.c"
    "${P4_DOOMGENERIC_DIR}/r_main.c"
    "${P4_DOOMGENERIC_DIR}/r_plane.c"
    "${P4_DOOMGENERIC_DIR}/r_segs.c"
    "${P4_DOOMGENERIC_DIR}/r_sky.c"
    "${P4_DOOMGENERIC_DIR}/r_things.c"
    "${P4_DOOMGENERIC_DIR}/sha1.c"
    "${P4_DOOMGENERIC_DIR}/sounds.c"
    "${P4_DOOMGENERIC_DIR}/statdump.c"
    "${P4_DOOMGENERIC_DIR}/st_lib.c"
    "${P4_DOOMGENERIC_DIR}/st_stuff.c"
    "${P4_DOOMGENERIC_DIR}/s_sound.c"
    "${P4_DOOMGENERIC_DIR}/tables.c"
    "${P4_DOOMGENERIC_DIR}/v_video.c"
    "${P4_DOOMGENERIC_DIR}/wi_stuff.c"
    "${P4_DOOMGENERIC_DIR}/w_checksum.c"
    "${P4_DOOMGENERIC_DIR}/w_file.c"
    "${P4_DOOMGENERIC_DIR}/w_main.c"
    "${P4_DOOMGENERIC_DIR}/w_wad.c"
    "${P4_DOOMGENERIC_DIR}/z_zone.c"
    "${P4_DOOMGENERIC_DIR}/w_file_stdc.c"
    "${P4_DOOMGENERIC_DIR}/i_input.c"
    "${P4_DOOMGENERIC_DIR}/i_video.c"
    "${P4_DOOMGENERIC_DIR}/doomgeneric.c"
)

idf_component_register(
    SRCS ${P4_DOOM_ENGINE_AUDIO_SOURCES}
    INCLUDE_DIRS "${P4_DOOMGENERIC_DIR}"
    REQUIRES doom_audio
    WHOLE_ARCHIVE
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    _DEFAULT_SOURCE
    _POSIX_C_SOURCE=200809L
    DOOMGENERIC_RESX=320
    DOOMGENERIC_RESY=200
    FEATURE_SOUND=1
    NORMALUNIX
    LINUX
    SNDSERV
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-error=dangling-pointer
    -Wno-error=format
    -Wno-error=format-truncation
    -Wno-error=unused-const-variable
    -Wno-error=unused-but-set-parameter
)
