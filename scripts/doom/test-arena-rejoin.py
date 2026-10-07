#!/usr/bin/env python3
"""Compare real Doom Arena state in independent host and cold-replay processes."""
import os
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(os.environ.get("P4_DOOM_REPO_ROOT", Path(__file__).resolve().parents[2])).resolve()
OVERRIDE = Path(os.environ["P4_DOOM_SOURCE_OVERRIDE"]).resolve() if os.environ.get("P4_DOOM_SOURCE_OVERRIDE") else None
SOURCE_ROOTS = [OVERRIDE, ROOT] if OVERRIDE else [ROOT]
OUT = ROOT / "build-host/doom-arena-rejoin"
OBJECTS = OUT / "objects"
SOURCE = ROOT / "third_party/doomgeneric/doomgeneric"
NET = Path("apps/doom_audio_probe/components/doom_engine_audio")
CC = os.environ.get("CC", "cc")
SANITIZERS = ["-fsanitize=address", "-fno-omit-frame-pointer"] if os.environ.get("P4_DOOM_ASAN", "1") == "1" else []
SOURCES = """dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop
d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom
i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox m_cheat m_config
m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy p_floor p_inter
p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight p_spec
p_switch p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky
r_things sha1 sounds statdump st_lib st_stuff s_sound tables v_video wi_stuff
w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric""".split()

def source(relative):
    if OVERRIDE and (OVERRIDE / relative).exists():
        return OVERRIDE / relative
    return ROOT / relative

def run(command, **kwargs):
    subprocess.run([str(value) for value in command], check=True, **kwargs)

def build():
    OBJECTS.mkdir(parents=True, exist_ok=True)
    includes = ["-I" + str(base / relative) for relative in
                (Path("third_party/doomgeneric/doomgeneric"), NET) for base in SOURCE_ROOTS]
    common = [CC, *SANITIZERS, "-std=c99", "-O2", "-g", "-Wall", "-Wextra",
              "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE", "-DNORMALUNIX",
              "-DLINUX", "-DSNDSERV", "-DDOOMGENERIC_RESX=320", "-DDOOMGENERIC_RESY=200"]
    upstream = [source(Path("third_party/doomgeneric/doomgeneric") / (name + ".c")) for name in SOURCES]
    with (OUT / "upstream-warnings.log").open("w") as log:
        run([*common, *includes, "-c", *upstream], cwd=OBJECTS, stderr=log)
    extras = [source(Path("apps/console_os/main/doom_gc_engine.c")),
              source(Path("components/doom_multiplayer/src/doom_arena.c")),
              source(NET / "p4_doom_net_stub.c"),
              source(Path("apps/doom/host/doom_arena_rejoin.c"))]
    project_includes = [argument for base in SOURCE_ROOTS for argument in
                        ("-isystem", str(base / "third_party/doomgeneric/doomgeneric"))]
    project_includes += ["-I" + str(base / relative) for relative in
                         (NET, Path("components/doom_multiplayer/include"),
                          Path("components/p4_multiplayer/include"),
                          Path("apps/console_os/main")) for base in SOURCE_ROOTS]
    for path in extras:
        run([*common, "-Wconversion", "-Wshadow", "-Werror", "-DP4_DOOM_ARENA_HOST_TEST=1",
             *project_includes, "-c", path, "-o", OBJECTS / (path.stem + ".o")])
    run([CC, *SANITIZERS, *[OBJECTS / (name + ".o") for name in SOURCES],
         *[OBJECTS / (path.stem + ".o") for path in extras], "-lm", "-o", OUT / "doom-arena-rejoin"])
    write_manifest(upstream, extras)

def write_manifest(upstream=None, extras=None):
    if upstream is None:
        upstream = [source(Path("third_party/doomgeneric/doomgeneric") / (name + ".c")) for name in SOURCES]
    if extras is None:
        extras = [source(Path("apps/console_os/main/doom_gc_engine.c")),
                  source(Path("components/doom_multiplayer/src/doom_arena.c")),
                  source(NET / "p4_doom_net_stub.c"),
                  source(Path("apps/doom/host/doom_arena_rejoin.c"))]
    inputs = [*upstream, *extras, source(Path("apps/doom/host/doom_rejoin_digest.h")),
              Path(__file__).resolve(), ROOT / "toolchain.lock.json", ROOT / "third_party/game-data.json",
              ROOT / "third_party/source-lock.json", ROOT / "third_party/doomgeneric-p4.json",
              ROOT / "third_party/doomgeneric-p4.patch",
              ROOT / "local-data/doom/freedoom2.wad",
              ROOT / "game-data/pure-hades/v0.6/PUREHADES.WAD",
              ROOT / "local-data/doom/arena-inbox/dwango5/DWANGO5.WAD"]
    manifest = {"repo_root": str(ROOT), "source_override": str(OVERRIDE) if OVERRIDE else None,
                "asan": bool(SANITIZERS), "compiler": subprocess.check_output([CC, "--version"], text=True).splitlines()[0],
                "fixture": "deterministic gameplay under identical headless presentation; synthetic clock; canonical-tic damage/item fixtures; no device or P4MP transport acceptance",
                "inputs": [{"path": str(path), "bytes": path.stat().st_size,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in inputs]}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

def process(role, tag=None):
    tag = tag or role
    environment = dict(os.environ, P4_REJOIN_ROLE=role,
                       P4_REJOIN_JOURNAL=str(OUT / "canonical-tics.txt"),
                       P4_REJOIN_STATES=str(OUT / (tag + "-states.txt")),
                       XDG_DATA_HOME=str(OUT / "userdata"),
                       ASAN_OPTIONS="detect_leaks=0:abort_on_error=1")
    command = [OUT / "doom-arena-rejoin", "-iwad", ROOT / "local-data/doom/freedoom2.wad",
               "-file", ROOT / "game-data/pure-hades/v0.6/PUREHADES.WAD",
               ROOT / "local-data/doom/arena-inbox/dwango5/DWANGO5.WAD",
               "-warp", "1", "-skill", "3", "-config", OUT / (role + ".cfg"),
               "-extraconfig", OUT / (role + "-extra.cfg")]
    with (OUT / (tag + ".log")).open("w") as log:
        run(command, env=environment, cwd=OUT, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    lines = (OUT / (tag + ".log")).read_text().splitlines()
    for line in lines:
        if line.startswith(("P4_DOOM_REJOIN", "REPLAY_", "FIXTURE", "ITEM_RESPAWN")):
            print(line)
    if not any("PROCESS PASS" in line for line in lines):
        raise RuntimeError("missing process result: " + role)

def compare():
    host = (OUT / "host-states.txt").read_text().splitlines()
    guest = (OUT / "guest-states.txt").read_text().splitlines()
    if len(host) != len(guest):
        raise RuntimeError(f"snapshot count differs: host={len(host)} guest={len(guest)}")
    for index, (left, right) in enumerate(zip(host, guest)):
        for role, snapshot in (("host", left), ("guest", right)):
            fields = dict(field.split("=", 1) for field in snapshot.split()[1:])
            if fields.get("tick") != str(index):
                raise RuntimeError(f"noncontiguous {role} snapshot {index}")
            if any(fields.get(key) != value for key, value in
                   (("valid", "1"), ("unresolved", "0"), ("unknown", "0"), ("malformed", "0"))):
                raise RuntimeError(f"invalid {role} snapshot {index}: {snapshot}")
        if left != right:
            print("HOST:", left, file=sys.stderr)
            print("GUEST:", right, file=sys.stderr)
            raise RuntimeError(f"canonical state differs at snapshot {index}")
    if len(host) != 2705:
        raise RuntimeError("expected every engine snapshot from tic 0 through 2704")
    print(f"P4_DOOM_REJOIN EQUIVALENCE PASS snapshots={len(host)} catchup=2000 activation=2048 resumed_live_tics=656 asan={bool(SANITIZERS)}")
    print("Evidence:", OUT)


def validate_wads():
    metadata = json.loads((ROOT / "third_party/game-data.json").read_text())
    entries = {entry["symbol"]: entry for entry in metadata["game_changers_ai_bundle"]["files"]}
    for symbol in ("BASE", "PWAD", "DWANGO"):
        entry = entries[symbol]
        path = ROOT / entry["local_path"]
        if path.stat().st_size != entry["size_bytes"] or hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            raise RuntimeError("pinned WAD mismatch: " + str(path))
        if not entry.get("repository_asset"):
            run(["git", "check-ignore", "-q", entry["local_path"]], cwd=ROOT)

def negative_consistency():
    previous = os.environ.get("P4_REJOIN_ZERO_CONSISTENCY")
    os.environ["P4_REJOIN_ZERO_CONSISTENCY"] = "1"
    try:
        try:
            process("guest", "negative-consistency")
        except subprocess.CalledProcessError:
            log = (OUT / "negative-consistency.log").read_text()
            if "NEGATIVE_FAULT tic=2000 kind=consistency-reset slot=3" not in log or "reason=guest local consistency did not replay" not in log:
                raise RuntimeError("negative control did not reject lost consistency")
            print("P4_DOOM_REJOIN NEGATIVE PASS fault=consistency-reset detected=first-live-submit")
        else:
            raise RuntimeError("negative control unexpectedly passed")
    finally:
        if previous is None:
            os.environ.pop("P4_REJOIN_ZERO_CONSISTENCY", None)
        else:
            os.environ["P4_REJOIN_ZERO_CONSISTENCY"] = previous

if __name__ == "__main__":
    try:
        validate_wads()
        if "--no-build" not in sys.argv:
            build()
        process("host")
        process("guest")
        compare()
        if "--negative-consistency" in sys.argv:
            negative_consistency()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, RuntimeError, OSError) as error:
        print("P4_DOOM_REJOIN RUN FAIL:", error, file=sys.stderr)
        print("Inspect logs:", OUT, file=sys.stderr)
        sys.exit(1)
