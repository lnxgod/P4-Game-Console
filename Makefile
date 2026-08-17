APP ?= bringup
BOARD ?= elecrow-crowpanel-advanced-10
PORT ?=
WAD ?= local-data/doom/doom1.wad
DOOM_FRAMES ?= 8
DOOMGENERIC_SOURCE ?=

.PHONY: setup verify build check backup flash flash-app monitor doom-provenance doom-vendor doom-host doom-smoke doom-idf doom-audio-host doom-audio-idf platform-board-host platform-audio-host platform-audio-factory-host platform-touch-host platform-game-storage-host doom-touch-host doom-touch-audio-host doom-touch-audio-idf console-shell-host p4-game-api-host p4-game-package-host p4-os-update-package-host p4-game-platform-host p4-content-host maze-chase-host space-invaders-host byte-buddy-host game-registry-check game-sdk-host board-port-check console-os-idf console-os-olimex-idf console-os-waveshare-idf gamepad-host gamepad-idf install-olimex-sd-card install-waveshare-sd-card

setup:
	./scripts/install-esp-idf.sh

verify:
	./scripts/verify-env.sh

build:
	./scripts/build.sh "$(APP)" "$(BOARD)"

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

platform-board-host:
	python3 scripts/verify-board-profiles.py
	cmake -S components/platform_board -B build-host/platform_board -G Ninja
	cmake --build build-host/platform_board
	ctest --test-dir build-host/platform_board --output-on-failure

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

platform-game-storage-host:
	cmake -S components/platform_game_storage -B build-host/platform_game_storage -G Ninja
	cmake --build build-host/platform_game_storage
	ctest --test-dir build-host/platform_game_storage --output-on-failure

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

p4-game-package-host:
	cmake -S components/p4_game_package -B build-host/p4_game_package -G Ninja
	cmake --build build-host/p4_game_package
	ctest --test-dir build-host/p4_game_package --output-on-failure

p4-os-update-package-host:
	cmake -S components/p4_os_update_package -B build-host/p4_os_update_package -G Ninja
	cmake --build build-host/p4_os_update_package
	ctest --test-dir build-host/p4_os_update_package --output-on-failure

p4-game-platform-host:
	cmake -S components/p4_game_platform -B build-host/p4_game_platform -G Ninja
	cmake --build build-host/p4_game_platform
	ctest --test-dir build-host/p4_game_platform --output-on-failure

p4-content-host:
	cmake -S components/p4_content_catalog -B build-host/p4_content_catalog -G Ninja
	cmake --build build-host/p4_content_catalog
	ctest --test-dir build-host/p4_content_catalog --output-on-failure
	python3 scripts/tests/test-p4-content.py

maze-chase-host:
	cmake -S games/maze_chase -B build-host/maze_chase -G Ninja
	cmake --build build-host/maze_chase
	ctest --test-dir build-host/maze_chase --output-on-failure

space-invaders-host:
	cmake -S games/space_invaders -B build-host/space_invaders -G Ninja
	cmake --build build-host/space_invaders
	ctest --test-dir build-host/space_invaders --output-on-failure

byte-buddy-host:
	cmake -S games/byte_buddy -B build-host/byte_buddy -G Ninja
	cmake --build build-host/byte_buddy
	ctest --test-dir build-host/byte_buddy --output-on-failure

game-registry-check:
	python3 scripts/generate-game-registry.py --games-root games --check
	python3 scripts/tests/test-game-registry.py
	python3 scripts/tests/test-new-game.py

game-sdk-host: p4-game-api-host p4-game-package-host p4-os-update-package-host p4-game-platform-host p4-content-host maze-chase-host space-invaders-host byte-buddy-host game-registry-check

board-port-check:
	python3 scripts/board-port.py check
	python3 scripts/board-port.py matrix
	python3 scripts/tests/test-board-port.py

console-os-idf: board-port-check console-shell-host platform-game-storage-host game-sdk-host
	./scripts/build.sh console_os
	python3 ./scripts/verify-console-os.py apps/console_os/build
	python3 ./scripts/tests/test-console-os-game-manager-runtime-capture.py
	python3 ./scripts/tests/test-console-os-game-manager-migrate.py

console-os-olimex-idf: board-port-check platform-board-host console-shell-host platform-game-storage-host gamepad-host
	./scripts/build.sh console_os olimex-esp32-p4-pc
	python3 ./scripts/verify-console-os-olimex.py

console-os-waveshare-idf: board-port-check platform-board-host console-shell-host platform-game-storage-host game-sdk-host
	./scripts/build-waveshare-console-os.sh
	python3 ./scripts/verify-console-os-waveshare.py

install-olimex-sd-card:
	@test -n "$(SD_MOUNT)" || (echo "usage: make install-olimex-sd-card SD_MOUNT=/Volumes/P4GAMES" >&2; exit 2)
	./scripts/build.sh console_os olimex-esp32-p4-pc
	python3 ./scripts/install-olimex-sd-card.py --target "$(SD_MOUNT)"

install-waveshare-sd-card:
	@test -n "$(SD_MOUNT)" || (echo "usage: make install-waveshare-sd-card SD_MOUNT=/Volumes/P4GAMES" >&2; exit 2)
	./scripts/build.sh console_os waveshare-esp32-p4-wifi6-touch-lcd-4.3
	python3 ./scripts/verify-console-os-waveshare.py
	python3 ./scripts/install-olimex-sd-card.py \
		--bundle apps/console_os/build-waveshare-landscape/sd-card \
		--target "$(SD_MOUNT)"

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
