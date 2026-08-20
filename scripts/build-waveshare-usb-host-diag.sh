#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_PROJECT_ROOT=$(CDPATH= cd -- "$P4_SCRIPT_DIR/.." && pwd)
P4_TINYUSB_SOURCE=${P4_TINYUSB_SOURCE:-"$P4_PROJECT_ROOT/apps/console_os/managed_components/espressif__tinyusb"}
P4_EXPECTED_COMPONENT_HASH=a72b7d67472914ab76309340fd50d578b31e310963d45ad0f81144bde3314752

if [ ! -f "$P4_TINYUSB_SOURCE/.component_hash" ]; then
    printf 'Pinned TinyUSB component is unavailable: %s\n' "$P4_TINYUSB_SOURCE" >&2
    exit 1
fi
P4_ACTUAL_COMPONENT_HASH=$(tr -d '[:space:]' < "$P4_TINYUSB_SOURCE/.component_hash")
if [ "$P4_ACTUAL_COMPONENT_HASH" != "$P4_EXPECTED_COMPONENT_HASH" ]; then
    printf 'Refusing TinyUSB component hash: %s\n' "$P4_ACTUAL_COMPONENT_HASH" >&2
    exit 1
fi

export P4_TINYUSB_SOURCE
exec "$P4_SCRIPT_DIR/build.sh" usb_host_diag \
    waveshare-esp32-p4-wifi6-touch-lcd-4.3
