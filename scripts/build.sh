#!/bin/sh

set -eu

P4_APP=${1:-bringup}
P4_BOARD=${2:-elecrow-crowpanel-advanced-10}
P4_BOARD_PROFILE=$P4_BOARD
P4_WAVESHARE_CONTROLLER_FIRST_BUILD=0
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
    m5stack-tab5)
        [ "$P4_APP" = console_os ] || { printf "%s\n" "Tab5 currently supports console_os" >&2; exit 2; }
        P4_BUILD_DIR="$P4_APP_DIR/build-tab5"
        P4_BOARD_DEFAULTS="$P4_PROJECT_ROOT/hardware/boards/m5stack-tab5/sdkconfig.defaults"
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
        if [ "$P4_APP" = console_os ]; then
            P4_BLE_DEFAULTS="$P4_APP_DIR/sdkconfig.waveshare-ble.defaults"
            P4_BOARD_ARGUMENTS="$P4_BOARD_ARGUMENTS;$P4_BLE_DEFAULTS"
        fi
        ;;
    waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host)
        P4_BOARD_PROFILE=waveshare-esp32-p4-wifi6-touch-lcd-4.3
        P4_WAVESHARE_CONTROLLER_FIRST_BUILD=1
        P4_BUILD_DIR="$P4_APP_DIR/build-waveshare-usb-host"
        P4_BOARD_DEFAULTS="$P4_PROJECT_ROOT/hardware/boards/waveshare-esp32-p4-wifi6-touch-lcd-4.3/sdkconfig.defaults"
        P4_HOST_DEFAULTS="$P4_PROJECT_ROOT/hardware/boards/waveshare-esp32-p4-wifi6-touch-lcd-4.3/sdkconfig.usb-host-test.defaults"
        P4_BOARD_ARGUMENTS="$P4_APP_DIR/sdkconfig.defaults;$P4_BOARD_DEFAULTS;$P4_HOST_DEFAULTS"
        if [ "$P4_APP" = console_os ]; then
            P4_BLE_DEFAULTS="$P4_APP_DIR/sdkconfig.waveshare-ble.defaults"
            P4_BOARD_ARGUMENTS="$P4_BOARD_ARGUMENTS;$P4_BLE_DEFAULTS"
        fi
        ;;
    *)
        printf 'Unsupported board: %s\n' "$P4_BOARD" >&2
        printf '%s\n' 'Known boards: m5stack-tab5, elecrow-crowpanel-advanced-10, olimex-esp32-p4-pc, waveshare-esp32-p4-wifi6-touch-lcd-4.3, waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host' >&2
        exit 2
        ;;
esac

p4_idf_action() {
    P4_IDF_REQUESTED_ACTION=$1
    if [ "$P4_APP" = console_os ] &&
       [ "$P4_BOARD_PROFILE" = waveshare-esp32-p4-wifi6-touch-lcd-4.3 ]; then
        set -- -D P4_BLE_DIAGNOSTIC_AUTOSTART=OFF \
            "$P4_IDF_REQUESTED_ACTION"
    else
        set -- "$P4_IDF_REQUESTED_ACTION"
    fi
    if [ "$P4_APP" = console_os ] &&
       [ "$P4_WAVESHARE_CONTROLLER_FIRST_BUILD" -eq 1 ]; then
        idf.py -C "$P4_APP_DIR" -B "$P4_BUILD_DIR" \
            -D IDF_TARGET=esp32p4 \
            -D "P4_BOARD_PROFILE=$P4_BOARD_PROFILE" \
            -D P4_WAVESHARE_CONTROLLER_FIRST_BUILD=ON \
            -D "SDKCONFIG=$P4_BUILD_DIR/sdkconfig" \
            -D "SDKCONFIG_DEFAULTS=$P4_BOARD_ARGUMENTS" "$@"
    else
        idf.py -C "$P4_APP_DIR" -B "$P4_BUILD_DIR" \
            -D IDF_TARGET=esp32p4 \
            -D "P4_BOARD_PROFILE=$P4_BOARD_PROFILE" \
            -D "SDKCONFIG=$P4_BUILD_DIR/sdkconfig" \
            -D "SDKCONFIG_DEFAULTS=$P4_BOARD_ARGUMENTS" "$@"
    fi
}

p4_console_sd_root_is_exact() {
    P4_SD_ROOT=$1
    [ -d "$P4_SD_ROOT" ] || return 1
    P4_SD_ACTUAL=$(find "$P4_SD_ROOT" -mindepth 1 -maxdepth 1 \
        -exec basename {} \; | LC_ALL=C sort)
    P4_SD_EXPECTED='DOOM1.WAD
GAMES
P4
README.TXT
UPDATE'
    [ "$P4_SD_ACTUAL" = "$P4_SD_EXPECTED" ]
}

p4_console_sd_prune_generated_conflicts() {
    P4_SD_ROOT=$1
    [ -d "$P4_SD_ROOT" ] || return 1
    find "$P4_SD_ROOT" -mindepth 1 -maxdepth 1 -print |
        while IFS= read -r P4_SD_ENTRY; do
            P4_SD_NAME=${P4_SD_ENTRY##*/}
            case "$P4_SD_NAME" in
                DOOM1.WAD|GAMES|P4|README.TXT|UPDATE)
                    ;;
                *)
                    printf 'Removing generated SD conflict entry: %s\n' \
                        "$P4_SD_NAME"
                    if [ -d "$P4_SD_ENTRY" ]; then
                        cmake -E remove_directory "$P4_SD_ENTRY"
                    else
                        cmake -E remove "$P4_SD_ENTRY"
                    fi
                    ;;
            esac
        done
    p4_console_sd_root_is_exact "$P4_SD_ROOT"
}

# sdkconfig is generated output. Regenerate it from locked defaults whenever
# any part of the Waveshare C6 BLE/SDIO contract is missing. Checking the
# complete transport identity prevents an interrupted or manually regenerated
# build from silently falling back to ESP-Hosted's unsafe SPI pin defaults.
p4_waveshare_ble_sdkconfig_is_exact() {
    P4_SDKCONFIG=$1
    [ -f "$P4_SDKCONFIG" ] || return 1
    for P4_REQUIRED_CONFIG in \
        CONFIG_SLAVE_IDF_TARGET_ESP32C6=y \
        CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y \
        CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54 \
        CONFIG_ESP_HOSTED_SDIO_PIN_CLK=18 \
        CONFIG_ESP_HOSTED_SDIO_PIN_CMD=19 \
        CONFIG_ESP_HOSTED_SDIO_PIN_D0=14 \
        CONFIG_ESP_HOSTED_SDIO_PIN_D1=15 \
        CONFIG_ESP_HOSTED_SDIO_PIN_D2=16 \
        CONFIG_ESP_HOSTED_SDIO_PIN_D3=17 \
        CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768 \
        CONFIG_FREERTOS_HZ=1000 \
        CONFIG_BT_NIMBLE_ENABLED=y \
        CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING=y \
        CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2 \
        CONFIG_BT_NIMBLE_MAX_BONDS=4 \
        CONFIG_BT_NIMBLE_MAX_CCCDS=16 \
        CONFIG_BT_NIMBLE_NVS_PERSIST=y \
        CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y \
        CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y; do
        grep -q "^${P4_REQUIRED_CONFIG}$" "$P4_SDKCONFIG" || return 1
    done
}

if [ "$P4_APP" = console_os ] &&
   [ "$P4_BOARD_PROFILE" = waveshare-esp32-p4-wifi6-touch-lcd-4.3 ] &&
   [ -f "$P4_BUILD_DIR/sdkconfig" ] &&
   ! p4_waveshare_ble_sdkconfig_is_exact "$P4_BUILD_DIR/sdkconfig"; then
    printf 'Regenerating stale Waveshare Console OS sdkconfig for BLE.\n'
    cmake -E remove "$P4_BUILD_DIR/sdkconfig"
    p4_idf_action reconfigure
fi

# Finder and interrupted host-copy experiments can leave duplicate top-level
# entries in this generated tree (for example "GAMES 2"). CMake owns the
# staging directory and recreates it during configure, so force that safe
# cleanup only when an existing removable-media bundle is malformed.
if [ "$P4_APP" = console_os ] &&
   { [ "$P4_BOARD_PROFILE" = m5stack-tab5 ] ||
     [ "$P4_BOARD_PROFILE" = olimex-esp32-p4-pc ] ||
     [ "$P4_BOARD_PROFILE" = waveshare-esp32-p4-wifi6-touch-lcd-4.3 ]; } &&
   ! p4_console_sd_root_is_exact "$P4_BUILD_DIR/sd-card"; then
    printf 'Regenerating malformed Console OS SD staging root.\n'
    # This is CMake-owned generated output, never the mounted device card.
    # Reconfigure merges into an existing directory, so remove that exact
    # staging root first or macOS conflict copies such as "GAMES 2" survive.
    cmake -E remove_directory "$P4_BUILD_DIR/sd-card"
    p4_idf_action reconfigure
fi

p4_idf_action build

if [ "$P4_APP" = console_os ] &&
   { [ "$P4_BOARD_PROFILE" = m5stack-tab5 ] ||
     [ "$P4_BOARD_PROFILE" = olimex-esp32-p4-pc ] ||
     [ "$P4_BOARD_PROFILE" = waveshare-esp32-p4-wifi6-touch-lcd-4.3 ]; } &&
   ! p4_console_sd_root_is_exact "$P4_BUILD_DIR/sd-card"; then
    p4_console_sd_prune_generated_conflicts "$P4_BUILD_DIR/sd-card" || {
        printf 'Console OS SD staging root remains malformed after cleanup.\n' >&2
        exit 1
    }
fi

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
