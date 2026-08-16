APP ?= bringup
PORT ?=
WAD ?= local-data/doom/doom1.wad
DOOM_FRAMES ?= 8
DOOMGENERIC_SOURCE ?=
GAME ?= space_invaders
QUAKE_DATA ?= local-data/quake
QUAKE_FRAMES ?= 8

.PHONY: setup verify build check backup flash flash-app monitor doom-provenance doom-vendor doom-host doom-smoke doom-idf quake-provenance quake-host quake-smoke quake-play doom-audio-host doom-audio-idf platform-audio-host platform-audio-factory-host platform-touch-host doom-touch-host doom-touch-audio-host doom-touch-audio-idf console-shell-host console-os-host play-console-os p4-desktop-host p4-game-api-host p4-game-platform-host p4-content-host p4-multiplayer-host maze-chase-host space-invaders-host breakout-host asteroids-host asteroids-2-host frog-hop-host skyline-leap-host solitaire-host play-game game-registry-check game-sdk-host console-os-idf console-os-waveshare-idf gamepad-host gamepad-idf

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

quake-provenance:
	python3 ./scripts/quake/verify-quakegeneric.py

quake-host: quake-provenance
	cmake -S ports/quake -B build-host/quake -G Ninja -DP4_QUAKE_DATA_DIR="$(QUAKE_DATA)"
	cmake --build build-host/quake
	ctest --test-dir build-host/quake --output-on-failure -R p4_quake_host_usage

quake-smoke: quake-host
	python3 ./scripts/quake/verify-quakegeneric.py --require-data
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build-host/quake/p4_quake_host --headless --no-audio --frames "$(QUAKE_FRAMES)"

quake-play: quake-host
	python3 ./scripts/quake/verify-quakegeneric.py --require-data
	build-host/quake/p4_quake_host

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

console-os-host:
	cmake -S tools/p4-console-host -B build-host/p4-console-host -G Ninja
	cmake --build build-host/p4-console-host
	ctest --test-dir build-host/p4-console-host --output-on-failure

play-console-os: console-os-host
	"build-host/p4-console-host/p4_console_host.app/Contents/MacOS/p4_console_host"

p4-desktop-host:
	cmake -S components/p4_desktop -B build-host/p4_desktop -G Ninja
	cmake --build build-host/p4_desktop
	ctest --test-dir build-host/p4_desktop --output-on-failure

p4-game-api-host:
	cmake -S components/p4_game_api -B build-host/p4_game_api -G Ninja
	cmake --build build-host/p4_game_api
	ctest --test-dir build-host/p4_game_api --output-on-failure

p4-game-platform-host:
	cmake -S components/p4_game_platform -B build-host/p4_game_platform -G Ninja
	cmake --build build-host/p4_game_platform
	ctest --test-dir build-host/p4_game_platform --output-on-failure

p4-content-host:
	cmake -S components/p4_content_catalog -B build-host/p4_content_catalog -G Ninja
	cmake --build build-host/p4_content_catalog
	ctest --test-dir build-host/p4_content_catalog --output-on-failure
	python3 scripts/tests/test-p4-content.py

p4-multiplayer-host:
	cmake -S components/p4_multiplayer -B build-host/p4_multiplayer -G Ninja
	cmake --build build-host/p4_multiplayer
	ctest --test-dir build-host/p4_multiplayer --output-on-failure

maze-chase-host:
	cmake -S games/maze_chase -B build-host/maze_chase -G Ninja
	cmake --build build-host/maze_chase
	ctest --test-dir build-host/maze_chase --output-on-failure

space-invaders-host:
	cmake -S games/space_invaders -B build-host/space_invaders -G Ninja
	cmake --build build-host/space_invaders
	ctest --test-dir build-host/space_invaders --output-on-failure

breakout-host:
	cmake -S games/breakout -B build-host/breakout -G Ninja
	cmake --build build-host/breakout
	ctest --test-dir build-host/breakout --output-on-failure

asteroids-host:
	cmake -S games/asteroids -B build-host/asteroids -G Ninja
	cmake --build build-host/asteroids

asteroids-2-host:
	cmake -S games/asteroids_2 -B build-host/asteroids_2 -G Ninja
	cmake --build build-host/asteroids_2

frog-hop-host:
	cmake -S games/frog_hop -B build-host/frog_hop -G Ninja
	cmake --build build-host/frog_hop
	ctest --test-dir build-host/frog_hop --output-on-failure
	cmake -S tools/p4-game-host -B build-host/play-frog_hop -G Ninja -DP4_GAME=frog_hop
	cmake --build build-host/play-frog_hop
	ctest --test-dir build-host/play-frog_hop --output-on-failure

skyline-leap-host:
	cmake -S games/skyline_leap -B build-host/skyline_leap -G Ninja
	cmake --build build-host/skyline_leap
	ctest --test-dir build-host/skyline_leap --output-on-failure
	cmake -S tools/p4-game-host -B build-host/play-skyline_leap -G Ninja -DP4_GAME=skyline_leap
	cmake --build build-host/play-skyline_leap
	ctest --test-dir build-host/play-skyline_leap --output-on-failure

solitaire-host:
	cmake -S games/solitaire -B build-host/solitaire -G Ninja
	cmake --build build-host/solitaire
	cmake -S tools/p4-game-host -B build-host/play-solitaire -G Ninja -DP4_GAME=solitaire
	cmake --build build-host/play-solitaire
	ctest --test-dir build-host/play-solitaire --output-on-failure

play-game:
	cmake -S tools/p4-game-host -B "build-host/play-$(GAME)" -G Ninja -DP4_GAME="$(GAME)"
	cmake --build "build-host/play-$(GAME)"
	"build-host/play-$(GAME)/p4_game_host.app/Contents/MacOS/p4_game_host"

game-registry-check:
	python3 scripts/generate-game-registry.py --games-root games --check
	python3 scripts/tests/test-game-registry.py
	python3 scripts/tests/test-new-game.py

game-sdk-host: p4-desktop-host p4-game-api-host p4-game-platform-host p4-content-host p4-multiplayer-host maze-chase-host space-invaders-host breakout-host asteroids-host asteroids-2-host frog-hop-host skyline-leap-host solitaire-host game-registry-check

console-os-idf: console-os-waveshare-idf

console-os-waveshare-idf: console-shell-host game-sdk-host quake-provenance
	./scripts/build-waveshare-console-os.sh
	python3 ./scripts/verify-console-os.py apps/console_os/build-waveshare-landscape

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
