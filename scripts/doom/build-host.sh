#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

P4_DG_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_DG_ROOT=$(CDPATH= cd -- "$P4_DG_SCRIPT_DIR/../.." && pwd)
P4_DG_SOURCE_DIR="$P4_DG_ROOT/third_party/doomgeneric/doomgeneric"
P4_DG_NET_DIR="$P4_DG_ROOT/apps/doom_audio_probe/components/doom_engine_audio"
P4_DG_ADAPTER="$P4_DG_ROOT/apps/doom/host/doomgeneric_headless.c"
P4_DG_BUILD_DIR="$P4_DG_ROOT/build-host/doom"
P4_DG_OUTPUT="$P4_DG_BUILD_DIR/doomgeneric-headless"
P4_DG_WARNING_LOG="$P4_DG_BUILD_DIR/upstream-warnings.log"
P4_DG_CC=${CC:-cc}
P4_DG_SANITIZERS=
if [ "${P4_DOOM_ASAN:-0}" = 1 ]; then
    P4_DG_SANITIZERS="-fsanitize=address -fno-omit-frame-pointer"
fi

if [ ! -f "$P4_DG_SOURCE_DIR/doomgeneric.c" ]; then
    echo "doomgeneric source is missing; run make doom-vendor" >&2
    exit 1
fi

mkdir -p "$P4_DG_BUILD_DIR"
P4_DG_OBJECT_DIR=$(mktemp -d "$P4_DG_BUILD_DIR/objects.XXXXXX")
case "$P4_DG_OBJECT_DIR" in
    "$P4_DG_BUILD_DIR"/objects.*) ;;
    *) echo "refusing unsafe object directory: $P4_DG_OBJECT_DIR" >&2; exit 1 ;;
esac
trap 'rm -rf "$P4_DG_OBJECT_DIR"' EXIT HUP INT TERM
mkdir -p "$P4_DG_OBJECT_DIR/upstream"

set -- \
    "$P4_DG_SOURCE_DIR/dummy.c" "$P4_DG_SOURCE_DIR/am_map.c" \
    "$P4_DG_SOURCE_DIR/doomdef.c" "$P4_DG_SOURCE_DIR/doomstat.c" \
    "$P4_DG_SOURCE_DIR/dstrings.c" "$P4_DG_SOURCE_DIR/d_event.c" \
    "$P4_DG_SOURCE_DIR/d_items.c" "$P4_DG_SOURCE_DIR/d_iwad.c" \
    "$P4_DG_SOURCE_DIR/d_loop.c" "$P4_DG_SOURCE_DIR/d_main.c" \
    "$P4_DG_SOURCE_DIR/d_mode.c" "$P4_DG_SOURCE_DIR/d_net.c" \
    "$P4_DG_SOURCE_DIR/f_finale.c" "$P4_DG_SOURCE_DIR/f_wipe.c" \
    "$P4_DG_SOURCE_DIR/g_game.c" "$P4_DG_SOURCE_DIR/hu_lib.c" \
    "$P4_DG_SOURCE_DIR/hu_stuff.c" "$P4_DG_SOURCE_DIR/info.c" \
    "$P4_DG_SOURCE_DIR/i_cdmus.c" "$P4_DG_SOURCE_DIR/i_endoom.c" \
    "$P4_DG_SOURCE_DIR/i_joystick.c" "$P4_DG_SOURCE_DIR/i_scale.c" \
    "$P4_DG_SOURCE_DIR/i_sound.c" "$P4_DG_SOURCE_DIR/i_system.c" \
    "$P4_DG_SOURCE_DIR/i_timer.c" "$P4_DG_SOURCE_DIR/memio.c" \
    "$P4_DG_SOURCE_DIR/m_argv.c" "$P4_DG_SOURCE_DIR/m_bbox.c" \
    "$P4_DG_SOURCE_DIR/m_cheat.c" "$P4_DG_SOURCE_DIR/m_config.c" \
    "$P4_DG_SOURCE_DIR/m_controls.c" "$P4_DG_SOURCE_DIR/m_fixed.c" \
    "$P4_DG_SOURCE_DIR/m_menu.c" "$P4_DG_SOURCE_DIR/m_misc.c" \
    "$P4_DG_SOURCE_DIR/m_random.c" "$P4_DG_SOURCE_DIR/p_ceilng.c" \
    "$P4_DG_SOURCE_DIR/p_doors.c" "$P4_DG_SOURCE_DIR/p_enemy.c" \
    "$P4_DG_SOURCE_DIR/p_floor.c" "$P4_DG_SOURCE_DIR/p_inter.c" \
    "$P4_DG_SOURCE_DIR/p_lights.c" "$P4_DG_SOURCE_DIR/p_map.c" \
    "$P4_DG_SOURCE_DIR/p_maputl.c" "$P4_DG_SOURCE_DIR/p_mobj.c" \
    "$P4_DG_SOURCE_DIR/p_plats.c" "$P4_DG_SOURCE_DIR/p_pspr.c" \
    "$P4_DG_SOURCE_DIR/p_saveg.c" "$P4_DG_SOURCE_DIR/p_setup.c" \
    "$P4_DG_SOURCE_DIR/p_sight.c" "$P4_DG_SOURCE_DIR/p_spec.c" \
    "$P4_DG_SOURCE_DIR/p_switch.c" "$P4_DG_SOURCE_DIR/p_telept.c" \
    "$P4_DG_SOURCE_DIR/p_tick.c" "$P4_DG_SOURCE_DIR/p_user.c" \
    "$P4_DG_SOURCE_DIR/r_bsp.c" "$P4_DG_SOURCE_DIR/r_data.c" \
    "$P4_DG_SOURCE_DIR/r_draw.c" "$P4_DG_SOURCE_DIR/r_main.c" \
    "$P4_DG_SOURCE_DIR/r_plane.c" "$P4_DG_SOURCE_DIR/r_segs.c" \
    "$P4_DG_SOURCE_DIR/r_sky.c" "$P4_DG_SOURCE_DIR/r_things.c" \
    "$P4_DG_SOURCE_DIR/sha1.c" "$P4_DG_SOURCE_DIR/sounds.c" \
    "$P4_DG_SOURCE_DIR/statdump.c" "$P4_DG_SOURCE_DIR/st_lib.c" \
    "$P4_DG_SOURCE_DIR/st_stuff.c" "$P4_DG_SOURCE_DIR/s_sound.c" \
    "$P4_DG_SOURCE_DIR/tables.c" "$P4_DG_SOURCE_DIR/v_video.c" \
    "$P4_DG_SOURCE_DIR/wi_stuff.c" "$P4_DG_SOURCE_DIR/w_checksum.c" \
    "$P4_DG_SOURCE_DIR/w_file.c" "$P4_DG_SOURCE_DIR/w_main.c" \
    "$P4_DG_SOURCE_DIR/w_wad.c" "$P4_DG_SOURCE_DIR/z_zone.c" \
    "$P4_DG_SOURCE_DIR/w_file_stdc.c" "$P4_DG_SOURCE_DIR/i_input.c" \
    "$P4_DG_SOURCE_DIR/i_video.c" "$P4_DG_SOURCE_DIR/doomgeneric.c"

# Preserve diagnostics from pinned upstream plus the reviewed P4 patch.
# Keep normal checks concise. A failed upstream compile prints the complete log.
: >"$P4_DG_WARNING_LOG"
if ! (
    cd "$P4_DG_OBJECT_DIR/upstream"
    "$P4_DG_CC" $P4_DG_SANITIZERS \
        -std=c99 -O2 -g0 -Wall -Wextra \
        -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
        -DNORMALUNIX -DLINUX -DSNDSERV \
        -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
        -I"$P4_DG_SOURCE_DIR" -I"$P4_DG_NET_DIR" -c "$@"
) 2>"$P4_DG_WARNING_LOG"; then
    cat "$P4_DG_WARNING_LOG" >&2
    exit 1
fi

# Project-authored adapter diagnostics are fatal. Warning isolation above does
# not suppress or weaken warnings for code maintained by this project.
"$P4_DG_CC" $P4_DG_SANITIZERS \
    -std=c99 -O2 -g0 -Wall -Wextra -Wpedantic -Werror \
    -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
    -DP4_DOOM_ARENA_HOST_TEST="${P4_DOOM_ARENA_HOST_TEST:-0}" \
    -I"$P4_DG_SOURCE_DIR" -c "$P4_DG_ADAPTER" \
    -o "$P4_DG_OBJECT_DIR/doomgeneric_headless.o"

# The production engine calls the P4MP seam even in a single-player build.
# Compile its existing neutral adapter with project-owned warnings fatal.
"$P4_DG_CC" $P4_DG_SANITIZERS -std=c99 -O2 -g0 -Wall -Wextra -Werror \
    -isystem "$P4_DG_SOURCE_DIR" -I"$P4_DG_NET_DIR" \
    -c "$P4_DG_NET_DIR/p4_doom_net_stub.c" \
    -o "$P4_DG_OBJECT_DIR/p4_doom_net_stub.o"

if [ "${P4_DOOM_ARENA_HOST_TEST:-0}" = 1 ]; then
    P4_DG_OUTPUT="$P4_DG_BUILD_DIR/doom-arena-headless"
    for P4_DG_EXTRA in \
        "$P4_DG_ROOT/apps/console_os/main/doom_gc_engine.c" \
        "$P4_DG_ROOT/components/doom_multiplayer/src/doom_arena.c" \
        "$P4_DG_ROOT/apps/doom/host/doom_arena_smoke.c"
    do
        "$P4_DG_CC" $P4_DG_SANITIZERS -std=c99 -O2 -g -Wall -Wextra -Wconversion -Wshadow -Werror \
            -DP4_DOOM_ARENA_HOST_TEST=1 \
            -isystem "$P4_DG_SOURCE_DIR" -I"$P4_DG_NET_DIR" \
            -I"$P4_DG_ROOT/components/doom_multiplayer/include" \
            -I"$P4_DG_ROOT/components/p4_multiplayer/include" \
            -c "$P4_DG_EXTRA" -o "$P4_DG_OBJECT_DIR/upstream/$(basename "$P4_DG_EXTRA" .c).o"
    done
fi

"$P4_DG_CC" $P4_DG_SANITIZERS "$P4_DG_OBJECT_DIR/upstream"/*.o \
    "$P4_DG_OBJECT_DIR/doomgeneric_headless.o" \
    "$P4_DG_OBJECT_DIR/p4_doom_net_stub.o" -o "$P4_DG_OUTPUT" -lm

P4_DG_WARNING_COUNT=$(awk '/warning:/{count++} END{print count+0}' "$P4_DG_WARNING_LOG")

echo "P4_DOOM_D0 UPSTREAM WARNINGS count=$P4_DG_WARNING_COUNT log=$P4_DG_WARNING_LOG"
echo "P4_DOOM_D0 BUILD PASS binary=$P4_DG_OUTPUT"
