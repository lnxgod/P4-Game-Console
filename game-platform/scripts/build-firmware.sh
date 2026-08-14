#!/bin/sh

set -eu

P4_GAME_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_GAME_ROOT=$(CDPATH= cd -- "$P4_GAME_SCRIPT_DIR/.." && pwd)
P4_REPOSITORY_ROOT=$(CDPATH= cd -- "$P4_GAME_ROOT/.." && pwd)

P4_SCRIPT_DIR="$P4_REPOSITORY_ROOT/scripts"
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_activate_idf

P4_GAME_APP_DIR="$P4_GAME_ROOT/firmware/apps/freertos_proof"
P4_GAME_BUILD_DIR="$P4_GAME_APP_DIR/build"
idf.py -C "$P4_GAME_APP_DIR" -B "$P4_GAME_BUILD_DIR" -D IDF_TARGET=esp32p4 build

python3 -c '
import json
import pathlib
import sys

description = pathlib.Path(sys.argv[1]) / "project_description.json"
data = json.loads(description.read_text())
lock = json.loads(pathlib.Path(sys.argv[2]).read_text())
expected = lock["target"]
expected_minimum = expected["min_revision_full"]
expected_maximum = expected["max_revision_full"]
target = data.get("target")
if target != expected["chip"]:
    raise SystemExit(f"unexpected build target: {target!r}")
try:
    minimum = int(data.get("min_rev"))
    maximum = int(data.get("max_rev"))
except (TypeError, ValueError) as error:
    raise SystemExit("build did not report numeric silicon revision bounds") from error
if minimum != expected_minimum or maximum != expected_maximum:
    raise SystemExit(
        "unsafe silicon revision bounds: "
        f"built {minimum}..{maximum}, expected "
        f"{expected_minimum}..{expected_maximum} "
        "(" + expected["silicon_family"] + ")"
    )
' "$P4_GAME_BUILD_DIR" "$P4_REPOSITORY_ROOT/toolchain.lock.json"
