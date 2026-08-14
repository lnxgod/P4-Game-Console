#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

p4_native_component_test() {
    P4_COMPONENT_NAME=$1
    P4_COMPONENT_DIR="$P4_SCRIPT_DIR/../components/$P4_COMPONENT_NAME"
    P4_COMPONENT_BUILD="$P4_SCRIPT_DIR/../build-host/$P4_COMPONENT_NAME"
    if [ ! -f "$P4_COMPONENT_DIR/CMakeLists.txt" ]; then
        return
    fi
    cmake -S "$P4_COMPONENT_DIR" -B "$P4_COMPONENT_BUILD" -G Ninja
    cmake --build "$P4_COMPONENT_BUILD"
    ctest --test-dir "$P4_COMPONENT_BUILD" --output-on-failure
}

for P4_SHELL_FILE in \
    "$P4_SCRIPT_DIR"/*.sh \
    "$P4_SCRIPT_DIR"/doom/*.sh \
    "$P4_SCRIPT_DIR"/lib/*.sh \
    "$P4_SCRIPT_DIR"/tests/*.sh
do
    sh -n "$P4_SHELL_FILE"
done

"$P4_SCRIPT_DIR/tests/test-project-env.sh"
"$P4_SCRIPT_DIR/tests/test-app-readback.sh"
python3 "$P4_SCRIPT_DIR/tests/test-gamepad-diag-gate.py"
python3 "$P4_SCRIPT_DIR/tests/test-gamepad-diag-state-contract.py"
python3 "$P4_SCRIPT_DIR/tests/test-gamepad-diag-install.py"
python3 "$P4_SCRIPT_DIR/tests/test-gamepad-diag-restore.py"
python3 "$P4_SCRIPT_DIR/tests/test-gamepad-diag-flash-route.py"
python3 "$P4_SCRIPT_DIR/tests/test-doom-e5-runtime-capture.py"
python3 "$P4_SCRIPT_DIR/tests/test-doom-e5-install.py"
python3 "$P4_SCRIPT_DIR/tests/test-doom-e5-gate.py"
python3 "$P4_SCRIPT_DIR/verify-factory-variant.py"
python3 "$P4_SCRIPT_DIR/verify-display-path.py"
python3 "$P4_SCRIPT_DIR/verify-touch-path.py"
python3 "$P4_SCRIPT_DIR/verify-platform-touch.py"
python3 "$P4_SCRIPT_DIR/doom/verify-metadata.py" "$P4_SCRIPT_DIR/.."
"$P4_SCRIPT_DIR/build.sh" display_diag
"$P4_SCRIPT_DIR/doom/verify-doomgeneric.sh"
"$P4_SCRIPT_DIR/doom/build-host.sh"
P4_DEFAULT_DOOM_WAD="$P4_SCRIPT_DIR/../local-data/doom/doom1.wad"
if [ -f "$P4_DEFAULT_DOOM_WAD" ]; then
    P4_DOOM_MAX_FRAMES=8 \
        "$P4_SCRIPT_DIR/doom/smoke-host.sh" "$P4_DEFAULT_DOOM_WAD"
else
    printf 'P4_DOOM_D0 SMOKE SKIP reason=default-local-wad-absent path=%s\n' \
        "$P4_DEFAULT_DOOM_WAD"
fi
"$P4_SCRIPT_DIR/build.sh" doom
"$P4_SCRIPT_DIR/doom/verify-idf-build.py" \
    "$P4_SCRIPT_DIR/.." \
    "$P4_SCRIPT_DIR/../test-runs/2026-08-12-doom-d05-idf-build.json"

P4_DOOM_AUDIO_DIR="$P4_SCRIPT_DIR/../apps/doom/components/doom_audio"
P4_DOOM_AUDIO_BUILD="$P4_SCRIPT_DIR/../build-host/doom_audio"
cmake -S "$P4_DOOM_AUDIO_DIR" -B "$P4_DOOM_AUDIO_BUILD" -G Ninja
cmake --build "$P4_DOOM_AUDIO_BUILD"
ctest --test-dir "$P4_DOOM_AUDIO_BUILD" --output-on-failure
"$P4_SCRIPT_DIR/build.sh" doom_audio_probe

"$P4_SCRIPT_DIR/verify-env.sh"
"$P4_SCRIPT_DIR/build.sh" bringup
"$P4_SCRIPT_DIR/build.sh" gamepad_diag
python3 "$P4_SCRIPT_DIR/verify-gamepad-diag.py" \
    "$P4_SCRIPT_DIR/../apps/gamepad_diag/build" build-only

for P4_NATIVE_COMPONENT in \
    platform_audio \
    platform_audio_factory \
    gamepad_core \
    platform_usb_host \
    platform_gamepad_usb \
    doom_gamepad_input \
    doom_touch_input \
    platform_i2c_shared \
    platform_display \
    platform_touch \
    doom_video
do
    p4_native_component_test "$P4_NATIVE_COMPONENT"
done

P4_E5_HOST_BUILD="$P4_SCRIPT_DIR/../build-host/doom_embedded_touch_audio"
cmake -S "$P4_SCRIPT_DIR/../apps/doom_embedded_touch_audio/tests" \
    -B "$P4_E5_HOST_BUILD" -G Ninja
cmake --build "$P4_E5_HOST_BUILD"
ctest --test-dir "$P4_E5_HOST_BUILD" --output-on-failure
P4_E5_AUDIO_ADAPTER_BUILD="$P4_SCRIPT_DIR/../build-host/doom_embedded_touch_audio_platform_audio"
cmake -S "$P4_SCRIPT_DIR/../apps/doom_embedded_touch_audio/components/platform_audio/tests" \
    -B "$P4_E5_AUDIO_ADAPTER_BUILD" -G Ninja
cmake --build "$P4_E5_AUDIO_ADAPTER_BUILD"
ctest --test-dir "$P4_E5_AUDIO_ADAPTER_BUILD" --output-on-failure
"$P4_SCRIPT_DIR/build.sh" doom_embedded_touch_audio
python3 "$P4_SCRIPT_DIR/verify-doom-embedded-touch-audio.py" \
    "$P4_SCRIPT_DIR/../apps/doom_embedded_touch_audio/build" build-only
