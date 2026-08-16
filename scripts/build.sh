#!/bin/sh

set -eu

P4_APP=${1:-bringup}
P4_BOARD=${2:-elecrow-crowpanel-advanced-10}
P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_require_app "$P4_APP"
p4_activate_idf

P4_APP_DIR="$P4_PROJECT_ROOT/apps/$P4_APP"
case "$P4_BOARD" in
    elecrow-crowpanel-advanced-10)
        P4_BUILD_DIR="$P4_APP_DIR/build"
        P4_BOARD_DEFAULTS="$P4_PROJECT_ROOT/hardware/boards/elecrow-crowpanel-advanced-10/sdkconfig.defaults"
        P4_BOARD_ARGUMENTS="$P4_APP_DIR/sdkconfig.defaults;$P4_BOARD_DEFAULTS"
        ;;
    olimex-esp32-p4-pc)
        P4_BUILD_DIR="$P4_APP_DIR/build-olimex-esp32-p4-pc"
        P4_BOARD_DEFAULTS="$P4_PROJECT_ROOT/hardware/boards/olimex-esp32-p4-pc-rev-b/sdkconfig.defaults"
        P4_BOARD_ARGUMENTS="$P4_APP_DIR/sdkconfig.defaults;$P4_BOARD_DEFAULTS"
        ;;
    waveshare-esp32-p4-wifi6-touch-lcd-4.3)
        P4_BUILD_DIR="$P4_APP_DIR/build-waveshare-landscape"
        P4_BOARD_DEFAULTS="$P4_PROJECT_ROOT/hardware/boards/waveshare-esp32-p4-wifi6-touch-lcd-4.3/sdkconfig.defaults"
        P4_BOARD_ARGUMENTS="$P4_APP_DIR/sdkconfig.defaults;$P4_BOARD_DEFAULTS"
        ;;
    *)
        printf 'Unsupported board: %s\n' "$P4_BOARD" >&2
        printf '%s\n' 'Known boards: elecrow-crowpanel-advanced-10, olimex-esp32-p4-pc, waveshare-esp32-p4-wifi6-touch-lcd-4.3' >&2
        exit 2
        ;;
esac

idf.py -C "$P4_APP_DIR" -B "$P4_BUILD_DIR" \
    -D IDF_TARGET=esp32p4 \
    -D "P4_BOARD_PROFILE=$P4_BOARD" \
    -D "SDKCONFIG=$P4_BUILD_DIR/sdkconfig" \
    -D "SDKCONFIG_DEFAULTS=$P4_BOARD_ARGUMENTS" build

python3 -c '
import json, pathlib, sys
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
' "$P4_BUILD_DIR" "$P4_PROJECT_ROOT/toolchain.lock.json"
