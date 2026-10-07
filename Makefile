APP ?= bringup
BOARD ?= $(if $(filter console_os,$(APP)),m5stack-tab5,elecrow-crowpanel-advanced-10)
PORT ?=
WAD ?= local-data/doom/doom1.wad
DOOM_FRAMES ?= 8
DOOMGENERIC_SOURCE ?=
GAME ?= space_invaders

.PHONY: prebuilt install-tools install-path-host
prebuilt:
	python3 scripts/fetch-prebuilt.py

install-tools:
	./scripts/setup-install-tools.sh

install-path-host:
	python3 scripts/tests/test-fetch-prebuilt.py
	python3 scripts/tests/test-tab5-prebuilt-install.py
	python3 scripts/tests/test-tab5-install.py
	python3 scripts/tests/test-tab5-release.py
	python3 scripts/tests/test-lite-source.py
	python3 scripts/tests/test-install-tools.py
	python3 scripts/tests/test-prepare-game-data.py
	python3 scripts/tests/test-sdk-setup.py
	sh scripts/tests/test-project-env.sh

.PHONY: setup verify build check backup flash flash-app monitor doom-provenance doom-vendor doom-host doom-smoke doom-idf doom-audio-host doom-audio-idf doom-multiplayer-host platform-board-host platform-audio-host platform-audio-factory-host platform-battery-host platform-touch-host platform-game-storage-host platform-save-seal-host h1-usb-drive-control-host doom-touch-host doom-touch-audio-host doom-touch-audio-idf console-shell-host console-os-host play-console-os p4-ansi-host p4-bbs-host p4-desktop-host p4-game-api-host p4-game-save-host p4-signal-scan-host p4-game-package-host p4-os-update-package-host p4-game-platform-host p4-content-host p4-multiplayer-host p4-multiplayer-registry-host p4-ble-radio-handoff-host lord-realm-e2e-host maze-chase-host space-invaders-host frog-hop-host byte-buddy-host skyline-leap-host solitaire-host p4-yahtzee-host calculator-host input-test-host av-test-host play-game game-registry-check game-sdk-host board-port-check console-os-idf console-os-elecrow-idf console-os-olimex-idf console-os-waveshare-idf gamepad-host gamepad-idf install-olimex-sd-card install-waveshare-sd-card

setup:
	./scripts/install-esp-idf.sh

verify:
	./scripts/verify-env.sh

.PHONY: prepare-game-data game-data-host
prepare-game-data:
	python3 scripts/prepare-game-data.py

game-data-host:
	python3 scripts/tests/test-prepare-game-data.py

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

platform-battery-host:
	cmake -S components/platform_battery -B build-host/platform_battery -G Ninja
	cmake --build build-host/platform_battery
	ctest --test-dir build-host/platform_battery --output-on-failure

platform-touch-host:
	cmake -S components/platform_touch -B build-host/platform_touch -G Ninja
	cmake --build build-host/platform_touch
	ctest --test-dir build-host/platform_touch --output-on-failure

platform-game-storage-host:
	cmake -S components/platform_game_storage -B build-host/platform_game_storage -G Ninja
	cmake --build build-host/platform_game_storage
	ctest --test-dir build-host/platform_game_storage --output-on-failure

h1-usb-drive-control-host:
	cmake -S components/p4_usb_content_transfer -B build-host/p4_usb_content_transfer -G Ninja
	cmake --build build-host/p4_usb_content_transfer
	ctest --test-dir build-host/p4_usb_content_transfer --output-on-failure
	python3 -m unittest scripts/tests/test-p4-transfer-usb-drive.py
	python3 -m unittest scripts/tests/test-p4-transfer-games.py scripts/tests/test-p4-usb-content-wire.py scripts/tests/test-p4-clock.py

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

.PHONY: console-startup-host
console-startup-host:
	cmake -S components/console_startup -B build-host/console_startup -G Ninja
	cmake --build build-host/console_startup
	ctest --test-dir build-host/console_startup --output-on-failure

console-shell-host:
	cmake -S components/console_shell -B build-host/console_shell -G Ninja
	cmake --build build-host/console_shell
	ctest --test-dir build-host/console_shell --output-on-failure

.PHONY: console-multiplayer-flow-host
console-multiplayer-flow-host:
	python3 scripts/tests/test-console-multiplayer-flow.py
	python3 scripts/tests/test-console-arena-resume.py
	python3 scripts/tests/test-console-arena-start.py
	python3 scripts/tests/test-console-doom-loading-boundary.py
	python3 scripts/tests/test-console-transport-response.py

console-os-host:
	cmake -S tools/p4-console-host -B build-host/p4-console-host -G Ninja
	cmake --build build-host/p4-console-host
	ctest --test-dir build-host/p4-console-host --output-on-failure

play-console-os: console-os-host
	"build-host/p4-console-host/p4_console_host.app/Contents/MacOS/p4_console_host"

p4-ansi-host:
	cmake -S components/p4_ansi -B build-host/p4_ansi -G Ninja
	cmake --build build-host/p4_ansi
	ctest --test-dir build-host/p4_ansi --output-on-failure

p4-bbs-host:
	cmake -S components/p4_bbs -B build-host/p4_bbs -G Ninja
	cmake --build build-host/p4_bbs
	ctest --test-dir build-host/p4_bbs --output-on-failure

p4-desktop-host:
	cmake -S components/p4_desktop -B build-host/p4_desktop -G Ninja
	cmake --build build-host/p4_desktop
	ctest --test-dir build-host/p4_desktop --output-on-failure

p4-game-api-host:
	cmake -S components/p4_game_api -B build-host/p4_game_api -G Ninja
	cmake --build build-host/p4_game_api
	ctest --test-dir build-host/p4_game_api --output-on-failure

p4-game-save-host:
	cmake -S components/p4_game_save -B build-host/p4_game_save -G Ninja
	cmake --build build-host/p4_game_save
	ctest --test-dir build-host/p4_game_save --output-on-failure

platform-save-seal-host:
	cmake -S components/platform_save_seal -B build-host/platform_save_seal -G Ninja
	cmake --build build-host/platform_save_seal
	ctest --test-dir build-host/platform_save_seal --output-on-failure

p4-signal-scan-host:
	cmake -S components/p4_signal_scan -B build-host/p4_signal_scan -G Ninja
	cmake --build build-host/p4_signal_scan
	ctest --test-dir build-host/p4_signal_scan --output-on-failure

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

.PHONY: p4-frame-scheduler-host
p4-frame-scheduler-host:
	cmake -S components/p4_frame_scheduler -B build-host/p4_frame_scheduler -G Ninja
	cmake --build build-host/p4_frame_scheduler
	ctest --test-dir build-host/p4_frame_scheduler --output-on-failure

p4-content-host:
	cmake -S components/p4_usb_content_transfer -B build-host/p4_usb_content_transfer -G Ninja
	cmake --build build-host/p4_usb_content_transfer
	ctest --test-dir build-host/p4_usb_content_transfer --output-on-failure
	python3 scripts/tests/test-p4-content.py
	python3 scripts/tests/test-native-sd-bundle.py

p4-multiplayer-host:
	cmake -S components/p4_multiplayer -B build-host/p4_multiplayer -G Ninja
	cmake --build build-host/p4_multiplayer
	ctest --test-dir build-host/p4_multiplayer --output-on-failure
	python3 scripts/tests/test-p4-multiplayer-relay.py

p4-multiplayer-registry-host:
	cmake -S components/p4_multiplayer_registry -B build-host/p4_multiplayer_registry -G Ninja
	cmake --build build-host/p4_multiplayer_registry
	ctest --test-dir build-host/p4_multiplayer_registry --output-on-failure

lord-realm-e2e-host:
	PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=. python3 -m unittest \
		tools.p4_realm_hub.tests.test_two_client_e2e \
		tools.p4_realm_hub.tests.test_four_player_campaign \
		tools.p4_realm_hub.tests.test_ten_player_chaos \
		tools.p4_realm_hub.tests.test_guild_campaign

doom-multiplayer-host:
	cmake -S components/doom_multiplayer -B build-host/doom_multiplayer -G Ninja
	cmake --build build-host/doom_multiplayer
	ctest --test-dir build-host/doom_multiplayer --output-on-failure

maze-chase-host:
	cmake -S games/maze_chase -B build-host/maze_chase -G Ninja
	cmake --build build-host/maze_chase
	ctest --test-dir build-host/maze_chase --output-on-failure

space-invaders-host:
	cmake -S games/space_invaders -B build-host/space_invaders -G Ninja
	cmake --build build-host/space_invaders
	ctest --test-dir build-host/space_invaders --output-on-failure

frog-hop-host:
	cmake -S games/frog_hop -B build-host/frog_hop -G Ninja
	cmake --build build-host/frog_hop
	ctest --test-dir build-host/frog_hop --output-on-failure
	cmake -S tools/p4-game-host -B build-host/play-frog_hop -G Ninja -DP4_GAME=frog_hop
	cmake --build build-host/play-frog_hop
	ctest --test-dir build-host/play-frog_hop --output-on-failure

byte-buddy-host:
	cmake -S games/byte_buddy -B build-host/byte_buddy -G Ninja
	cmake --build build-host/byte_buddy
	ctest --test-dir build-host/byte_buddy --output-on-failure
	cmake -S tools/p4-game-host -B build-host/play-byte_buddy -G Ninja -DP4_GAME=byte_buddy
	cmake --build build-host/play-byte_buddy
	ctest --test-dir build-host/play-byte_buddy --output-on-failure

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

p4-yahtzee-host:
	cmake -S games/p4_yahtzee -B build-host/p4_yahtzee-tests -G Ninja
	cmake --build build-host/p4_yahtzee-tests
	ctest --test-dir build-host/p4_yahtzee-tests --output-on-failure
	cmake -S tools/p4-game-host -B build-host/play-p4_yahtzee -G Ninja -DP4_GAME=p4_yahtzee
	cmake --build build-host/play-p4_yahtzee
	ctest --test-dir build-host/play-p4_yahtzee --output-on-failure

calculator-host:
	cmake -S games/calculator -B build-host/calculator -G Ninja
	cmake --build build-host/calculator
	ctest --test-dir build-host/calculator --output-on-failure
	cmake -S tools/p4-game-host -B build-host/play-calculator -G Ninja -DP4_GAME=calculator
	cmake --build build-host/play-calculator
	ctest --test-dir build-host/play-calculator --output-on-failure

input-test-host:
	cmake -S games/input_test -B build-host/input_test -G Ninja
	cmake --build build-host/input_test
	cmake -S tools/p4-game-host -B build-host/play-input_test -G Ninja -DP4_GAME=input_test
	cmake --build build-host/play-input_test
	ctest --test-dir build-host/play-input_test --output-on-failure

av-test-host:
	cmake -S games/av_test -B build-host/av_test -G Ninja
	cmake --build build-host/av_test
	cmake -S tools/p4-game-host -B build-host/play-av_test -G Ninja -DP4_GAME=av_test
	cmake --build build-host/play-av_test
	ctest --test-dir build-host/play-av_test --output-on-failure

play-game:
	cmake -S tools/p4-game-host -B "build-host/play-$(GAME)" -G Ninja -DP4_GAME="$(GAME)" -DP4_ALLOW_DRAFT_GAME=ON
	cmake --build "build-host/play-$(GAME)"
	"build-host/play-$(GAME)/p4_game_host.app/Contents/MacOS/p4_game_host"

game-registry-check:
	python3 scripts/generate-game-registry.py --games-root games --check --require-native-resolution
	python3 scripts/tests/test-console-native-resolution.py
	python3 scripts/tests/test-game-registry.py
	python3 scripts/tests/test-game-release.py
	python3 scripts/tests/test-native-board-verifiers.py
	python3 scripts/tests/test-game-resource.py
	python3 scripts/tests/test-protected-game-lineage.py
	python3 scripts/tests/test-new-game.py
	python3 scripts/tests/test-tab5-warm-boot-capture.py

.PHONY: dev-games install-dev
dev-games:
	P4_TAB5_DEV_GAMES_ONLY=1 ./scripts/build.sh console_os m5stack-tab5

install-dev:
	@test -n "$(GAME)" -a -n "$(PORT)" || { echo 'Usage: make install-dev GAME=byte_buddy PORT=/dev/cu.usbmodem...'; exit 2; }
	python3 scripts/install-dev-games.py --game "$(GAME)" --port "$(PORT)" $(if $(PROTECTED_PAYLOAD_SHA256),--protected-payload-sha256 "$(PROTECTED_PAYLOAD_SHA256)",)

game-sdk-host: p4-desktop-host p4-game-api-host p4-game-save-host platform-save-seal-host p4-signal-scan-host p4-game-package-host p4-os-update-package-host p4-game-platform-host p4-frame-scheduler-host p4-content-host p4-multiplayer-host p4-multiplayer-registry-host lord-realm-e2e-host maze-chase-host space-invaders-host frog-hop-host byte-buddy-host skyline-leap-host solitaire-host p4-yahtzee-host calculator-host input-test-host av-test-host game-registry-check

board-port-check:
	python3 scripts/board-port.py check
	python3 scripts/board-port.py matrix
	python3 scripts/tests/test-board-port.py

console-os-idf: console-os-tab5-idf

console-os-elecrow-idf: board-port-check console-shell-host platform-game-storage-host game-sdk-host
	./scripts/build.sh console_os elecrow-crowpanel-advanced-10
	python3 ./scripts/verify-console-os.py apps/console_os/build
	python3 ./scripts/tests/test-console-os-game-manager-runtime-capture.py

# Explicit artifact check for the retained one-time Elecrow migration route.
# Requires a matching committed Elecrow build; it does not build or flash.
.PHONY: console-os-game-manager-migration-check
console-os-game-manager-migration-check:
	python3 ./scripts/tests/test-console-os-game-manager-migrate.py

console-os-olimex-idf: board-port-check platform-board-host console-shell-host platform-game-storage-host gamepad-host
	./scripts/build.sh console_os olimex-esp32-p4-pc
	python3 ./scripts/verify-console-os-olimex.py
	python3 ./scripts/install-olimex-sd-card.py --check-bundle-only

console-os-waveshare-idf: board-port-check platform-board-host console-shell-host platform-game-storage-host game-sdk-host
	./scripts/build-waveshare-console-os.sh
	python3 ./scripts/verify-console-os-waveshare.py
	python3 ./scripts/install-olimex-sd-card.py \
		--bundle apps/console_os/build-waveshare-landscape/sd-card \
		--check-bundle-only

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
		--require-waveshare-h2-fat32 \
		--target "$(SD_MOUNT)"

p4-ble-radio-handoff-host:
	cmake -S components/p4_ble_radio_handoff -B build-host/p4_ble_radio_handoff -G Ninja
	cmake --build build-host/p4_ble_radio_handoff
	ctest --test-dir build-host/p4_ble_radio_handoff --output-on-failure

gamepad-host: p4-ble-radio-handoff-host gamepad-xusb-host
	python3 scripts/tests/test-espressif-usb-ext-port-overlay.py
	python3 scripts/tests/test-espressif-usb-hcd-fsls-overlay.py
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

gamepad-xusb-host:
	cmake -S components/platform_gamepad_xusb -B build-host/platform_gamepad_xusb -G Ninja
	cmake --build build-host/platform_gamepad_xusb
	ctest --test-dir build-host/platform_gamepad_xusb --output-on-failure

gamepad-idf: gamepad-host
	./scripts/build.sh gamepad_diag

.PHONY: console-os-tab5-idf tab5-host tab5-usb-host

tab5-usb-host:
	cmake -S components/platform_tab5/tests/usb_power -B build-host/tab5-usb-power-isolated -G Ninja
	cmake --build build-host/tab5-usb-power-isolated
	ctest --test-dir build-host/tab5-usb-power-isolated --output-on-failure


tab5-host: console-settings-host tab5-usb-host platform-board-host platform-touch-host console-shell-host platform-game-storage-host p4-os-update-package-host
	cmake -S components/platform_tab5/tests -B build-host/platform_tab5 -G Ninja
	cmake --build build-host/platform_tab5
	ctest --test-dir build-host/platform_tab5 --output-on-failure
	cmake -S components/platform_i2c_shared -B build-host/platform_i2c_shared -G Ninja
	cmake --build build-host/platform_i2c_shared
	ctest --test-dir build-host/platform_i2c_shared --output-on-failure
	cmake -S components/platform_display -B build-host/platform_display -G Ninja
	cmake --build build-host/platform_display
	ctest --test-dir build-host/platform_display --output-on-failure

console-os-tab5-idf: board-port-check console-startup-host tab5-host
	./scripts/build.sh console_os m5stack-tab5
	python3 scripts/verify-console-os-tab5.py $(if $(filter 1,$(P4_TAB5_FIRMWARE_ONLY)),--firmware-only,)

.PHONY: gamepad-xusb-host

.PHONY: console-settings-host
console-settings-host:
	cmake -S components/platform_console_settings/tests -B build-host/console-settings -G Ninja
	cmake --build build-host/console-settings
	ctest --test-dir build-host/console-settings --output-on-failure
