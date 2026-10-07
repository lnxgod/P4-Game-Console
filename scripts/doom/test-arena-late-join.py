#!/usr/bin/env python3
"""Real-engine proof: solo host, late first guest, departure, cold return."""
import os
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(os.environ.get("P4_DOOM_REPO_ROOT", next(path for path in Path(__file__).resolve().parents if (path / ".git").exists() and (path / "toolchain.lock.json").exists()))).resolve()
OVERRIDE = Path(os.environ.get("P4_DOOM_SOURCE_OVERRIDE", Path(__file__).resolve().parents[2])).resolve()
SOURCE_ROOTS = [OVERRIDE, ROOT] if OVERRIDE else [ROOT]
OUT = Path(os.environ.get("P4_DOOM_LATE_JOIN_OUTPUT", ROOT / "build-host/doom-arena-late-join")).resolve()
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

def selected_wads():
    metadata = json.loads(source(Path("third_party/game-data.json")).read_text())
    entries = {entry["symbol"]: entry for entry in metadata["game_changers_ai_bundle"]["files"]}
    return [(entries[symbol], source(Path(entries[symbol]["local_path"])))
            for symbol in ("BASE", "PWAD", "DWANGO")]

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
    extras = [source(Path("apps/console_os/main/doom_gc_engine.c")),
              source(Path("components/doom_multiplayer/src/doom_arena.c")),
              source(NET / "p4_doom_net_stub.c"),
              source(Path("apps/doom/host/doom_arena_late_join.c"))]
    write_manifest(upstream, extras)
    with (OUT / "upstream-warnings.log").open("w") as log:
        run([*common, *includes, "-c", *upstream], cwd=OBJECTS, stderr=log)
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
         *[OBJECTS / (path.stem + ".o") for path in extras], "-lm", "-o", OUT / "doom-arena-late-join"])

def write_manifest(upstream=None, extras=None):
    if upstream is None:
        upstream = [source(Path("third_party/doomgeneric/doomgeneric") / (name + ".c")) for name in SOURCES]
    if extras is None:
        extras = [source(Path("apps/console_os/main/doom_gc_engine.c")),
                  source(Path("components/doom_multiplayer/src/doom_arena.c")),
                  source(NET / "p4_doom_net_stub.c"),
                  source(Path("apps/doom/host/doom_arena_late_join.c"))]
    project_headers = [source(Path("components/doom_multiplayer/include/p4/doom_arena.h")),
                       source(NET / "p4_doom_net.h"), source(Path("apps/console_os/main/doom_arena_ui.h")),
                       source(Path("components/doom_multiplayer/include/p4/doom_multiplayer.h"))]
    project_headers += [source(path.relative_to(ROOT)) for path in
                        sorted((ROOT / "components/p4_multiplayer/include").rglob("*.h"))]
    vendor_headers = [path for path in sorted(source(Path("third_party/doomgeneric/doomgeneric")).rglob("*.h"))]
    inputs = [*upstream, *extras, *vendor_headers, *project_headers, source(Path("apps/doom/host/doom_late_join_digest.h")),
              Path(__file__).resolve(), ROOT / "toolchain.lock.json", source(Path("third_party/game-data.json")),
              ROOT / "third_party/source-lock.json", source(Path("third_party/doomgeneric-p4.json")),
              source(Path("third_party/doomgeneric-p4.patch")),
              *[path for _, path in selected_wads()]]
    manifest = {"repo_root": str(ROOT), "source_override": str(OVERRIDE) if OVERRIDE else None,
                "asan": bool(SANITIZERS), "compiler": subprocess.check_output([CC, "--version"], text=True).splitlines()[0],
                "fixture": "deterministic gameplay under identical headless presentation; synthetic clock; canonical-tic damage/item fixtures; no device or P4MP transport acceptance",
                "inputs": [{"path": str(path), "bytes": path.stat().st_size,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in inputs]}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

def process(role, tag=None):
    tag = tag or role
    environment = dict(os.environ, P4_LATE_JOIN_ROLE=role,
                       P4_LATE_JOIN_JOURNAL=str(OUT / "canonical-tics.txt"),
                       P4_LATE_JOIN_STATES=str(OUT / (tag + "-states.txt")),
                       XDG_DATA_HOME=str(OUT / "userdata"),
                       ASAN_OPTIONS="detect_leaks=0:abort_on_error=1")
    wad_paths = [path for _, path in selected_wads()]
    command = [OUT / "doom-arena-late-join", "-iwad", wad_paths[0],
               "-file", *wad_paths[1:],
               "-warp", "1", "-skill", "3", "-config", OUT / (role + ".cfg"),
               "-extraconfig", OUT / (role + "-extra.cfg")]
    with (OUT / (tag + ".log")).open("w") as log:
        run(command, env=environment, cwd=OUT, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    lines = (OUT / (tag + ".log")).read_text().splitlines()
    for line in lines:
        if line.startswith(("P4_DOOM_LATE_JOIN", "REPLAY_", "FIXTURE", "ITEM_RESPAWN", "FIRST_VISIT", "SECOND_VISIT", "SPAWN_OCCUPANCY")):
            print(line)
    if not any("PROCESS PASS" in line for line in lines):
        raise RuntimeError("missing process result: " + role)

def snapshots(role):
    lines = (OUT / (role + "-states.txt")).read_text().splitlines()
    for index, snapshot in enumerate(lines):
        fields = dict(field.split("=", 1) for field in snapshot.split()[1:])
        if fields.get("tick") != str(index):
            raise RuntimeError(f"noncontiguous {role} snapshot {index}")
        if any(fields.get(key) != value for key, value in
               (("valid", "1"), ("unresolved", "0"), ("unknown", "0"), ("malformed", "0"))):
            raise RuntimeError(f"invalid {role} snapshot {index}: {snapshot}")
    return lines

def compare():
    host = snapshots("host")
    if len(host) != 2705:
        raise RuntimeError("expected continuous host snapshots from tic 0 through 2704")
    evidence = {}
    for role, expected in (("guest-first", 1702), ("guest-rejoin", 2705), ("solo-reference", 1024)):
        guest = snapshots(role)
        if len(guest) != expected:
            raise RuntimeError(f"snapshot count differs: {role}={len(guest)} expected={expected}")
        for index, (left, right) in enumerate(zip(host, guest)):
            if left != right:
                print("HOST:", left, file=sys.stderr)
                print(role.upper() + ":", right, file=sys.stderr)
                raise RuntimeError(f"canonical state differs for {role} at snapshot {index}")
        evidence[role] = {"equal_snapshots": len(guest), "first_tic": 0, "last_tic": len(guest)-1}
        print(f"P4_DOOM_LATE_JOIN EQUIVALENCE PASS role={role} snapshots={len(guest)} asan={bool(SANITIZERS)}")
    print("P4_DOOM_LATE_JOIN RESERVED_SEATS PASS capacity=4 initial_mask=1 solo_reference_players=1 snapshots=1024")
    print("Evidence:", OUT)
    return evidence


def validate_wads():
    for entry, path in selected_wads():
        if path.is_symlink() or not path.is_file() or path.stat().st_size != entry["size_bytes"] or hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            raise RuntimeError("pinned WAD mismatch: " + str(path))
        if not entry.get("repository_asset"):
            run(["git", "check-ignore", "-q", path], cwd=ROOT)


def negative_control(variable, tag, fault, reason):
    previous = os.environ.get(variable)
    os.environ[variable] = "1"
    try:
        try:
            process("guest-rejoin", tag)
        except subprocess.CalledProcessError:
            log = (OUT / (tag + ".log")).read_text()
            if fault not in log or reason not in log:
                raise RuntimeError("negative control did not reject expected fault: " + tag)
            print("P4_DOOM_LATE_JOIN NEGATIVE PASS fault=" + tag)
            return {"fault": tag, "rejected": True, "log": str(OUT / (tag + ".log"))}
        else:
            raise RuntimeError("negative control unexpectedly passed: " + tag)
    finally:
        if previous is None:
            os.environ.pop(variable, None)
        else:
            os.environ[variable] = previous

def file_receipt(path):
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}

if __name__ == "__main__":
    try:
        validate_wads()
        if "--no-build" not in sys.argv:
            build()
        else:
            write_manifest()
        process("host")
        process("solo-reference")
        process("guest-first")
        process("guest-rejoin")
        evidence = compare()
        negatives = [negative_control("P4_LATE_JOIN_ALL_INITIAL", "negative-initial-mask",
            "NEGATIVE_FAULT tic=0 kind=all-seat-initial-mask mask=15",
            "reason=virgin reserved seat spawned or visited")]
        if "--negative-consistency" in sys.argv:
            negatives.append(negative_control("P4_LATE_JOIN_ZERO_CONSISTENCY", "negative-consistency",
                "NEGATIVE_FAULT tic=2000 kind=consistency-reset slot=1",
                "reason=guest local consistency did not replay"))
        manifest = json.loads((OUT / "manifest.json").read_text())
        for item in manifest["inputs"]:
            if file_receipt(Path(item["path"])) != item:
                raise RuntimeError("input changed during proof: " + item["path"])
        receipt = {"status": "PASS", "asan": bool(SANITIZERS),
            "capacity": 4, "initial_mask": 1, "guest_slot": 1,
            "first_catchup_tic": 960, "first_activation_tic": 1024,
            "departure_tic": 1700, "second_catchup_tic": 2000, "second_activation_tic": 2048,
            "host_continuous_snapshots": 2705, "comparisons": evidence, "negative_controls": negatives,
            "scope": "staged host engine proof; identical headless presentation; no P4MP or physical device acceptance",
            "outputs": [file_receipt(path) for path in (OUT / "doom-arena-late-join", OUT / "canonical-tics.txt",
                OUT / "host-states.txt", OUT / "solo-reference-states.txt", OUT / "guest-first-states.txt",
                OUT / "guest-rejoin-states.txt", OUT / "manifest.json", OUT / "host.log",
                OUT / "solo-reference.log", OUT / "guest-first.log", OUT / "guest-rejoin.log",
                *[Path(item["log"]) for item in negatives])]}
        (OUT / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, RuntimeError, OSError) as error:
        print("P4_DOOM_LATE_JOIN RUN FAIL:", error, file=sys.stderr)
        print("Inspect logs:", OUT, file=sys.stderr)
        sys.exit(1)
