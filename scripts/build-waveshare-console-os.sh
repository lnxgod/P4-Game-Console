#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$P4_SCRIPT_DIR/build.sh" console_os \
    waveshare-esp32-p4-wifi6-touch-lcd-4.3
