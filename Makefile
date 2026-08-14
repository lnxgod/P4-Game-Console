APP ?= bringup
PORT ?=
WAD ?= local-data/doom/doom1.wad
DOOM_FRAMES ?= 8
DOOMGENERIC_SOURCE ?=

.PHONY: setup verify build check backup flash flash-app monitor doom-provenance doom-vendor doom-host doom-smoke doom-idf doom-audio-host doom-audio-idf platform-audio-host platform-audio-factory-host platform-touch-host doom-touch-host doom-touch-audio-host doom-touch-audio-idf console-shell-host p4-game-api-host p4-game-platform-host maze-chase-host game-registry-check game-sdk-host console-os-idf gamepad-host gamepad-idf

setup:
	./scripts/install-esp-idf.sh

verify:
	./scripts/verify-env.sh

build:
	./scripts/build.sh "$(APP)"

check:
	./scripts/check.sh

backup:
	./scripts/backup-flash.sh --port "$(PORT)"

flash:
	./scripts/flash.sh --app "$(APP)" --port "$(PORT)"

flash-app:
	./scripts/flash.sh --app "$(APP)" --port "$(PORT)" --app-only

monitor:
	./scripts/monitor.sh --app "$(APP)" --port "$(PORT)"

doom-provenance:
	./scripts/doom/verify-doomgeneric.sh

doom-idf: doom-provenance
	./scripts/build.sh doom
	./scripts/doom/verify-idf-build.py . test-runs/2026-08-12-doom-d05-idf-build.json

doom-vendor:
	./scripts/doom/vendor-doomgeneric.sh "$(DOOMGENERIC_SOURCE)"

doom-host: doom-provenance
	./scripts/doom/build-host.sh

doom-smoke: doom-host
	P4_DOOM_MAX_FRAMES="$(DOOM_FRAMES)" ./scripts/doom/smoke-host.sh "$(WAD)"

doom-audio-host:
	cmake -S apps/doom/components/doom_audio -B build-host/doom_audio -G Ninja
	cmake --build build-host/doom_audio
	ctest --test-dir build-host/doom_audio --output-on-failure

doom-audio-idf: doom-audio-host
	./scripts/build.sh doom_audio_probe

platform-audio-host:
	cmake -S components/platform_audio -B build-host/platform_audio -G Ninja
	cmake --build build-host/platform_audio
	ctest --test-dir build-host/platform_audio --output-on-failure

platform-audio-factory-host:
	cmake -S components/platform_audio_factory -B build-host/platform_audio_factory -G Ninja
	cmake --build build-host/platform_audio_factory
	ctest --test-dir build-host/platform_audio_factory --output-on-failure

platform-touch-host:
	cmake -S components/platform_touch -B build-host/platform_touch -G Ninja
	cmake --build build-host/platform_touch
	ctest --test-dir build-host/platform_touch --output-on-failure

doom-touch-host:
	cmake -S components/doom_touch_input -B build-host/doom_touch_input -G Ninja
	cmake --build build-host/doom_touch_input
	ctest --test-dir build-host/doom_touch_input --output-on-failure

doom-touch-audio-host: platform-audio-factory-host doom-touch-host platform-touch-host
	cmake -S apps/doom_embedded_touch_audio/tests -B build-host/doom_embedded_touch_audio -G Ninja
	cmake --build build-host/doom_embedded_touch_audio
	ctest --test-dir build-host/doom_embedded_touch_audio --output-on-failure
	cmake -S apps/doom_embedded_touch_audio/components/platform_audio/tests -B build-host/doom_embedded_touch_audio_platform_audio -G Ninja
	cmake --build build-host/doom_embedded_touch_audio_platform_audio
	ctest --test-dir build-host/doom_embedded_touch_audio_platform_audio --output-on-failure

doom-touch-audio-idf: doom-touch-audio-host
	./scripts/build.sh doom_embedded_touch_audio
	python3 ./scripts/verify-doom-embedded-touch-audio.py apps/doom_embedded_touch_audio/build build-only

console-shell-host:
	cmake -S components/console_shell -B build-host/console_shell -G Ninja
	cmake --build build-host/console_shell
	ctest --test-dir build-host/console_shell --output-on-failure

p4-game-api-host:
	cmake -S components/p4_game_api -B build-host/p4_game_api -G Ninja
	cmake --build build-host/p4_game_api
	ctest --test-dir build-host/p4_game_api --output-on-failure

p4-game-platform-host:
	cmake -S components/p4_game_platform -B build-host/p4_game_platform -G Ninja
	cmake --build build-host/p4_game_platform
	ctest --test-dir build-host/p4_game_platform --output-on-failure

maze-chase-host:
	cmake -S games/maze_chase -B build-host/maze_chase -G Ninja
	cmake --build build-host/maze_chase
	ctest --test-dir build-host/maze_chase --output-on-failure

game-registry-check:
	python3 scripts/generate-game-registry.py --games-root games --check
	python3 scripts/tests/test-game-registry.py
	python3 scripts/tests/test-new-game.py

game-sdk-host: p4-game-api-host p4-game-platform-host maze-chase-host game-registry-check

console-os-idf: console-shell-host game-sdk-host
	./scripts/build.sh console_os
	python3 ./scripts/verify-console-os.py apps/console_os/build

gamepad-host:
	cmake -S apps/gamepad_diag/tests -B build-host/gamepad_diag_arm -G Ninja
	cmake --build build-host/gamepad_diag_arm
	ctest --test-dir build-host/gamepad_diag_arm --output-on-failure
	cmake -S components/gamepad_core -B build-host/gamepad_core -G Ninja
	cmake --build build-host/gamepad_core
	ctest --test-dir build-host/gamepad_core --output-on-failure
	cmake -S components/platform_usb_host -B build-host/platform_usb_host -G Ninja
	cmake --build build-host/platform_usb_host
	ctest --test-dir build-host/platform_usb_host --output-on-failure
	cmake -S components/platform_gamepad_usb -B build-host/platform_gamepad_usb -G Ninja
	cmake --build build-host/platform_gamepad_usb
	ctest --test-dir build-host/platform_gamepad_usb --output-on-failure
	cmake -S components/doom_gamepad_input -B build-host/doom_gamepad_input -G Ninja
	cmake --build build-host/doom_gamepad_input
	ctest --test-dir build-host/doom_gamepad_input --output-on-failure

gamepad-idf: gamepad-host
	./scripts/build.sh gamepad_diag
