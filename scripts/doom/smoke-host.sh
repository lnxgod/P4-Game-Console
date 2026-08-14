#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

P4_DG_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_DG_ROOT=$(CDPATH= cd -- "$P4_DG_SCRIPT_DIR/../.." && pwd)
P4_DG_BINARY="$P4_DG_ROOT/build-host/doom/doomgeneric-headless"
P4_DG_WAD=${1:-$P4_DG_ROOT/local-data/doom/doom1.wad}
P4_DG_EXPECTED_FRAMES=${P4_DOOM_MAX_FRAMES:-8}

P4_DG_WAD=$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$P4_DG_WAD")

if [ ! -f "$P4_DG_WAD" ] || [ ! -r "$P4_DG_WAD" ]; then
    echo "WAD is not a readable regular file: $P4_DG_WAD" >&2
    exit 2
fi

python3 - "$P4_DG_WAD" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
with path.open("rb") as wad:
    magic = wad.read(4)
if magic not in (b"IWAD", b"PWAD"):
    raise SystemExit(f"invalid WAD header {magic!r}: {path}")
PY

python3 - "$P4_DG_ROOT" "$P4_DG_WAD" <<'PY'
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
wad_path = pathlib.Path(sys.argv[2]).resolve()
manifest_path = root / "third_party" / "game-data.json"
with manifest_path.open(encoding="utf-8") as manifest_file:
    manifest = json.load(manifest_file)

matching = []
for entry in manifest["game_data"]:
    local_path = entry.get("local_path")
    if local_path is not None and (root / local_path).resolve() == wad_path:
        matching.append(entry)

if len(matching) > 1:
    raise SystemExit(f"duplicate game-data manifest entries for {wad_path}")
if not matching:
    print(f"P4_DOOM_D0 WAD UNPINNED path={wad_path}")
    raise SystemExit(0)

entry = matching[0]
expected_size = entry.get("size_bytes")
expected_sha256 = entry.get("sha256")
if not isinstance(expected_size, int) or expected_size <= 0:
    raise SystemExit(f"invalid size in {manifest_path} for {entry.get('id')}")
if not isinstance(expected_sha256, str) or len(expected_sha256) != 64:
    raise SystemExit(f"invalid SHA-256 in {manifest_path} for {entry.get('id')}")

size = wad_path.stat().st_size
digest = hashlib.sha256(wad_path.read_bytes()).hexdigest()
if size != expected_size or digest != expected_sha256:
    raise SystemExit(
        f"game-data identity mismatch for {wad_path}: "
        f"size={size} sha256={digest}"
    )
print(f"P4_DOOM_D0 WAD VERIFIED id={entry['id']} size={size} sha256={digest}")
PY

case "$P4_DG_WAD" in
    "$P4_DG_ROOT"/*)
        if ! git -C "$P4_DG_ROOT" check-ignore -q -- "$P4_DG_WAD"; then
            echo "a WAD inside the repository must be ignored: $P4_DG_WAD" >&2
            exit 2
        fi
        ;;
esac

if [ ! -x "$P4_DG_BINARY" ]; then
    "$P4_DG_SCRIPT_DIR/build-host.sh"
fi

P4_DG_TEMP_BASE=${TMPDIR:-/tmp}
P4_DG_TEMP_BASE=${P4_DG_TEMP_BASE%/}
P4_DG_RUN_DIR=$(mktemp -d "$P4_DG_TEMP_BASE/p4-doom-d0.XXXXXX")
case "$P4_DG_RUN_DIR" in
    "$P4_DG_TEMP_BASE"/p4-doom-d0.*) ;;
    *) echo "refusing unsafe temporary directory: $P4_DG_RUN_DIR" >&2; exit 1 ;;
esac
trap 'rm -rf "$P4_DG_RUN_DIR"' EXIT HUP INT TERM

P4_DG_LOG="$P4_DG_RUN_DIR/smoke.log"
(
    cd "$P4_DG_RUN_DIR"
    P4_DOOM_MAX_FRAMES="$P4_DG_EXPECTED_FRAMES" \
        "$P4_DG_BINARY" -iwad "$P4_DG_WAD" -nosound -nomusic -nosfx
) >"$P4_DG_LOG" 2>&1 || {
    sed -n '1,240p' "$P4_DG_LOG" >&2
    echo "P4_DOOM_D0 SMOKE FAIL engine exited before the frame marker" >&2
    exit 1
}

sed -n '1,240p' "$P4_DG_LOG"
if ! grep -q "^P4_DOOM_D0 PASS frames=$P4_DG_EXPECTED_FRAMES " "$P4_DG_LOG"; then
    echo "P4_DOOM_D0 SMOKE FAIL missing frame marker" >&2
    exit 1
fi

echo "P4_DOOM_D0 SMOKE PASS wad=$P4_DG_WAD"
