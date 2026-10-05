#!/bin/sh
set -eu
P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_activate_idf
# M5Stack's legacy CMake registration splits paths containing spaces. Build a
# small source mirror at a private path without spaces; never patch dependencies.
P4_DICE_STAGE="/tmp/p4-dice-build-$(printf '%s' "$P4_PROJECT_ROOT" | shasum -a 256 | cut -c1-12)"
python3 "$P4_SCRIPT_DIR/stage-dice-core2.py" "$P4_PROJECT_ROOT" "$P4_DICE_STAGE"
idf.py -C "$P4_DICE_STAGE/apps/dice_core2" -B "$P4_DICE_STAGE/build" \
    -D SDKCONFIG="$P4_DICE_STAGE/build/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="$P4_DICE_STAGE/apps/dice_core2/sdkconfig.defaults" build
python3 "$P4_SCRIPT_DIR/stage-dice-core2.py" "$P4_PROJECT_ROOT" "$P4_DICE_STAGE" --collect
