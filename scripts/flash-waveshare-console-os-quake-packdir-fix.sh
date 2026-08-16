#!/bin/sh

set -eu

P4_WRAPPER_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$P4_WRAPPER_DIR/flash-waveshare-doom.sh" \
    --console-os-quake-packdir-fix "$@"
